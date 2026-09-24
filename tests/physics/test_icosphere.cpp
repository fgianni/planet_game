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

    constexpr std::array<std::size_t, 7> expected_cells{
        20, 80, 320, 1'280, 5'120, 20'480, 81'920};
    constexpr std::array<std::size_t, 7> expected_vertices{
        12, 42, 162, 642, 2'562, 10'242, 40'962};

    for (std::uint32_t subdivision = 0; subdivision <= 6; ++subdivision) {
        const auto mesh = planetsim::make_icosphere(subdivision, 6'371'000.0);
        const auto level = static_cast<std::size_t>(subdivision);
        PLANETSIM_EXPECT(test, mesh.subdivision() == subdivision);
        PLANETSIM_EXPECT(test, mesh.cell_count() == expected_cells[level]);
        PLANETSIM_EXPECT(test, mesh.vertex_count() == expected_vertices[level]);
        PLANETSIM_EXPECT(
            test,
            planetsim::expected_icosphere_cell_count(subdivision) == expected_cells[level]);
        PLANETSIM_EXPECT(
            test,
            planetsim::expected_icosphere_vertex_count(subdivision) ==
                expected_vertices[level]);

        for (const auto& cell : mesh.cells()) {
            PLANETSIM_EXPECT(test, cell.id.to_index() < mesh.cell_count());
            PLANETSIM_EXPECT_NEAR(test, planetsim::length(cell.center_unit), 1.0, 2.0e-15);
            PLANETSIM_EXPECT(test, std::isfinite(cell.area_m2));
            PLANETSIM_EXPECT(test, cell.area_m2 > 0.0);
            PLANETSIM_EXPECT(test, cell.neighbors[0] != cell.neighbors[1]);
            PLANETSIM_EXPECT(test, cell.neighbors[1] != cell.neighbors[2]);
            PLANETSIM_EXPECT(test, cell.neighbors[0] != cell.neighbors[2]);
            for (std::size_t edge = 0; edge < cell.neighbors.size(); ++edge) {
                PLANETSIM_EXPECT(test, cell.neighbors[edge].is_valid());
                PLANETSIM_EXPECT(test,
                                 cell.neighbors[edge].to_index() < mesh.cell_count());
                PLANETSIM_EXPECT(test, cell.edge_length_m[edge] > 0.0);
                PLANETSIM_EXPECT(test, std::isfinite(cell.edge_length_m[edge]));
            }
        }

        const auto diagnostics = planetsim::analyze_mesh(mesh);
        PLANETSIM_EXPECT(test, diagnostics.topology_valid());
        PLANETSIM_EXPECT(test, diagnostics.invalid_neighbor_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.non_reciprocal_neighbor_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.adjacency_edge_mismatch_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.invalid_vertex_index_count == 0);
        PLANETSIM_EXPECT(test, diagnostics.non_finite_geometry_count == 0);
    }

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::make_icosphere(0, 0.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::make_icosphere(0, -1.0));

    return test.result();
}
