#include "sim/planet/mesh/icosphere.hpp"

#include "sim/planet/coordinates/local_tangent_basis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace planetsim {
namespace {

using PrimalFace = std::array<SourceVertexIndex, 3>;

struct OrderedCorner {
    CornerIndex index = 0;
    double angle_rad = 0.0;
};

struct EdgeRecord {
    EdgeId id;
    std::uint8_t reference_count = 0;
};

[[nodiscard]] std::size_t subdivision_scale(std::uint32_t subdivision) {
    std::size_t scale = 1;
    for (std::uint32_t level = 0; level < subdivision; ++level) {
        if (scale > std::numeric_limits<std::size_t>::max() / 4U) {
            throw std::overflow_error("icosphere subdivision count overflow");
        }
        scale *= 4U;
    }
    return scale;
}

[[nodiscard]] std::uint64_t index_pair_key(std::uint32_t first, std::uint32_t second) noexcept {
    const auto low = std::min(first, second);
    const auto high = std::max(first, second);
    return (static_cast<std::uint64_t>(low) << 32U) | static_cast<std::uint64_t>(high);
}

[[nodiscard]] double spherical_triangle_area_unit(const Vec3d& first, const Vec3d& second,
                                                  const Vec3d& third) noexcept {
    const double numerator = std::abs(dot(first, cross(second, third)));
    const double denominator = 1.0 + dot(first, second) + dot(second, third) + dot(third, first);
    return 2.0 * std::atan2(numerator, denominator);
}

[[nodiscard]] double great_circle_angle(const Vec3d& first, const Vec3d& second) noexcept {
    return std::acos(std::clamp(dot(first, second), -1.0, 1.0));
}

[[nodiscard]] Vec3d outward_edge_normal_at_cell(const Vec3d& cell_center, const Vec3d& first_corner,
                                                const Vec3d& second_corner) {
    Vec3d edge_conormal = normalized(cross(first_corner, second_corner));
    if (dot(edge_conormal, cell_center) > 0.0) {
        edge_conormal = edge_conormal * -1.0;
    }
    return normalized(edge_conormal - cell_center * dot(edge_conormal, cell_center));
}

[[nodiscard]] std::vector<Vec3d> base_vertices() {
    constexpr double golden_ratio = 1.6180339887498948482;
    std::vector<Vec3d> vertices{
        {-1.0, golden_ratio, 0.0},  {1.0, golden_ratio, 0.0},   {-1.0, -golden_ratio, 0.0},
        {1.0, -golden_ratio, 0.0},  {0.0, -1.0, golden_ratio},  {0.0, 1.0, golden_ratio},
        {0.0, -1.0, -golden_ratio}, {0.0, 1.0, -golden_ratio},  {golden_ratio, 0.0, -1.0},
        {golden_ratio, 0.0, 1.0},   {-golden_ratio, 0.0, -1.0}, {-golden_ratio, 0.0, 1.0},
    };
    for (auto& vertex : vertices) {
        vertex = normalized(vertex);
    }
    return vertices;
}

[[nodiscard]] std::vector<PrimalFace> base_faces() {
    return {
        {0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
        {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1},
    };
}

[[nodiscard]] SourceVertexIndex
common_other_vertex(const PrimalFace& first, const PrimalFace& second, SourceVertexIndex center) {
    for (const auto first_vertex : first) {
        if (first_vertex == center) {
            continue;
        }
        if (std::find(second.begin(), second.end(), first_vertex) != second.end()) {
            return first_vertex;
        }
    }
    throw std::logic_error("consecutive dual corners do not share a primal edge");
}

[[nodiscard]] std::vector<CellBlock> make_cell_blocks(std::size_t cell_count) {
    std::vector<CellBlock> blocks;
    blocks.reserve((cell_count + deterministic_cell_block_size - 1U) /
                   deterministic_cell_block_size);
    for (std::size_t begin = 0; begin < cell_count; begin += deterministic_cell_block_size) {
        const std::size_t end = std::min(begin + deterministic_cell_block_size, cell_count);
        blocks.push_back({static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end)});
    }
    return blocks;
}

}  // namespace

std::size_t expected_icosphere_cell_count(std::uint32_t subdivision) {
    const std::size_t scale = subdivision_scale(subdivision);
    if (scale > (std::numeric_limits<std::size_t>::max() - 2U) / 10U) {
        throw std::overflow_error("icosphere cell count overflow");
    }
    return 10U * scale + 2U;
}

std::size_t expected_icosphere_corner_count(std::uint32_t subdivision) {
    const std::size_t scale = subdivision_scale(subdivision);
    if (scale > std::numeric_limits<std::size_t>::max() / 20U) {
        throw std::overflow_error("icosphere corner count overflow");
    }
    return 20U * scale;
}

std::size_t expected_icosphere_edge_count(std::uint32_t subdivision) {
    const std::size_t scale = subdivision_scale(subdivision);
    if (scale > std::numeric_limits<std::size_t>::max() / 30U) {
        throw std::overflow_error("icosphere edge count overflow");
    }
    return 30U * scale;
}

