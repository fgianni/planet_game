#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

struct MeshDiagnostics {
    std::uint32_t subdivision = 0;
    std::size_t cell_count = 0;
    std::size_t corner_count = 0;
    std::size_t edge_count = 0;
    std::size_t directed_edge_count = 0;
    std::size_t block_count = 0;
    std::size_t pentagon_count = 0;
    std::size_t hexagon_count = 0;
    std::size_t allocated_bytes = 0;
    double total_area_m2 = 0.0;
    double expected_area_m2 = 0.0;
    double relative_area_error = 0.0;
    double min_cell_area_m2 = 0.0;
    double max_cell_area_m2 = 0.0;
    double min_edge_length_m = 0.0;
    double max_edge_length_m = 0.0;
    double min_centroid_distance_m = 0.0;
    double max_centroid_distance_m = 0.0;
    double max_center_norm_error = 0.0;
    double max_tangent_basis_error = 0.0;
    double mean_neighbor_index_delta = 0.0;
    double naive_mean_neighbor_index_delta = 0.0;
    std::size_t invalid_neighbor_count = 0;
    std::size_t non_reciprocal_neighbor_count = 0;
    std::size_t invalid_edge_reference_count = 0;
    std::size_t invalid_corner_index_count = 0;
    std::size_t shared_corner_mismatch_count = 0;
    std::size_t edge_reference_count_mismatch = 0;
    std::size_t invalid_block_count = 0;
    std::size_t non_finite_geometry_count = 0;
    bool counts_match_expected = false;

    [[nodiscard]] bool topology_valid() const noexcept;
};

[[nodiscard]] MeshDiagnostics analyze_mesh(const PlanetMesh& mesh);

}  // namespace planetsim
