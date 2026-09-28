#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_dijkstra.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] double geometric_cost(planetsim::CellId, const planetsim::CellEdgeGeometry& edge,
                                    std::uint32_t, const planetsim::PlanetMesh& mesh) {
    return mesh.edge(edge.edge).centroid_distance_m;
}

[[nodiscard]] double great_circle_m(const planetsim::PlanetMesh& mesh, planetsim::CellId a,
                                    planetsim::CellId b) {
    const double cosine = std::clamp(
        planetsim::dot(mesh.cell(a).center_unit, mesh.cell(b).center_unit), -1.0, 1.0);
    return mesh.radius_m() * std::acos(cosine);
}

[[nodiscard]] bool same_result(const planetsim::DijkstraResult& left,
                               const planetsim::DijkstraResult& right) {
    return left.cost == right.cost && left.label == right.label &&
           left.predecessor == right.predecessor;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const planetsim::PlanetMesh mesh = planetsim::make_icosphere(4, 6'371'000.0);
    const auto cost = [&](planetsim::CellId from, const planetsim::CellEdgeGeometry& edge,
                          std::uint32_t label) { return geometric_cost(from, edge, label, mesh); };

    // Single source: every cell reached; path length close to the geodesic.
    {
        const std::vector<planetsim::DijkstraSource> sources{{planetsim::CellId{0}, 0.0, 7U}};
        const auto result = planetsim::multi_source_dijkstra(mesh, sources, cost);
        double maximum_ratio = 0.0;
        for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
            const planetsim::CellId cell{static_cast<std::uint32_t>(index)};
            PLANETSIM_EXPECT(test, std::isfinite(result.cost[index]));
            PLANETSIM_EXPECT(test, result.label[index] == 7U);
            const double geodesic = great_circle_m(mesh, planetsim::CellId{0}, cell);
            if (geodesic > 5'000'000.0) {
                const double ratio = result.cost[index] / geodesic;
                PLANETSIM_EXPECT(test, ratio >= 0.999);
                maximum_ratio = std::max(maximum_ratio, ratio);
            }
            // Predecessor chains have constant label and strictly decreasing cost.
            if (index != 0U) {
                const planetsim::CellId parent = result.predecessor[index];
                PLANETSIM_EXPECT(test, parent.is_valid());
                PLANETSIM_EXPECT(test, result.cost[parent.to_index()] < result.cost[index]);
                PLANETSIM_EXPECT(test, result.label[parent.to_index()] == result.label[index]);
            }
        }
        PLANETSIM_EXPECT(test, !result.predecessor[0].is_valid());
        std::cout << "max_path_to_geodesic_ratio: " << maximum_ratio << '\n';
        // Edge paths on a hexagonal lattice exceed the geodesic by up to
        // 2/sqrt(3); the icosahedral seams add a little. Measured at L4 beyond
        // 5,000 km: 1.174 (2026-09-28).
        PLANETSIM_EXPECT(test, maximum_ratio < 1.2);
    }

    // Several sources: labels form a nearest-source partition and do not
    // depend on the order in which sources on distinct cells are given.
    {
        std::vector<planetsim::DijkstraSource> sources{
            {planetsim::CellId{5}, 0.0, 0U},
            {planetsim::CellId{900}, 0.0, 1U},
            {planetsim::CellId{1800}, 250'000.0, 2U},
            {planetsim::CellId{2500}, 0.0, 3U},
        };
        const auto forward = planetsim::multi_source_dijkstra(mesh, sources, cost);
        std::reverse(sources.begin(), sources.end());
        const auto reversed = planetsim::multi_source_dijkstra(mesh, sources, cost);
        PLANETSIM_EXPECT(test, same_result(forward, reversed));
        for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
            for (const auto& edge : mesh.cell_edges(planetsim::CellId{static_cast<std::uint32_t>(index)})) {
                // Triangle inequality over every edge.
                PLANETSIM_EXPECT(test, forward.cost[edge.neighbor.to_index()] <=
                                           forward.cost[index] + cost(planetsim::CellId{}, edge, 0U) *
                                                                     (1.0 + 1e-12));
            }
        }
        PLANETSIM_EXPECT(test, forward.label[900] == 1U);
        PLANETSIM_EXPECT(test, forward.cost[1800] == 250'000.0);
    }

    // Duplicate sources on one cell: the lower cost wins, then the first entry.
    {
        const std::vector<planetsim::DijkstraSource> sources{
            {planetsim::CellId{3}, 10.0, 1U},
            {planetsim::CellId{3}, 5.0, 2U},
            {planetsim::CellId{3}, 5.0, 3U},
        };
        const auto result = planetsim::multi_source_dijkstra(mesh, sources, cost);
        PLANETSIM_EXPECT(test, result.label[3] == 2U);
        PLANETSIM_EXPECT(test, result.cost[3] == 5.0);
    }

    // Blocked edges and a maximum cost leave cells unreached.
    {
        const std::vector<planetsim::DijkstraSource> sources{{planetsim::CellId{0}, 0.0, 0U}};
        const planetsim::Vec3d source_center = mesh.cell(planetsim::CellId{0}).center_unit;
        const auto hemisphere = [&](planetsim::CellId from, const planetsim::CellEdgeGeometry& edge,
                                    std::uint32_t label) {
            if (planetsim::dot(mesh.cell(edge.neighbor).center_unit, source_center) < 0.0) {
                return std::numeric_limits<double>::infinity();
            }
            return geometric_cost(from, edge, label, mesh);
        };
        const auto blocked = planetsim::multi_source_dijkstra(mesh, sources, hemisphere);
        const auto limited = planetsim::multi_source_dijkstra(mesh, sources, cost, 2'000'000.0);
        for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
            const planetsim::CellId cell{static_cast<std::uint32_t>(index)};
            const bool far_side = planetsim::dot(mesh.cell(cell).center_unit, source_center) < 0.0;
            PLANETSIM_EXPECT(test, std::isfinite(blocked.cost[index]) == !far_side);
            PLANETSIM_EXPECT(test, far_side || blocked.label[index] == 0U);
            PLANETSIM_EXPECT(test, !far_side || blocked.label[index] == planetsim::no_dijkstra_label);
            PLANETSIM_EXPECT(test, std::isinf(limited.cost[index]) || limited.cost[index] <= 2'000'000.0);
        }
        PLANETSIM_EXPECT(test, std::count_if(limited.cost.begin(), limited.cost.end(), [](double value) {
                                   return std::isinf(value);
                               }) > 0);
    }

    // Invalid input.
    {
        const std::vector<planetsim::DijkstraSource> outside{
            {planetsim::CellId{static_cast<std::uint32_t>(mesh.cell_count())}, 0.0, 0U}};
        PLANETSIM_EXPECT_THROWS(test, std::out_of_range,
                                planetsim::multi_source_dijkstra(mesh, outside, cost));
        const std::vector<planetsim::DijkstraSource> negative{{planetsim::CellId{0}, -1.0, 0U}};
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::multi_source_dijkstra(mesh, negative, cost));
        const std::vector<planetsim::DijkstraSource> valid{{planetsim::CellId{0}, 0.0, 0U}};
        const auto negative_edge = [](planetsim::CellId, const planetsim::CellEdgeGeometry&,
                                      std::uint32_t) { return -1.0; };
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::multi_source_dijkstra(mesh, valid, negative_edge));
    }

    return test.result();
}
