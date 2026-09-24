#include "sim/planet/mesh/icosphere.hpp"

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

using Face = std::array<VertexIndex, 3>;

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

[[nodiscard]] std::uint64_t edge_key(VertexIndex first, VertexIndex second) noexcept {
    const auto low = std::min(first, second);
    const auto high = std::max(first, second);
    return (static_cast<std::uint64_t>(low) << 32U) | static_cast<std::uint64_t>(high);
}

[[nodiscard]] double spherical_triangle_area_unit(const Vec3d& first,
                                                  const Vec3d& second,
                                                  const Vec3d& third) noexcept {
    const double numerator = std::abs(dot(first, cross(second, third)));
    const double denominator =
        1.0 + dot(first, second) + dot(second, third) + dot(third, first);
    return 2.0 * std::atan2(numerator, denominator);
}

[[nodiscard]] double great_circle_angle(const Vec3d& first, const Vec3d& second) noexcept {
    return std::acos(std::clamp(dot(first, second), -1.0, 1.0));
}

[[nodiscard]] std::vector<Vec3d> base_vertices() {
    constexpr double golden_ratio = 1.6180339887498948482;
    std::vector<Vec3d> vertices{
        {-1.0, golden_ratio, 0.0},
        {1.0, golden_ratio, 0.0},
        {-1.0, -golden_ratio, 0.0},
        {1.0, -golden_ratio, 0.0},
        {0.0, -1.0, golden_ratio},
        {0.0, 1.0, golden_ratio},
        {0.0, -1.0, -golden_ratio},
        {0.0, 1.0, -golden_ratio},
        {golden_ratio, 0.0, -1.0},
        {golden_ratio, 0.0, 1.0},
        {-golden_ratio, 0.0, -1.0},
        {-golden_ratio, 0.0, 1.0},
    };
    for (auto& vertex : vertices) {
        vertex = normalized(vertex);
    }
    return vertices;
}

[[nodiscard]] std::vector<Face> base_faces() {
    return {
        {0, 11, 5},
        {0, 5, 1},
        {0, 1, 7},
        {0, 7, 10},
        {0, 10, 11},
        {1, 5, 9},
        {5, 11, 4},
        {11, 10, 2},
        {10, 7, 6},
        {7, 1, 8},
        {3, 9, 4},
        {3, 4, 2},
        {3, 2, 6},
        {3, 6, 8},
        {3, 8, 9},
        {4, 9, 5},
        {2, 4, 11},
        {6, 2, 10},
        {8, 6, 7},
        {9, 8, 1},
    };
}

struct EdgeRecord {
    CellId first_cell;
    std::size_t first_edge = 0;
    bool paired = false;
};

}  // namespace

std::size_t expected_icosphere_cell_count(std::uint32_t subdivision) {
    const std::size_t scale = subdivision_scale(subdivision);
    if (scale > std::numeric_limits<std::size_t>::max() / 20U) {
        throw std::overflow_error("icosphere cell count overflow");
    }
    return 20U * scale;
}

std::size_t expected_icosphere_vertex_count(std::uint32_t subdivision) {
    const std::size_t scale = subdivision_scale(subdivision);
    if (scale > (std::numeric_limits<std::size_t>::max() - 2U) / 10U) {
        throw std::overflow_error("icosphere vertex count overflow");
    }
    return 10U * scale + 2U;
}

PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m) {
    if (!(radius_m > 0.0) || !std::isfinite(radius_m)) {
        throw std::invalid_argument("icosphere radius must be finite and positive");
    }

    const std::size_t expected_cells = expected_icosphere_cell_count(subdivision);
    const std::size_t expected_vertices = expected_icosphere_vertex_count(subdivision);
    const auto invalid_cell_value = static_cast<std::size_t>(CellId::invalid().value());
    const auto max_vertex_count =
        static_cast<std::size_t>(std::numeric_limits<VertexIndex>::max());
    if (expected_cells > invalid_cell_value || expected_vertices > max_vertex_count) {
        throw std::overflow_error("icosphere IDs exceed their 32-bit representation");
    }

    auto vertices = base_vertices();
    auto faces = base_faces();
    vertices.reserve(expected_vertices);
    faces.reserve(expected_cells);

    for (std::uint32_t level = 0; level < subdivision; ++level) {
        std::map<std::uint64_t, VertexIndex> midpoint_indices;
        std::vector<Face> subdivided_faces;
        subdivided_faces.reserve(faces.size() * 4U);

        const auto midpoint = [&](VertexIndex first, VertexIndex second) {
            const auto key = edge_key(first, second);
            const auto found = midpoint_indices.find(key);
            if (found != midpoint_indices.end()) {
                return found->second;
            }
            if (vertices.size() >= max_vertex_count) {
                throw std::overflow_error("icosphere vertex ID overflow");
            }
            const auto index = static_cast<VertexIndex>(vertices.size());
            vertices.push_back(normalized(vertices[first] + vertices[second]));
            midpoint_indices.emplace(key, index);
            return index;
        };

        for (const auto& face : faces) {
            const VertexIndex edge_01 = midpoint(face[0], face[1]);
            const VertexIndex edge_12 = midpoint(face[1], face[2]);
            const VertexIndex edge_20 = midpoint(face[2], face[0]);
            subdivided_faces.push_back({face[0], edge_01, edge_20});
            subdivided_faces.push_back({face[1], edge_12, edge_01});
            subdivided_faces.push_back({face[2], edge_20, edge_12});
            subdivided_faces.push_back({edge_01, edge_12, edge_20});
        }
        faces = std::move(subdivided_faces);
    }

    if (faces.size() != expected_cells || vertices.size() != expected_vertices) {
        throw std::logic_error("icosphere subdivision produced unexpected counts");
    }

    std::vector<CellGeometry> cells;
    cells.reserve(faces.size());
    for (std::size_t cell_index = 0; cell_index < faces.size(); ++cell_index) {
        const auto& face = faces[cell_index];
        const Vec3d& first = vertices[face[0]];
        const Vec3d& second = vertices[face[1]];
        const Vec3d& third = vertices[face[2]];

        CellGeometry cell;
        cell.id = CellId{static_cast<CellId::value_type>(cell_index)};
        cell.center_unit = normalized(first + second + third);
        cell.area_m2 =
            spherical_triangle_area_unit(first, second, third) * radius_m * radius_m;
        cell.vertex_indices = face;
        for (std::size_t edge = 0; edge < face.size(); ++edge) {
            const Vec3d& edge_first = vertices[face[edge]];
            const Vec3d& edge_second = vertices[face[(edge + 1U) % face.size()]];
            cell.edge_length_m[edge] =
                great_circle_angle(edge_first, edge_second) * radius_m;
        }
        cells.push_back(cell);
    }

    std::map<std::uint64_t, EdgeRecord> edge_records;
    for (std::size_t cell_index = 0; cell_index < cells.size(); ++cell_index) {
        auto& cell = cells[cell_index];
        for (std::size_t edge = 0; edge < cell.vertex_indices.size(); ++edge) {
            const auto first_vertex = cell.vertex_indices[edge];
            const auto second_vertex =
                cell.vertex_indices[(edge + 1U) % cell.vertex_indices.size()];
            const auto key = edge_key(first_vertex, second_vertex);
            const auto [record_iterator, inserted] = edge_records.try_emplace(
                key, EdgeRecord{cell.id, edge, false});
            if (inserted) {
                continue;
            }

            auto& record = record_iterator->second;
            if (record.paired) {
                throw std::logic_error("icosphere contains a non-manifold edge");
            }
            cells[record.first_cell.to_index()].neighbors[record.first_edge] = cell.id;
            cell.neighbors[edge] = record.first_cell;
            record.paired = true;
        }
    }

    for (const auto& [key, record] : edge_records) {
        static_cast<void>(key);
        if (!record.paired) {
            throw std::logic_error("icosphere contains an open boundary edge");
        }
    }

    return PlanetMesh{subdivision, radius_m, std::move(vertices), std::move(cells)};
}

}  // namespace planetsim
