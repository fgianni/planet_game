#include "sim/planet/mesh/mesh_diagnostics.hpp"

#include "sim/planet/mesh/icosphere.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace planetsim {
namespace {

[[nodiscard]] const CellEdgeGeometry* find_neighbor(const PlanetMesh& mesh, CellId cell,
                                                    CellId expected) noexcept {
    const auto edges = mesh.cell_edges(cell);
    const auto found = std::find_if(edges.begin(), edges.end(), [expected](const auto& edge) {
        return edge.neighbor == expected;
    });
    return found == edges.end() ? nullptr : &*found;
}

[[nodiscard]] std::size_t shared_corner_count(const PlanetMesh& mesh, CellId first,
                                              CellId second) noexcept {
    std::size_t count = 0;
    for (const auto first_corner : mesh.cell_corners(first)) {
        const auto second_corners = mesh.cell_corners(second);
        if (std::find(second_corners.begin(), second_corners.end(), first_corner) !=
            second_corners.end()) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] double basis_error(const CellGeometry& cell) noexcept {
    return std::max(
        {std::abs(length(cell.center_unit) - 1.0), std::abs(length(cell.east_unit) - 1.0),
         std::abs(length(cell.north_unit) - 1.0), std::abs(dot(cell.center_unit, cell.east_unit)),
         std::abs(dot(cell.center_unit, cell.north_unit)),
         std::abs(dot(cell.east_unit, cell.north_unit)),
         std::abs(dot(cross(cell.east_unit, cell.north_unit), cell.center_unit) - 1.0)});
}

}  // namespace

bool MeshDiagnostics::topology_valid() const noexcept {
    return counts_match_expected && pentagon_count == 12U &&
           pentagon_count + hexagon_count == cell_count && invalid_neighbor_count == 0 &&
           non_reciprocal_neighbor_count == 0 && invalid_edge_reference_count == 0 &&
           invalid_corner_index_count == 0 && shared_corner_mismatch_count == 0 &&
           edge_reference_count_mismatch == 0 && invalid_block_count == 0 &&
           non_finite_geometry_count == 0;
}

MeshDiagnostics analyze_mesh(const PlanetMesh& mesh) {
    MeshDiagnostics result;
    result.subdivision = mesh.subdivision();
    result.cell_count = mesh.cell_count();
    result.corner_count = mesh.corner_count();
    result.edge_count = mesh.edge_count();
    result.directed_edge_count = mesh.directed_edge_count();
    result.block_count = mesh.blocks().size();
    result.allocated_bytes = mesh.allocated_bytes();
    result.expected_area_m2 = 4.0 * std::numbers::pi_v<double> * mesh.radius_m() * mesh.radius_m();
    result.min_cell_area_m2 = std::numeric_limits<double>::infinity();
    result.min_edge_length_m = std::numeric_limits<double>::infinity();
    result.min_centroid_distance_m = std::numeric_limits<double>::infinity();
    result.counts_match_expected =
        result.cell_count == expected_icosphere_cell_count(mesh.subdivision()) &&
        result.corner_count == expected_icosphere_corner_count(mesh.subdivision()) &&
        result.edge_count == expected_icosphere_edge_count(mesh.subdivision()) &&
        result.directed_edge_count == expected_icosphere_directed_edge_count(mesh.subdivision());

    std::vector<std::size_t> edge_references(mesh.edge_count(), 0U);
    double area_compensation = 0.0;
    double neighbor_delta_sum = 0.0;
    double naive_neighbor_delta_sum = 0.0;

    for (const auto& cell : mesh.cells()) {
        const double corrected_area = cell.area_m2 - area_compensation;
        const double next_total = result.total_area_m2 + corrected_area;
        area_compensation = (next_total - result.total_area_m2) - corrected_area;
        result.total_area_m2 = next_total;

        result.min_cell_area_m2 = std::min(result.min_cell_area_m2, cell.area_m2);
        result.max_cell_area_m2 = std::max(result.max_cell_area_m2, cell.area_m2);
        result.max_center_norm_error =
            std::max(result.max_center_norm_error, std::abs(length(cell.center_unit) - 1.0));
        result.max_tangent_basis_error =
            std::max(result.max_tangent_basis_error, basis_error(cell));

        if (cell.is_pentagon()) {
            ++result.pentagon_count;
        } else if (cell.edge_count == 6U) {
            ++result.hexagon_count;
        } else {
            ++result.invalid_neighbor_count;
        }

        if (!is_finite(cell.center_unit) || !is_finite(cell.east_unit) ||
            !is_finite(cell.north_unit) || !std::isfinite(cell.area_m2) || !(cell.area_m2 > 0.0)) {
            ++result.non_finite_geometry_count;
        }

        const auto adjacency = mesh.cell_edges(cell.id);
        const auto corners = mesh.cell_corners(cell.id);
        if (adjacency.size() != cell.edge_count || corners.size() != cell.edge_count) {
            ++result.invalid_neighbor_count;
        }

        for (std::size_t local_edge = 0; local_edge < adjacency.size(); ++local_edge) {
            const auto& cell_edge = adjacency[local_edge];
            if (!cell_edge.neighbor.is_valid() ||
                cell_edge.neighbor.to_index() >= mesh.cell_count() ||
                cell_edge.neighbor == cell.id) {
                ++result.invalid_neighbor_count;
                continue;
            }
            if (!cell_edge.edge.is_valid() || cell_edge.edge.to_index() >= mesh.edge_count()) {
                ++result.invalid_edge_reference_count;
                continue;
            }
            ++edge_references[cell_edge.edge.to_index()];

            const auto* reciprocal = find_neighbor(mesh, cell_edge.neighbor, cell.id);
            if (reciprocal == nullptr || reciprocal->edge != cell_edge.edge) {
                ++result.non_reciprocal_neighbor_count;
            }
            if (shared_corner_count(mesh, cell.id, cell_edge.neighbor) != 2U) {
                ++result.shared_corner_mismatch_count;
            }

            if (corners[local_edge] >= mesh.corner_count()) {
                ++result.invalid_corner_index_count;
            }
            if (!std::isfinite(cell_edge.outward_normal_east) ||
                !std::isfinite(cell_edge.outward_normal_north)) {
                ++result.non_finite_geometry_count;
            }
            const double normal_length =
                std::hypot(cell_edge.outward_normal_east, cell_edge.outward_normal_north);
            if (std::abs(normal_length - 1.0) > 2.0e-6) {
                ++result.non_finite_geometry_count;
            }

            neighbor_delta_sum += std::abs(static_cast<double>(cell.id.value()) -
                                           static_cast<double>(cell_edge.neighbor.value()));
            const auto neighbor_source = mesh.cell(cell_edge.neighbor).source_vertex_index;
            naive_neighbor_delta_sum += std::abs(static_cast<double>(cell.source_vertex_index) -
                                                 static_cast<double>(neighbor_source));
        }
    }

    for (const auto& edge : mesh.edges()) {
        result.min_edge_length_m = std::min(result.min_edge_length_m, edge.length_m);
        result.max_edge_length_m = std::max(result.max_edge_length_m, edge.length_m);
        result.min_centroid_distance_m =
            std::min(result.min_centroid_distance_m, edge.centroid_distance_m);
        result.max_centroid_distance_m =
            std::max(result.max_centroid_distance_m, edge.centroid_distance_m);
        if (!std::isfinite(edge.length_m) || !(edge.length_m > 0.0) ||
            !std::isfinite(edge.centroid_distance_m) || !(edge.centroid_distance_m > 0.0) ||
            !edge.first_cell.is_valid() || !edge.second_cell.is_valid() ||
            edge.first_cell.to_index() >= mesh.cell_count() ||
            edge.second_cell.to_index() >= mesh.cell_count() ||
            edge.first_cell == edge.second_cell) {
            ++result.non_finite_geometry_count;
        }
    }

    for (const auto references : edge_references) {
        if (references != 2U) {
            ++result.edge_reference_count_mismatch;
        }
    }

    std::size_t expected_block_begin = 0;
    for (const auto& block : mesh.blocks()) {
        if (block.begin != expected_block_begin || block.begin >= block.end ||
            block.end > mesh.cell_count() ||
            static_cast<std::size_t>(block.end - block.begin) > deterministic_cell_block_size) {
            ++result.invalid_block_count;
        }
        expected_block_begin = block.end;
    }
    if (expected_block_begin != mesh.cell_count()) {
        ++result.invalid_block_count;
    }

    if (mesh.cell_count() == 0) {
        result.min_cell_area_m2 = 0.0;
    }
    if (mesh.edge_count() == 0) {
        result.min_edge_length_m = 0.0;
        result.min_centroid_distance_m = 0.0;
    }
    if (mesh.directed_edge_count() > 0) {
        const double count = static_cast<double>(mesh.directed_edge_count());
        result.mean_neighbor_index_delta = neighbor_delta_sum / count;
        result.naive_mean_neighbor_index_delta = naive_neighbor_delta_sum / count;
    }
    result.relative_area_error =
        std::abs(result.total_area_m2 - result.expected_area_m2) / result.expected_area_m2;
    return result;
}

}  // namespace planetsim
