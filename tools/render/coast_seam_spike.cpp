// ADR-0030 read-only seam experiment. Samples are independently requested
// from the detailed presentation relief; no renderer or solver is changed.
// Build like coast_geometry_spike.cpp against the two static libraries.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
constexpr int edge_grid = 128;

std::vector<double> crossings(const std::vector<double>& h, int step) {
    std::vector<double> out;
    for (int j = 0; j + step < static_cast<int>(h.size()); j += step) {
        const double a = h[static_cast<std::size_t>(j)];
        const double b = h[static_cast<std::size_t>(j + step)];
        if ((a <= 0.0) != (b <= 0.0))
            out.push_back(j + step * a / (a - b));
    }
    return out;
}

struct Comparison {
    std::size_t fine = 0, coarse = 0;
    double max_shift_m = 0.0;
    std::size_t unmatched = 0;
};

struct Vertex { double u, j, h; }; // strip coordinates: 0 boundary, 1 interior

std::vector<Vertex> clip(const std::array<Vertex, 3>& triangle, bool water) {
    std::vector<Vertex> polygon;
    for (int k = 0; k < 3; ++k) {
        const auto& a = triangle[static_cast<std::size_t>(k)];
        const auto& b = triangle[static_cast<std::size_t>((k + 1) % 3)];
        const bool inside_a = water ? a.h <= 0.0 : a.h >= 0.0;
        const bool inside_b = water ? b.h <= 0.0 : b.h >= 0.0;
        if (inside_a) polygon.push_back(a);
        if (inside_a != inside_b) {
            const double t = a.h / (a.h - b.h);
            polygon.push_back({a.u + t * (b.u - a.u), a.j + t * (b.j - a.j), 0.0});
        }
    }
    return polygon;
}

struct Strip {
    std::size_t triangles = 0;
    std::size_t clipped_vertices = 0; // separate wet and dry, triangle-list layout
    std::vector<double> boundary_crossings;
    std::vector<int> boundary_intervals;
};

void add_triangle(Strip& strip, const std::array<Vertex, 3>& triangle) {
    const double area2 = (triangle[1].u - triangle[0].u) * (triangle[2].j - triangle[0].j) -
                         (triangle[1].j - triangle[0].j) * (triangle[2].u - triangle[0].u);
    if (area2 <= 0.0) throw std::runtime_error("degenerate or reversed seam triangle");
    ++strip.triangles;
    for (int k = 0; k < 3; ++k) {
        const auto& a = triangle[static_cast<std::size_t>(k)];
        const auto& b = triangle[static_cast<std::size_t>((k + 1) % 3)];
        if (a.u == 0.0 && b.u == 0.0)
            strip.boundary_intervals.push_back(static_cast<int>(std::min(a.j, b.j)));
    }
    for (const bool water : {true, false}) {
        const auto polygon = clip(triangle, water);
        if (polygon.size() < 3) continue;
        strip.clipped_vertices += 3 * (polygon.size() - 2);
        for (const auto& point : polygon)
            if (point.u == 0.0 && point.h == 0.0) strip.boundary_crossings.push_back(point.j);
    }
}

void unique_positions(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [](double a, double b) {
        return std::abs(a - b) < 1e-9;
    }), values.end());
}

Comparison compare(const std::vector<double>& samples, double length_m) {
    const auto fine = crossings(samples, 1);
    const auto coarse = crossings(samples, 2);
    Comparison out{fine.size(), coarse.size(), 0.0, 0};
    for (const double x : fine) {
        if (coarse.empty()) {
            ++out.unmatched;
            continue;
        }
        const double nearest = *std::min_element(coarse.begin(), coarse.end(), [x](double a, double b) {
            return std::abs(a - x) < std::abs(b - x);
        });
        out.max_shift_m = std::max(out.max_shift_m,
                                   std::abs(nearest - x) * length_m / (samples.size() - 1));
        if (std::abs(nearest - x) > 1.0) ++out.unmatched;
    }
    return out;
}