std::size_t expected_icosphere_directed_edge_count(std::uint32_t subdivision) {
    const std::size_t edge_count = expected_icosphere_edge_count(subdivision);
    if (edge_count > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::overflow_error("icosphere directed edge count overflow");
    }
    return 2U * edge_count;
}

PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m) {
    if (!(radius_m > 0.0) || !std::isfinite(radius_m)) {
        throw std::invalid_argument("icosphere radius must be finite and positive");
    }

    const std::size_t expected_cells = expected_icosphere_cell_count(subdivision);
    const std::size_t expected_corners = expected_icosphere_corner_count(subdivision);
    const std::size_t expected_edges = expected_icosphere_edge_count(subdivision);
    const std::size_t expected_directed_edges = expected_icosphere_directed_edge_count(subdivision);
    const auto invalid_cell_value = static_cast<std::size_t>(CellId::invalid().value());
    const auto max_source_index =
        static_cast<std::size_t>(std::numeric_limits<SourceVertexIndex>::max());
    const auto max_corner_index = static_cast<std::size_t>(std::numeric_limits<CornerIndex>::max());
    const auto max_edge_index = static_cast<std::size_t>(EdgeId::invalid().value());
    if (expected_cells > invalid_cell_value || expected_cells > max_source_index ||
        expected_corners > max_corner_index || expected_edges > max_edge_index ||
        expected_directed_edges > max_source_index) {
        throw std::overflow_error("icosphere IDs exceed their 32-bit representation");
    }

    auto source_vertices = base_vertices();
    auto faces = base_faces();
    source_vertices.reserve(expected_cells);
    faces.reserve(expected_corners);

    for (std::uint32_t level = 0; level < subdivision; ++level) {
        std::map<std::uint64_t, SourceVertexIndex> midpoint_indices;
        std::vector<PrimalFace> subdivided_faces;
        subdivided_faces.reserve(faces.size() * 4U);

        const auto midpoint = [&](SourceVertexIndex first, SourceVertexIndex second) {
            const auto key = index_pair_key(first, second);
            const auto found = midpoint_indices.find(key);
            if (found != midpoint_indices.end()) {
                return found->second;
            }
            if (source_vertices.size() >= max_source_index) {
                throw std::overflow_error("icosphere source vertex ID overflow");
            }
            const auto index = static_cast<SourceVertexIndex>(source_vertices.size());
            source_vertices.push_back(normalized(source_vertices[first] + source_vertices[second]));
            midpoint_indices.emplace(key, index);
            return index;
        };

        for (const auto& face : faces) {
            const SourceVertexIndex edge_01 = midpoint(face[0], face[1]);
            const SourceVertexIndex edge_12 = midpoint(face[1], face[2]);
            const SourceVertexIndex edge_20 = midpoint(face[2], face[0]);
            subdivided_faces.push_back({face[0], edge_01, edge_20});
            subdivided_faces.push_back({face[1], edge_12, edge_01});
            subdivided_faces.push_back({face[2], edge_20, edge_12});
            subdivided_faces.push_back({edge_01, edge_12, edge_20});
        }
        faces = std::move(subdivided_faces);
    }

    if (faces.size() != expected_corners || source_vertices.size() != expected_cells) {
        throw std::logic_error("icosphere subdivision produced unexpected counts");
    }

    std::vector<CellId> source_to_cell(source_vertices.size(), CellId::invalid());
    std::vector<SourceVertexIndex> cell_to_source;
    cell_to_source.reserve(expected_cells);
    for (const auto& face : faces) {
        for (const auto source_index : face) {
            if (!source_to_cell[source_index].is_valid()) {
                const auto cell_value = static_cast<CellId::value_type>(cell_to_source.size());
                source_to_cell[source_index] = CellId{cell_value};
                cell_to_source.push_back(source_index);
            }
        }
    }
    if (cell_to_source.size() != expected_cells) {
        throw std::logic_error("recursive face ordering omitted a dual cell");
    }

    std::vector<Vec3d> corners_unit;
    corners_unit.reserve(expected_corners);
    std::vector<std::vector<CornerIndex>> incident_corners(source_vertices.size());
    for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
        const auto& face = faces[face_index];
        corners_unit.push_back(normalized(source_vertices[face[0]] + source_vertices[face[1]] +
                                          source_vertices[face[2]]));
        const auto corner_index = static_cast<CornerIndex>(face_index);
        for (const auto source_index : face) {
            incident_corners[source_index].push_back(corner_index);
        }
    }

    std::vector<CellGeometry> cells;
    cells.reserve(expected_cells);
    for (std::size_t cell_index = 0; cell_index < expected_cells; ++cell_index) {
        const SourceVertexIndex source_index = cell_to_source[cell_index];
        const auto basis = make_local_tangent_basis(source_vertices[source_index]);
        CellGeometry cell;
        cell.center_unit = basis.up_unit;
        cell.east_unit = basis.east_unit;
        cell.north_unit = basis.north_unit;
        cell.id = CellId{static_cast<CellId::value_type>(cell_index)};
        cell.source_vertex_index = source_index;
        cells.push_back(cell);
    }

    std::vector<CellEdgeGeometry> cell_edges;
    cell_edges.reserve(expected_directed_edges);
    std::vector<CornerIndex> cell_corner_indices;
    cell_corner_indices.reserve(expected_directed_edges);
    std::vector<EdgeGeometry> edges;
    edges.reserve(expected_edges);
    std::map<std::uint64_t, EdgeRecord> edge_records;

    for (auto& cell : cells) {
        const auto& incident = incident_corners[cell.source_vertex_index];
        if (incident.size() != 5U && incident.size() != 6U) {
            throw std::logic_error("dual cell is neither a pentagon nor a hexagon");
        }

        std::vector<OrderedCorner> ordered;
        ordered.reserve(incident.size());
        for (const CornerIndex corner_index : incident) {
            const Vec3d& corner = corners_unit[corner_index];
            const Vec3d tangent = corner - cell.center_unit * dot(corner, cell.center_unit);
            ordered.push_back({corner_index, std::atan2(dot(tangent, cell.north_unit),
                                                        dot(tangent, cell.east_unit))});
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            if (left.angle_rad == right.angle_rad) {
                return left.index < right.index;
            }
            return left.angle_rad < right.angle_rad;
        });

        cell.edge_offset = static_cast<std::uint32_t>(cell_edges.size());
        cell.edge_count = static_cast<std::uint8_t>(ordered.size());
        double area_unit = 0.0;
        double area_compensation = 0.0;

        for (std::size_t local_edge = 0; local_edge < ordered.size(); ++local_edge) {
            const CornerIndex first_corner = ordered[local_edge].index;
            const CornerIndex second_corner = ordered[(local_edge + 1U) % ordered.size()].index;
            cell_corner_indices.push_back(first_corner);

            const double triangle_area = spherical_triangle_area_unit(
                cell.center_unit, corners_unit[first_corner], corners_unit[second_corner]);
            const double corrected_area = triangle_area - area_compensation;
            const double next_area = area_unit + corrected_area;
            area_compensation = (next_area - area_unit) - corrected_area;
            area_unit = next_area;

            const SourceVertexIndex neighbor_source = common_other_vertex(
                faces[first_corner], faces[second_corner], cell.source_vertex_index);
            const CellId neighbor = source_to_cell[neighbor_source];
            if (!neighbor.is_valid() || neighbor == cell.id) {
                throw std::logic_error("dual topology produced an invalid neighbor");
            }

            const auto key = index_pair_key(cell.id.value(), neighbor.value());
            const double edge_length_m =
                great_circle_angle(corners_unit[first_corner], corners_unit[second_corner]) *
                radius_m;
            const double centroid_distance_m =
                great_circle_angle(cell.center_unit, cells[neighbor.to_index()].center_unit) *
                radius_m;
            auto [record_iterator, inserted] = edge_records.try_emplace(key);
            if (inserted) {
                if (edges.size() >= max_edge_index) {
                    throw std::overflow_error("dual edge ID overflow");
                }
                const EdgeId edge_id{static_cast<EdgeId::value_type>(edges.size())};
                record_iterator->second = {edge_id, 1U};
                edges.push_back({std::min(cell.id, neighbor), std::max(cell.id, neighbor),
                                 edge_length_m, centroid_distance_m});
            } else {
                auto& record = record_iterator->second;
                if (record.reference_count != 1U) {
                    throw std::logic_error("dual mesh contains a non-manifold edge");
                }
                ++record.reference_count;
                const auto& existing = edges[record.id.to_index()];
                if (existing.length_m != edge_length_m ||
                    existing.centroid_distance_m != centroid_distance_m) {
                    throw std::logic_error("dual edge geometry is not reciprocal");
                }
            }

            const Vec3d outward = outward_edge_normal_at_cell(
                cell.center_unit, corners_unit[first_corner], corners_unit[second_corner]);
            const auto edge_id = record_iterator->second.id;
            cell_edges.push_back({neighbor, edge_id,
                                  static_cast<float>(dot(outward, cell.east_unit)),
                                  static_cast<float>(dot(outward, cell.north_unit))});
        }
        cell.area_m2 = area_unit * radius_m * radius_m;
    }

    if (cell_edges.size() != expected_directed_edges ||
        cell_corner_indices.size() != expected_directed_edges || edges.size() != expected_edges) {
        throw std::logic_error("dual mesh produced unexpected topology counts");
    }
    for (const auto& [key, record] : edge_records) {
        static_cast<void>(key);
        if (record.reference_count != 2U) {
            throw std::logic_error("dual mesh contains an open boundary edge");
        }
    }

    return PlanetMesh{subdivision,
                      radius_m,
                      std::move(cells),
                      std::move(cell_edges),
                      std::move(edges),
                      std::move(corners_unit),
                      std::move(cell_corner_indices),
                      make_cell_blocks(expected_cells)};
}

}  // namespace planetsim
