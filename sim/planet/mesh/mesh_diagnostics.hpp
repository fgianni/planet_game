#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

struct MeshDiagnostics {
    std::uint32_t subdivision = 0;
    std::size_t cell_count = 0;
    std::size_t vertex_count = 0;
    double total_area_m2 = 0.0;
    double expected_area_m2 = 0.0;
    double relative_area_error = 0.0;
    double min_cell_area_m2 = 0.0;
    double max_cell_area_m2 = 0.0;
    double min_edge_length_m = 0.0;
    double max_edge_length_m = 0.0;
    double max_center_norm_error = 0.0;
    std::size_t invalid_neighbor_count = 0;
    std::size_t non_reciprocal_neighbor_count = 0;
    std::size_t adjacency_edge_mismatch_count = 0;
    std::size_t invalid_vertex_index_count = 0;
    std::size_t non_finite_geometry_count = 0;
    bool counts_match_expected = false;

    [[nodiscard]] bool topology_valid() const noexcept;
};

[[nodiscard]] MeshDiagnostics analyze_mesh(const PlanetMesh& mesh);

}  // namespace planetsim
