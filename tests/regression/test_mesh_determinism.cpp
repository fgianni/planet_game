#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cstddef>

int main() {
    planetsim::test::Context test;

    const auto first = planetsim::make_icosphere(4, 6'371'000.0);
    const auto second = planetsim::make_icosphere(4, 6'371'000.0);
    PLANETSIM_EXPECT(test, first.cell_count() == second.cell_count());
    PLANETSIM_EXPECT(test, first.vertex_count() == second.vertex_count());

    for (std::size_t index = 0; index < first.vertex_count(); ++index) {
        const auto& first_vertex = first.vertices_unit()[index];
        const auto& second_vertex = second.vertices_unit()[index];
        PLANETSIM_EXPECT(test, first_vertex.x == second_vertex.x);
        PLANETSIM_EXPECT(test, first_vertex.y == second_vertex.y);
        PLANETSIM_EXPECT(test, first_vertex.z == second_vertex.z);
    }

    for (std::size_t index = 0; index < first.cell_count(); ++index) {
        const auto& first_cell = first.cells()[index];
        const auto& second_cell = second.cells()[index];
        PLANETSIM_EXPECT(test, first_cell.id == second_cell.id);
        PLANETSIM_EXPECT(test, first_cell.center_unit.x == second_cell.center_unit.x);
        PLANETSIM_EXPECT(test, first_cell.center_unit.y == second_cell.center_unit.y);
        PLANETSIM_EXPECT(test, first_cell.center_unit.z == second_cell.center_unit.z);
        PLANETSIM_EXPECT(test, first_cell.area_m2 == second_cell.area_m2);
        PLANETSIM_EXPECT(test, first_cell.neighbors == second_cell.neighbors);
        PLANETSIM_EXPECT(test, first_cell.edge_length_m == second_cell.edge_length_m);
        PLANETSIM_EXPECT(test, first_cell.vertex_indices == second_cell.vertex_indices);
    }

    return test.result();
}
