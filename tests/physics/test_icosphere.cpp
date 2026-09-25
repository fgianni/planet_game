#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

int main() {
    planetsim::test::Context test;

    constexpr std::array<std::size_t, 7> expected_cells{12, 42, 162, 642, 2'562, 10'242, 40'962};
    constexpr std::array<std::size_t, 7> expected_corners{20,    80,     320,   1'280,
                                                          5'120, 20'480, 81'920};
    constexpr std::array<std::size_t, 7> expected_edges{30,    120,    480,    1'920,
                                                        7'680, 30'720, 122'880};

    for (std::uint32_t subdivision = 0; subdivision <= 6; ++subdivision) {
        const auto mesh = planetsim::make_icosphere(subdivision, 6'371'000.0);
        const auto level = static_cast<std::size_t>(subdivision);
        PLANETSIM_EXPECT(test, mesh.subdivision() == subdivision);
        PLANETSIM_EXPECT(test, mesh.cell_count() == expected_cells[level]);
        PLANETSIM_EXPECT(test, mesh.corner_count() == expected_corners[level]);
        PLANETSIM_EXPECT(test, mesh.edge_count() == expected_edges[level]);
        PLANETSIM_EXPECT(test, mesh.directed_edge_count() == 2U * expected_edges[level]);
        PLANETSIM_EXPECT(test, planetsim::expected_icosphere_cell_count(subdivision) ==
                                   expected_cells[level]);
        PLANETSIM_EXPECT(test, planetsim::expected_icosphere_corner_count(subdivision) ==
                                   expected_corners[level]);
        PLANETSIM_EXPECT(test, planetsim::expected_icosphere_edge_count(subdivision) ==
                                   expected_edges[level]);

        std::size_t pentagon_count = 0;
        for (const auto& cell : mesh.cells()) {
            PLANETSIM_EXPECT(test, cell.id.to_index() < mesh.cell_count());
            PLANETSIM_EXPECT_NEAR(test, planetsim::length(cell.center_unit), 1.0, 2.0e-15);
            PLANETSIM_EXPECT(test, std::isfinite(cell.area_m2));
            PLANETSIM_EXPECT(test, cell.area_m2 > 0.0);
            PLANETSIM_EXPECT(test, cell.edge_count == 5U || cell.edge_count == 6U);
            pentagon_count += cell.is_pentagon() ? 1U : 0U;

            const auto adjacency = mesh.cell_edges(cell.id);
            const auto corners = mesh.cell_corners(cell.id);
            PLANETSIM_EXPECT(test, adjacency.size() == cell.edge_count);
            PLANETSIM_EXPECT(test, corners.size() == cell.edge_count);
            for (std::size_t edge = 0; edge < adjacency.size(); ++edge) {
                PLANETSIM_EXPECT(test, adjacency[edge].neighbor.is_valid());
                PLANETSIM_EXPECT(test, adjacency[edge].neighbor.to_index() < mesh.cell_count());
                PLANETSIM_EXPECT(test, adjacency[edge].edge.is_valid());
                PLANETSIM_EXPECT(test, adjacency[edge].edge.to_index() < mesh.edge_count());
                PLANETSIM_EXPECT(test, corners[edge] < mesh.corner_count());

                const planetsim::Vec3d outward =
                    cell.east_unit * adjacency[edge].outward_normal_east +
                    cell.north_unit * adjacency[edge].outward_normal_north;
                const auto& neighbor = mesh.cell(adjacency[edge].neighbor);
                const planetsim::Vec3d toward_neighbor = planetsim::normalized(
                    neighbor.center_unit -
                    cell.center_unit * planetsim::dot(neighbor.center_unit, cell.center_unit));
                PLANETSIM_EXPECT(test, planetsim::dot(outward, toward_neighbor) > 0.0);
            }
        }
        PLANETSIM_EXPECT(test, pentagon_count == 12U);

        const auto diagnostics = planetsim::analyze_mesh(mesh);
        PLANETSIM_EXPECT(test, diagnostics.topology_valid());
        PLANETSIM_EXPECT(test, diagnostics.pentagon_count == 12U);
        PLANETSIM_EXPECT(test, diagnostics.hexagon_count + diagnostics.pentagon_count ==
                                   mesh.cell_count());
        PLANETSIM_EXPECT(test, diagnostics.invalid_neighbor_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.non_reciprocal_neighbor_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.invalid_edge_reference_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.invalid_corner_index_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.shared_corner_mismatch_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.edge_reference_count_mismatch == 0);
        PLANETSIM_EXPECT(test, diagnostics.invalid_block_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.non_finite_geometry_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.mean_neighbor_index_delta <
                                   diagnostics.naive_mean_neighbor_index_delta);
        if (subdivision == 5U) {
            constexpr std::size_t level_5_budget_bytes = 3U * 1'024U * 1'024U;
            PLANETSIM_EXPECT(test, diagnostics.allocated_bytes <=
                                       level_5_budget_bytes + level_5_budget_bytes / 5U);
        }
        if (subdivision == 6U) {
            constexpr std::size_t level_6_budget_bytes = 12U * 1'024U * 1'024U;
            PLANETSIM_EXPECT(test, diagnostics.allocated_bytes <=
                                       level_6_budget_bytes + level_6_budget_bytes / 5U);
        }
    }

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, planetsim::make_icosphere(0, 0.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, planetsim::make_icosphere(0, -1.0));

    return test.result();
}
