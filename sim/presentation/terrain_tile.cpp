#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace planetsim::presentation {
namespace {

struct FaceFrame {
    Vec3d normal;
    Vec3d u_axis;
    Vec3d v_axis;
};

constexpr std::array<FaceFrame, 6> faces{{
    {{1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}},
    {{-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}},
    {{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
    {{0.0, 0.0, -1.0}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
}};

// tan(u pi/4), exact at the face's edges so faces meet on the same points.
[[nodiscard]] double warp(double u) noexcept {
    if (u == 1.0) return 1.0;
    if (u == -1.0) return -1.0;
    return std::tan(u * std::numbers::pi / 4.0);
}

// Face coordinate of grid line i of a tile: dyadic, so it is the same
// number in every tile and level that shares the line.
[[nodiscard]] double face_coordinate(std::uint32_t tile, std::uint8_t level, std::int64_t i) noexcept {
    const double lines = static_cast<double>(tile_grid) * std::ldexp(1.0, level);
    return -1.0 + 2.0 * (static_cast<double>(tile) * tile_grid + static_cast<double>(i)) / lines;
}

struct FanTriangle {
    CellId cell;
    CornerIndex first = 0;
    CornerIndex second = 0;
};

// The cell-mesh fan triangle (centre, corner, next corner) a direction is in.
[[nodiscard]] FanTriangle fan_triangle(const PlanetMesh& mesh, const Vec3d& direction, CellId cell) {
    const Vec3d center = mesh.cell(cell).center_unit;
    const auto corners = mesh.cell_corners(cell);
    std::size_t best = 0;
    double best_margin = -1e300;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const Vec3d a = mesh.corners_unit()[corners[k]];
        const Vec3d b = mesh.corners_unit()[corners[(k + 1U) % corners.size()]];
        const double wc = dot(direction, cross(a, b));
        const double wa = dot(direction, cross(b, center));
        const double wb = dot(direction, cross(center, a));
        const double total = wc + wa + wb;
        const double margin = std::min({wc, wa, wb}) / total;
        if (margin > best_margin) {
            best_margin = margin;
            best = k;
        }
    }
    return {cell, corners[best], corners[(best + 1U) % corners.size()]};
}

void append(std::vector<float>& values, double a, double b) {
    values.push_back(static_cast<float>(a));
    values.push_back(static_cast<float>(b));
}

void append(std::vector<float>& values, const Vec3d& v) {
    values.push_back(static_cast<float>(v.x));
    values.push_back(static_cast<float>(v.y));
    values.push_back(static_cast<float>(v.z));
}

}  // namespace

std::array<TileKey, 4> tile_children(const TileKey& key) {
    const auto level = static_cast<std::uint8_t>(key.level + 1U);
    return {{{key.face, level, 2U * key.x, 2U * key.y},
             {key.face, level, 2U * key.x + 1U, 2U * key.y},
             {key.face, level, 2U * key.x, 2U * key.y + 1U},
             {key.face, level, 2U * key.x + 1U, 2U * key.y + 1U}}};
}

TileKey tile_parent(const TileKey& key) {
    if (key.level == 0U) return key;
    return {key.face, static_cast<std::uint8_t>(key.level - 1U), key.x / 2U, key.y / 2U};
}

Vec3d tile_face_direction(std::uint8_t face, double u, double v) {
    const FaceFrame& frame = faces[face];
    return normalized(frame.normal + frame.u_axis * warp(u) + frame.v_axis * warp(v));
}

Vec3d tile_center_direction(const TileKey& key) {
    const double u = face_coordinate(key.x, key.level, tile_grid / 2U);
    const double v = face_coordinate(key.y, key.level, tile_grid / 2U);
    return tile_face_direction(key.face, u, v);
}

double tile_angular_size(const TileKey& key) {
    return std::numbers::pi / 2.0 / std::ldexp(1.0, key.level);
}

TileGeometry make_terrain_tile(const TileKey& key, const TerrainRelief& relief, const TileDrawing& drawing) {
    const PlanetMesh& mesh = relief.mesh();
    const std::size_t cell_count = mesh.cell_count();
    constexpr std::int64_t lines = tile_grid + 1;
    constexpr std::int64_t ring = lines + 2; // one more line on every side, for normals
    const double exaggeration = drawing.relief_exaggeration / drawing.radius_m;

    struct Point {
        Vec3d direction;
        Vec3d position; // planet-centred, radii
        double height_m = 0.0;
        double rank = 0.0;
    };
    std::vector<Point> points(static_cast<std::size_t>(ring * ring));
    // Tiles too coarse to show sub-cell relief (fewer than five vertices
    // across a cell) draw the filtered surface (§4.9), which needs none of
    // the cells' rank tables.
    const bool filtered = tile_angular_size(key) / static_cast<double>(tile_grid) >
                          tile_filtered_cells * relief.cell_spacing_rad();
    CellId hint = relief.nearest_cell(tile_center_direction(key), CellId{0});
    for (std::int64_t j = -1; j <= lines; ++j) {
        for (std::int64_t i = -1; i <= lines; ++i) {
            const double u = face_coordinate(key.x, key.level, i);
            const double v = face_coordinate(key.y, key.level, j);
            Point& point = points[static_cast<std::size_t>((j + 1) * ring + (i + 1))];
            point.direction = tile_face_direction(key.face, u, v);
            const ReliefSample sample = filtered ? relief.smooth_sample(point.direction, hint)
                                                 : relief.sample(point.direction, hint);
            hint = sample.cell;
            point.height_m = sample.elevation_m - drawing.sea_level_m;
            point.rank = sample.rank;
            point.position = point.direction * (1.0 + exaggeration * std::max(0.0, point.height_m));
        }
    }
    const auto at = [&](std::int64_t i, std::int64_t j) -> const Point& {
        return points[static_cast<std::size_t>((j + 1) * ring + (i + 1))];
    };
    const auto normal_at = [&](std::int64_t i, std::int64_t j) {
        Vec3d n = cross(at(i + 1, j).position - at(i - 1, j).position,
                        at(i, j + 1).position - at(i, j - 1).position);
        if (dot(n, at(i, j).direction) < 0.0) n = n * -1.0;
        return normalized(n);
    };

    TileGeometry tile;
    tile.key = key;
    tile.origin = tile_center_direction(key);
    tile.min_radius = 1e300;
    tile.max_radius = 0.0;
    const auto texel = [&](std::size_t index) {
        return std::array<double, 2>{static_cast<double>(index % drawing.texture_width),
                                     static_cast<double>(index / drawing.texture_width)};
    };
    const FaceFrame& frame = faces[key.face];
    // Godot's front faces wind clockwise seen from outside: on a right-handed
    // face frame (u, v, outward) the quads below are emitted that way.
    const bool right_handed = dot(cross(frame.u_axis, frame.v_axis), frame.normal) > 0.0;

    struct Vertex {
        Vec3d position;
        Vec3d normal;
        Vec3d direction;
        double height_m;
        double rank;
    };
    const auto emit_triangle = [&](const std::array<Vertex, 3>& corners_in) {
        std::array<Vertex, 3> v = corners_in;
        if (!right_handed) std::swap(v[1], v[2]);
        const Vec3d centroid = normalized(v[0].direction + v[1].direction + v[2].direction);
        const CellId cell = relief.nearest_cell(centroid, hint);
        hint = cell;
        const FanTriangle fan = fan_triangle(mesh, centroid, cell);
        const Vec3d center = mesh.cell(fan.cell).center_unit;
        const Vec3d a = mesh.corners_unit()[fan.first];
        const Vec3d b = mesh.corners_unit()[fan.second];
        const auto t_cell = texel(fan.cell.to_index());
        const auto t_first = texel(cell_count + fan.first);
        const auto t_second = texel(cell_count + fan.second);
        for (const Vertex& vertex : v) {
            const double wc = dot(vertex.direction, cross(a, b));
            const double wa = dot(vertex.direction, cross(b, center));
            const double wb = dot(vertex.direction, cross(center, a));
            const double total = wc + wa + wb;
            append(tile.positions, vertex.position - tile.origin);
            append(tile.normals, vertex.normal);
            append(tile.cell_texel, t_cell[0], t_cell[1]);
            append(tile.corner_texel, t_first[0], t_first[1]);
            append(tile.custom0, t_second[0], t_second[1]);
            append(tile.custom0, wc / total, wa / total);
            // The styles' detail is a function of the planet-centred
            // position (§4.9); a tile's VERTEX is relative to its origin.
            append(tile.custom1, vertex.direction);
            tile.custom1.push_back(static_cast<float>(vertex.height_m));
            const double radius = length(vertex.position);
            tile.min_radius = std::min(tile.min_radius, radius);
            tile.max_radius = std::max(tile.max_radius, radius);
        }
    };
    const auto vertex_at = [&](std::int64_t i, std::int64_t j) {
        const Point& p = at(i, j);
        return Vertex{p.position, normal_at(i, j), p.direction, p.height_m, p.rank};
    };

    for (std::int64_t j = 0; j < tile_grid; ++j) {
        for (std::int64_t i = 0; i < tile_grid; ++i) {
            const Vertex v00 = vertex_at(i, j);
            const Vertex v10 = vertex_at(i + 1, j);
            const Vertex v11 = vertex_at(i + 1, j + 1);
            const Vertex v01 = vertex_at(i, j + 1);
            emit_triangle({v00, v11, v10});
            emit_triangle({v00, v01, v11});
        }
    }
    // Skirts hang below every edge, so a neighbour drawn at another level
    // never shows a gap between them.
    const double depth = tile_skirt_depth * tile_angular_size(key);
    const auto skirt = [&](std::int64_t i0, std::int64_t j0, std::int64_t i1, std::int64_t j1) {
        const Vertex top0 = vertex_at(i0, j0);
        const Vertex top1 = vertex_at(i1, j1);
        Vertex bottom0 = top0;
        Vertex bottom1 = top1;
        bottom0.position = top0.direction * (length(top0.position) - depth);
        bottom1.position = top1.direction * (length(top1.position) - depth);
        emit_triangle({top0, top1, bottom1});
        emit_triangle({top0, bottom1, bottom0});
    };
    for (std::int64_t k = 0; k < tile_grid; ++k) {
        skirt(k + 1, 0, k, 0);
        skirt(k, tile_grid, k + 1, tile_grid);
        skirt(0, k, 0, k + 1);
        skirt(tile_grid, k + 1, tile_grid, k);
    }
    return tile;
}

}  // namespace planetsim::presentation
