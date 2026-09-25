#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cstddef>

int main() {
    planetsim::test::Context test;

    const auto first = planetsim::make_icosphere(4, 6'371'000.0);
    const auto second = planetsim::make_icosphere(4, 6'371'000.0);
    PLANETSIM_EXPECT(test, first.cell_count() == second.cell_count());
    PLANETSIM_EXPECT(test, first.corner_count() == second.corner_count());
    PLANETSIM_EXPECT(test, first.edge_count() == second.edge_count());

    for (std::size_t index = 0; index < first.corner_count(); ++index) {
        const auto& first_corner = first.corners_unit()[index];
        const auto& second_corner = second.corners_unit()[index];
        PLANETSIM_EXPECT(test, first_corner.x == second_corner.x);
        PLANETSIM_EXPECT(test, first_corner.y == second_corner.y);
        PLANETSIM_EXPECT(test, first_corner.z == second_corner.z);
    }

    for (std::size_t index = 0; index < first.cell_count(); ++index) {
        const auto& first_cell = first.cells()[index];
        const auto& second_cell = second.cells()[index];
        PLANETSIM_EXPECT(test, first_cell.id == second_cell.id);
        PLANETSIM_EXPECT(test, first_cell.center_unit.x == second_cell.center_unit.x);
        PLANETSIM_EXPECT(test, first_cell.center_unit.y == second_cell.center_unit.y);
        PLANETSIM_EXPECT(test, first_cell.center_unit.z == second_cell.center_unit.z);
        PLANETSIM_EXPECT(test, first_cell.east_unit.x == second_cell.east_unit.x);
        PLANETSIM_EXPECT(test, first_cell.east_unit.y == second_cell.east_unit.y);
        PLANETSIM_EXPECT(test, first_cell.east_unit.z == second_cell.east_unit.z);
        PLANETSIM_EXPECT(test, first_cell.north_unit.x == second_cell.north_unit.x);
        PLANETSIM_EXPECT(test, first_cell.north_unit.y == second_cell.north_unit.y);
        PLANETSIM_EXPECT(test, first_cell.north_unit.z == second_cell.north_unit.z);
        PLANETSIM_EXPECT(test, first_cell.area_m2 == second_cell.area_m2);
        PLANETSIM_EXPECT(test, first_cell.source_vertex_index == second_cell.source_vertex_index);
        PLANETSIM_EXPECT(test, first_cell.edge_offset == second_cell.edge_offset);
        PLANETSIM_EXPECT(test, first_cell.edge_count == second_cell.edge_count);

        const auto first_adjacency = first.cell_edges(first_cell.id);
        const auto second_adjacency = second.cell_edges(second_cell.id);
        PLANETSIM_EXPECT(test, first_adjacency.size() == second_adjacency.size());
        for (std::size_t edge = 0; edge < first_adjacency.size(); ++edge) {
            PLANETSIM_EXPECT(test,
                             first_adjacency[edge].neighbor == second_adjacency[edge].neighbor);
            PLANETSIM_EXPECT(test, first_adjacency[edge].edge == second_adjacency[edge].edge);
            PLANETSIM_EXPECT(test, first_adjacency[edge].outward_normal_east ==
                                       second_adjacency[edge].outward_normal_east);
            PLANETSIM_EXPECT(test, first_adjacency[edge].outward_normal_north ==
                                       second_adjacency[edge].outward_normal_north);
            PLANETSIM_EXPECT(test, first.cell_corners(first_cell.id)[edge] ==
                                       second.cell_corners(second_cell.id)[edge]);
        }
    }

    for (std::size_t index = 0; index < first.edge_count(); ++index) {
        const auto& first_edge = first.edges()[index];
        const auto& second_edge = second.edges()[index];
        PLANETSIM_EXPECT(test, first_edge.first_cell == second_edge.first_cell);
        PLANETSIM_EXPECT(test, first_edge.second_cell == second_edge.second_cell);
        PLANETSIM_EXPECT(test, first_edge.length_m == second_edge.length_m);
        PLANETSIM_EXPECT(test, first_edge.centroid_distance_m == second_edge.centroid_distance_m);
    }

    PLANETSIM_EXPECT(test, first.blocks().size() == second.blocks().size());
    for (std::size_t index = 0; index < first.blocks().size(); ++index) {
        PLANETSIM_EXPECT(test, first.blocks()[index].begin == second.blocks()[index].begin);
        PLANETSIM_EXPECT(test, first.blocks()[index].end == second.blocks()[index].end);
    }

    return test.result();
}
