#include "sim/planet/mesh/mesh_diagnostics.hpp"

#include "sim/planet/mesh/icosphere.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace planetsim {
namespace {

[[nodiscard]] bool contains_neighbor(const CellGeometry& cell, CellId expected) noexcept {
    return std::find(cell.neighbors.begin(), cell.neighbors.end(), expected) !=
           cell.neighbors.end();
}

[[nodiscard]] bool has_undirected_edge(const CellGeometry& cell,
                                       VertexIndex first,
                                       VertexIndex second) noexcept {
    for (std::size_t edge = 0; edge < cell.vertex_indices.size(); ++edge) {
        const auto candidate_first = cell.vertex_indices[edge];
        const auto candidate_second =
            cell.vertex_indices[(edge + 1U) % cell.vertex_indices.size()];
        if ((candidate_first == first && candidate_second == second) ||
            (candidate_first == second && candidate_second == first)) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool MeshDiagnostics::topology_valid() const noexcept {
    return counts_match_expected && invalid_neighbor_count == 0 &&
           non_reciprocal_neighbor_count == 0 &&
           adjacency_edge_mismatch_count == 0 &&
           invalid_vertex_index_count == 0 && non_finite_geometry_count == 0;
}

MeshDiagnostics analyze_mesh(const PlanetMesh& mesh) {
    MeshDiagnostics result;
    result.subdivision = mesh.subdivision();
    result.cell_count = mesh.cell_count();
    result.vertex_count = mesh.vertex_count();
    result.expected_area_m2 =
        4.0 * std::numbers::pi_v<double> * mesh.radius_m() * mesh.radius_m();
    result.min_cell_area_m2 = std::numeric_limits<double>::infinity();
    result.min_edge_length_m = std::numeric_limits<double>::infinity();
    result.counts_match_expected =
        result.cell_count == expected_icosphere_cell_count(mesh.subdivision()) &&
        result.vertex_count == expected_icosphere_vertex_count(mesh.subdivision());

    double compensated_area = 0.0;
    for (const auto& cell : mesh.cells()) {
        const double corrected_area = cell.area_m2 - compensated_area;
        const double next_total = result.total_area_m2 + corrected_area;
        compensated_area = (next_total - result.total_area_m2) - corrected_area;
        result.total_area_m2 = next_total;

        result.min_cell_area_m2 = std::min(result.min_cell_area_m2, cell.area_m2);
        result.max_cell_area_m2 = std::max(result.max_cell_area_m2, cell.area_m2);
        result.max_center_norm_error =
            std::max(result.max_center_norm_error, std::abs(length(cell.center_unit) - 1.0));

        if (!is_finite(cell.center_unit) || !std::isfinite(cell.area_m2) ||
            !(cell.area_m2 > 0.0)) {
            ++result.non_finite_geometry_count;
        }

        for (std::size_t edge = 0; edge < cell.neighbors.size(); ++edge) {
            const double edge_length_m = cell.edge_length_m[edge];
            result.min_edge_length_m = std::min(result.min_edge_length_m, edge_length_m);
            result.max_edge_length_m = std::max(result.max_edge_length_m, edge_length_m);
            if (!std::isfinite(edge_length_m) || !(edge_length_m > 0.0)) {
                ++result.non_finite_geometry_count;
            }

            const CellId neighbor_id = cell.neighbors[edge];
            if (!neighbor_id.is_valid() || neighbor_id.to_index() >= mesh.cell_count() ||
                neighbor_id == cell.id) {
                ++result.invalid_neighbor_count;
                continue;
            }

            const auto& neighbor = mesh.cell(neighbor_id);
            if (!contains_neighbor(neighbor, cell.id)) {
                ++result.non_reciprocal_neighbor_count;
            }

            const auto first_vertex = cell.vertex_indices[edge];
            const auto second_vertex =
                cell.vertex_indices[(edge + 1U) % cell.vertex_indices.size()];
            if (!has_undirected_edge(neighbor, first_vertex, second_vertex)) {
                ++result.adjacency_edge_mismatch_count;
            }
        }

        for (const auto vertex_index : cell.vertex_indices) {
            if (static_cast<std::size_t>(vertex_index) >= mesh.vertex_count()) {
                ++result.invalid_vertex_index_count;
            }
        }
    }

    if (mesh.cell_count() == 0) {
        result.min_cell_area_m2 = 0.0;
        result.min_edge_length_m = 0.0;
    }
    result.relative_area_error =
        std::abs(result.total_area_m2 - result.expected_area_m2) /
        result.expected_area_m2;
    return result;
}

}  // namespace planetsim