void run(std::uint32_t level) {
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = level;
    const PlanetRun planet(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const std::uint32_t side = 1U << level;
    double worst_shared_m = 0.0;
    Comparison chosen;
    TileKey chosen_key{};
    bool found = false;
    std::size_t chosen_odd_flips = 0;
    std::size_t chosen_constraint_vertices = 0;
    double constrained_crossing_error_m = 0.0;
    std::size_t strip_extra_vertices = 0;
    std::size_t strip_triangles = 0;
    std::size_t strip_boundary_crossings = 0;
    std::size_t strip_inverted_triangles = 0;
    double strip_max_edge_gap_m = 0.0;
    // Fine tile on the left of a dyadic mixed-level edge; the coarse tile
    // to its right independently samples every second point along this edge.
    for (std::uint32_t y = 0; y < side && !found; ++y) {
        for (std::uint32_t x = 1; x + 1 < side; x += 2) {
            std::vector<double> fine(static_cast<std::size_t>(edge_grid + 1));
            const TileKey left{0, static_cast<std::uint8_t>(level), x, y};
            const TileKey right_coarse{0, static_cast<std::uint8_t>(level - 1U), (x + 1U) / 2U, y / 2U};
            for (int j = 0; j <= edge_grid; ++j) {
                const double v = -1.0 + 2.0 * (y + static_cast<double>(j) / edge_grid) / side;
                const double u = -1.0 + 2.0 * (x + 1U) / side;
                fine[static_cast<std::size_t>(j)] =
                    relief.sample(tile_face_direction(left.face, u, v)).elevation_m - terrain.sea_level_m;
                if (j % 2 == 0) {
                    const double coarse_u = -1.0 + 2.0 * right_coarse.x / (side / 2U);
                    const double coarse_v = -1.0 + 2.0 * (right_coarse.y +
                        ((y % 2U) * edge_grid / 2 + j / 2) / static_cast<double>(edge_grid)) / (side / 2U);
                    const double independent = relief.sample(tile_face_direction(right_coarse.face, coarse_u,
                                                                                   coarse_v)).elevation_m -
                                               terrain.sea_level_m;
                    worst_shared_m = std::max(worst_shared_m,
                                              std::abs(independent - fine[static_cast<std::size_t>(j)]));
                }
            }
            const auto result = compare(fine, mesh->radius_m() * std::numbers::pi / (2.0 * side));
            if (result.fine && (result.fine != result.coarse || result.max_shift_m > chosen.max_shift_m)) {
                chosen = result;
                chosen_key = left;
                if (result.fine != result.coarse) {
                    found = true;
                    std::vector<double> constrained(static_cast<std::size_t>(edge_grid + 1));
                    for (int j = 0; j <= edge_grid; ++j) {
                        const double coarse_u = -1.0 + 2.0 * right_coarse.x / (side / 2U);
                        const double coarse_v = -1.0 + 2.0 * (right_coarse.y +
                            ((y % 2U) * edge_grid / 2 + j / 2.0) / edge_grid) / (side / 2U);
                        constrained[static_cast<std::size_t>(j)] =
                            relief.sample(tile_face_direction(0, coarse_u, coarse_v)).elevation_m -
                            terrain.sea_level_m;
                        if (j % 2 != 0) {
                            const double interpolated = 0.5 * (fine[static_cast<std::size_t>(j - 1)] +
                                                               fine[static_cast<std::size_t>(j + 1)]);
                            chosen_odd_flips += (interpolated <= 0.0) !=
                                                (constrained[static_cast<std::size_t>(j)] <= 0.0);
                            ++chosen_constraint_vertices;
                        }
                    }
                    const auto left_crossings = crossings(fine, 1);
                    const auto right_crossings = crossings(constrained, 1);
                    if (left_crossings.size() != right_crossings.size())
                        throw std::runtime_error("constrained crossing count differs across seam");
                    for (std::size_t i = 0; i < left_crossings.size(); ++i)
                        constrained_crossing_error_m = std::max(constrained_crossing_error_m,
                            std::abs(left_crossings[i] - right_crossings[i]) *
                            mesh->radius_m() * std::numbers::pi / (2.0 * side * edge_grid));
                    // A one-coarse-grid-cell-wide ribbon. Two fine boundary
                    // segments and one coarse interior segment form three
                    // triangles, replacing the ordinary two-triangle quad.
                    Strip stitched, ordinary;
                    const double coarse_u = -1.0 + 2.0 * right_coarse.x / (side / 2U);
                    const double inside_u = coarse_u + 2.0 / ((side / 2U) * edge_grid);
                    const auto position = [&](const Vertex& point) {
                        const double u = coarse_u + point.u * (inside_u - coarse_u);
                        const double v = -1.0 + 2.0 * (right_coarse.y +
                            ((y % 2U) * edge_grid / 2 + point.j / 2.0) / edge_grid) / (side / 2U);
                        return tile_face_direction(0, u, v) *
                            (mesh->radius_m() + std::max(0.0, point.h));
                    };
                    const auto emit = [&](const std::array<Vertex, 3>& triangle) {
                        add_triangle(stitched, triangle);
                        for (const bool water : {true, false}) {
                            const auto polygon = clip(triangle, water);
                            for (std::size_t t = 2; t < polygon.size(); ++t) {
                                const Vec3d a = position(polygon[0]);
                                const Vec3d b = position(polygon[t - 1]);
                                const Vec3d c = position(polygon[t]);
                                if (dot(cross(b - a, c - a), a) <= 0.0)
                                    ++strip_inverted_triangles;
                            }
                        }
                    };
                    for (int j = 0; j < edge_grid; j += 2) {
                        const auto boundary = [&](int index) {
                            return Vertex{0.0, static_cast<double>(index),
                                          constrained[static_cast<std::size_t>(index)]};
                        };
                        const auto inside = [&](int index) {
                            const double coarse_v = -1.0 + 2.0 * (right_coarse.y +
                                ((y % 2U) * edge_grid / 2 + index / 2.0) / edge_grid) / (side / 2U);
                            return Vertex{1.0, static_cast<double>(index),
                                relief.sample(tile_face_direction(0, inside_u, coarse_v)).elevation_m -
                                terrain.sea_level_m};
                        };
                        const Vertex b0 = boundary(j), b1 = boundary(j + 1), b2 = boundary(j + 2);
                        const Vertex i0 = inside(j), i2 = inside(j + 2);
                        emit({b0, i0, b1});
                        emit({b1, i0, b2});
                        emit({b2, i0, i2});
                        add_triangle(ordinary, {b0, i0, b2});
                        add_triangle(ordinary, {b2, i0, i2});
                    }
                    for (int j = 0; j <= edge_grid; ++j) {
                        const double v = -1.0 + 2.0 * (y + static_cast<double>(j) / edge_grid) / side;
                        const double u = -1.0 + 2.0 * (x + 1U) / side;
                        const Vec3d fine_point = tile_face_direction(0, u, v) *
                            (mesh->radius_m() + std::max(0.0, fine[static_cast<std::size_t>(j)]));
                        strip_max_edge_gap_m = std::max(strip_max_edge_gap_m,
                            length(position({0.0, static_cast<double>(j),
                                             constrained[static_cast<std::size_t>(j)]}) - fine_point));
                    }
                    std::sort(stitched.boundary_intervals.begin(), stitched.boundary_intervals.end());
                    if (stitched.boundary_intervals.size() != static_cast<std::size_t>(edge_grid))
                        throw std::runtime_error("seam ribbon boundary edge count differs");
                    for (int j = 0; j < edge_grid; ++j)
                        if (stitched.boundary_intervals[static_cast<std::size_t>(j)] != j)
                            throw std::runtime_error("seam ribbon does not cover each fine boundary edge once");
                    unique_positions(stitched.boundary_crossings);
                    if (stitched.boundary_crossings.size() != left_crossings.size())
                        throw std::runtime_error("clipped ribbon loses a boundary crossing");
                    for (std::size_t i = 0; i < left_crossings.size(); ++i)
                        if (std::abs(stitched.boundary_crossings[i] - left_crossings[i]) > 1e-9)
                            throw std::runtime_error("clipped ribbon boundary coast differs from fine edge");
                    strip_extra_vertices = stitched.clipped_vertices - ordinary.clipped_vertices;
                    strip_triangles = stitched.triangles;
                    strip_boundary_crossings = stitched.boundary_crossings.size();
                }
            }
        }
    }
    std::cout << "L" << level << " mixed-level seam face0 fine tile "
              << static_cast<int>(chosen_key.level) << '/' << chosen_key.x << '/' << chosen_key.y
              << " fine/coarse linear crossings " << chosen.fine << '/' << chosen.coarse
              << "; fine crossings >1 fine interval from coarse " << chosen.unmatched
              << "; max nearest coarse shift approx " << chosen.max_shift_m
              << " m; odd boundary classification flips " << chosen_odd_flips
              << "; add " << chosen_constraint_vertices << " boundary samples to coarse neighbour"
              << " -> independently generated constrained crossing offset "
              << constrained_crossing_error_m << " m; common heights max " << worst_shared_m
              << " m; stitched strip " << strip_triangles << " triangles, "
              << strip_boundary_crossings << " clipped boundary crossings, +"
              << strip_extra_vertices << " triangle-list vertices (~" << strip_extra_vertices * 72U
              << " bytes) vs ordinary strip; inverted 3D triangles "
              << strip_inverted_triangles << "; max independent 3D edge gap "
              << strip_max_edge_gap_m << " m\n";

    // Face 0 (+x), right edge u=+1 is face 5 (-z), left edge u=-1.
    // They have the same v coordinate and identical unnormalised direction.
    const int count = edge_grid * static_cast<int>(side);
    std::vector<double> face0(static_cast<std::size_t>(count + 1));
    std::vector<double> face5(static_cast<std::size_t>(count + 1));
    double face_height_error_m = 0.0;
    for (int j = 0; j <= count; ++j) {
        const double v = -1.0 + 2.0 * j / count;
        face0[static_cast<std::size_t>(j)] = relief.sample(tile_face_direction(0, 1.0, v)).elevation_m -
                                             terrain.sea_level_m;
        face5[static_cast<std::size_t>(j)] = relief.sample(tile_face_direction(5, -1.0, v)).elevation_m -
                                             terrain.sea_level_m;
        face_height_error_m = std::max(face_height_error_m,
            std::abs(face0[static_cast<std::size_t>(j)] - face5[static_cast<std::size_t>(j)]));
    }
    const auto face_crossings = crossings(face0, 1);
    const auto other_crossings = crossings(face5, 1);
    double crossing_error_m = 0.0;
    if (face_crossings.size() == other_crossings.size()) {
        for (std::size_t i = 0; i < face_crossings.size(); ++i)
            crossing_error_m = std::max(crossing_error_m,
                std::abs(face_crossings[i] - other_crossings[i]) *
                mesh->radius_m() * std::numbers::pi / (2.0 * count));
    }
    std::cout << "L" << level << " face0-right / face5-left: " << face_crossings.size()
              << '/' << other_crossings.size() << " crossings; max common height error "
              << face_height_error_m << " m; max crossing offset approx " << crossing_error_m
              << " m (equal counts " << (face_crossings.size() == other_crossings.size()) << ")\n";
}
} // namespace

int main() {
    // A sign sequence with an island between coarse samples must be detected.
    if (crossings({-1.0, 1.0, -1.0}, 1).size() != 2 ||
        !crossings({-1.0, 1.0, -1.0}, 2).empty()) return 1;
    run(4);
    run(6);
}
