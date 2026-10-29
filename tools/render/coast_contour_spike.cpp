// Read-only 2D feasibility spike for proposed ADR-0030. This is not a tile
// generator: it measures a candidate adaptive coastal mask against the
// canonical full-rank relief on one coastal cube-face patch per mesh level.
// Build: c++ -std=c++20 -O3 -ffp-contract=off -I .
//   tools/render/coast_contour_spike.cpp build-godot/libPlanetSimPresentation.a
//   build-godot/libPlanetSim.a -lzstd -pthread -o build-godot/coast_contour_spike
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
#ifndef COAST_GRID
#define COAST_GRID 128
#endif
constexpr int grid = COAST_GRID;
static_assert(grid >= 8 && (grid & (grid - 1)) == 0);

struct Patch {
    const TerrainRelief& relief;
    TileKey key;
    double sea_level_m;
    std::map<std::pair<int, int>, double> samples;

    double height(int x, int y) {
        const auto point = std::pair{x, y};
        if (const auto found = samples.find(point); found != samples.end()) return found->second;
        const double scale = std::ldexp(1.0, key.level);
        const double u = -1.0 + 2.0 * (static_cast<double>(key.x) + static_cast<double>(x) / (2 * grid)) / scale;
        const double v = -1.0 + 2.0 * (static_cast<double>(key.y) + static_cast<double>(y) / (2 * grid)) / scale;
        const auto direction = tile_face_direction(key.face, u, v);
        const double value = relief.sample(direction).elevation_m - sea_level_m;
        samples.emplace(point, value);
        return value;
    }
};

struct Leaf {
    int x, y, size;
    std::array<double, 4> corners; // bottom-left, bottom-right, top-left, top-right
};

struct Point { double x, y, h; };
struct Segment { Point a, b; };

Point zero_crossing(const Point& a, const Point& b) {
    const double t = a.h / (a.h - b.h);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0};
}

std::vector<Point> clip(const std::array<Point, 3>& triangle, bool wet) {
    std::vector<Point> polygon;
    for (int i = 0; i < 3; ++i) {
        const Point& a = triangle[static_cast<std::size_t>(i)];
        const Point& b = triangle[static_cast<std::size_t>((i + 1) % 3)];
        const bool in_a = wet ? a.h <= 0.0 : a.h >= 0.0;
        const bool in_b = wet ? b.h <= 0.0 : b.h >= 0.0;
        if (in_a) polygon.push_back(a);
        if (in_a != in_b) polygon.push_back(zero_crossing(a, b));
    }
    return polygon;
}

void coastal_triangle(const std::array<Point, 3>& triangle, std::vector<Segment>& shore,
                      std::size_t& coastal_vertices) {
    const auto wet = clip(triangle, true);
    const auto dry = clip(triangle, false);
    if (wet.size() < 3 || dry.size() < 3) return;
    coastal_vertices += 3U * (wet.size() + dry.size() - 4U);
    std::vector<Point> zero;
    for (const Point& point : wet) {
        if (point.h == 0.0 && std::none_of(zero.begin(), zero.end(), [&](const Point& other) {
            return std::abs(point.x - other.x) < 1e-12 && std::abs(point.y - other.y) < 1e-12;
        })) zero.push_back(point);
    }
    if (zero.size() == 2) shore.push_back({zero[0], zero[1]});
}

double grid_distance_to_shore(const std::vector<Segment>& segments, double x, double y) {
    double best_squared = 1e300;
    for (const auto& segment : segments) {
        const double dx = segment.b.x - segment.a.x;
        const double dy = segment.b.y - segment.a.y;
        const double length2 = dx * dx + dy * dy;
        const double t = length2 > 0.0 ? std::clamp(((x - segment.a.x) * dx +
            (y - segment.a.y) * dy) / length2, 0.0, 1.0) : 0.0;
        const double ex = x - segment.a.x - t * dx;
        const double ey = y - segment.a.y - t * dy;
        best_squared = std::min(best_squared, ex * ex + ey * ey);
    }
    return std::sqrt(best_squared);
}

double bilinear(const Leaf& leaf, double x, double y) {
    const double u = (x - leaf.x) / leaf.size;
    const double v = (y - leaf.y) / leaf.size;
    return (1.0 - v) * ((1.0 - u) * leaf.corners[0] + u * leaf.corners[1]) +
           v * ((1.0 - u) * leaf.corners[2] + u * leaf.corners[3]);
}

void subdivide(Patch& patch, std::vector<Leaf>& leaves, int x, int y, int size, double tolerance_m) {
    const Leaf leaf{x, y, size, {patch.height(2 * x, 2 * y), patch.height(2 * (x + size), 2 * y),
                                  patch.height(2 * x, 2 * (y + size)),
                                  patch.height(2 * (x + size), 2 * (y + size))}};
    if (size > 1) {
        const int half = size / 2;
        const std::array<std::pair<int, int>, 5> checks{{{x + half, y}, {x, y + half},
            {x + half, y + half}, {x + size, y + half}, {x + half, y + size}}};
        const bool mixed = std::any_of(leaf.corners.begin(), leaf.corners.end(),
            [&](double h) { return (h <= 0.0) != (leaf.corners[0] <= 0.0); });
        bool refine = mixed;
        for (const auto& [cx, cy] : checks) {
            const double actual = patch.height(2 * cx, 2 * cy);
            const double approximation = bilinear(leaf, cx, cy);
            refine |= (actual <= 0.0) != (approximation <= 0.0) ||
                      std::abs(actual - approximation) > tolerance_m;
        }
        if (refine) {
            subdivide(patch, leaves, x, y, half, tolerance_m);
            subdivide(patch, leaves, x + half, y, half, tolerance_m);
            subdivide(patch, leaves, x, y + half, half, tolerance_m);
            subdivide(patch, leaves, x + half, y + half, half, tolerance_m);
            return;
        }
    }
    leaves.push_back(leaf);
}

TileKey find_coastal_tile(const TerrainRelief& relief, double sea_level_m, std::uint8_t level) {
    const std::uint32_t side = 1U << level;
    for (std::uint8_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                Patch patch{relief, {face, level, x, y}, sea_level_m, {}};
                bool land = false, water = false;
                for (int j = 0; j <= 4; ++j) {
                    for (int i = 0; i <= 4; ++i) {
                        const double h = patch.height(i * grid / 2, j * grid / 2);
                        land |= h > 0.0;
                        water |= h <= 0.0;
                    }
                }
                if (land && water && x + 1 < side) return patch.key;
            }
        }
    }
    return {};
}

void report(std::uint32_t level, double tolerance_m) {
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = level;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const TileKey key = find_coastal_tile(relief, terrain.sea_level_m,
                                           static_cast<std::uint8_t>(level));
    Patch patch{relief, key, terrain.sea_level_m, {}};
    const auto start = std::chrono::steady_clock::now();
    std::vector<Leaf> leaves;
    subdivide(patch, leaves, 0, 0, grid, tolerance_m);
    std::vector<Segment> shore;
    std::size_t coastal_leaves = 0;
    std::size_t coastal_vertices = 0;
    for (const auto& leaf : leaves) {
        const auto& h = leaf.corners;
        const bool mixed = std::any_of(h.begin(), h.end(), [&](double value) {
            return (value <= 0.0) != (h[0] <= 0.0);
        });
        if (!mixed) continue;
        ++coastal_leaves;
        const Point a{static_cast<double>(leaf.x), static_cast<double>(leaf.y), h[0]};
        const Point b{static_cast<double>(leaf.x + leaf.size), static_cast<double>(leaf.y), h[1]};
        const Point c{static_cast<double>(leaf.x), static_cast<double>(leaf.y + leaf.size), h[2]};
        const Point d{static_cast<double>(leaf.x + leaf.size), static_cast<double>(leaf.y + leaf.size), h[3]};
        coastal_triangle({a, d, b}, shore, coastal_vertices);
        coastal_triangle({a, c, d}, shore, coastal_vertices);
    }
    std::size_t wrong = 0;
    std::size_t wrong_finest = 0;
    std::size_t compared = 0;
    std::vector<double> wrong_distances_grid;
    // Test independent points (odd coordinates) that were not used by the
    // dyadic refinement. Detect hidden islands and missed narrow inlets.
    for (const auto& leaf : leaves) {
        for (int y = leaf.y; y < leaf.y + leaf.size; ++y) {
            for (int x = leaf.x; x < leaf.x + leaf.size; ++x) {
                const double actual = patch.height(2 * x + 1, 2 * y + 1);
                if (std::abs(actual) <= 1.0) continue;
                const bool mismatch = (actual <= 0.0) != (bilinear(leaf, x + 0.5, y + 0.5) <= 0.0);
                wrong += mismatch;
                if (leaf.size == 1) wrong_finest += mismatch;
                if (mismatch && !shore.empty())
                    wrong_distances_grid.push_back(grid_distance_to_shore(shore, x + 0.5, y + 0.5));
                ++compared;
            }
        }
    }
    // At the common dyadic edge, both tiles ask the same canonical field
    // for the same position. This is only a value check, NOT a mixed-LOD
    // topology or mesh-stitching proof.
    Patch neighbour{relief, {key.face, key.level, key.x + 1U, key.y}, terrain.sea_level_m, {}};
    double maximum_seam_difference_m = 0.0;
    for (int y = 0; y <= 2 * grid; ++y)
        maximum_seam_difference_m = std::max(maximum_seam_difference_m,
            std::abs(patch.height(2 * grid, y) - neighbour.height(0, y)));
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::sort(wrong_distances_grid.begin(), wrong_distances_grid.end());
    const double p95_grid = wrong_distances_grid.empty() ? 0.0 :
        wrong_distances_grid[static_cast<std::size_t>(0.95 * (wrong_distances_grid.size() - 1U))];
    // A conservative physical estimate using the tile's angular span; the
    // cube-face warp means this is not a measured geodesic distance.
    const double metres_per_grid = tile_angular_size(key) * mesh->radius_m() / grid;
    std::cout << "L" << level << " face " << static_cast<int>(key.face) << " tile "
              << static_cast<int>(key.level) << "/" << key.x << "/" << key.y
              << " grid " << grid << " tolerance " << tolerance_m << " m: " << leaves.size() << " leaves, "
              << patch.samples.size() << " canonical samples, " << wrong << "/" << compared
               << " independent points on wrong side (" << wrong_finest
               << " in finest leaves); " << coastal_leaves << " coastal leaves, "
               << shore.size() << " coast segments, " << coastal_vertices
               << " clipped vertices (~" << coastal_vertices * 72U << " B);"
               << " wrong-side distance to generated shore p95 ~" << p95_grid * metres_per_grid
               << " m; shared-edge max height difference "
              << maximum_seam_difference_m << " m; " << elapsed << " s\n";
}
} // namespace

int main() {
    for (const std::uint32_t level : {4U, 6U}) {
        for (const double tolerance : {200.0, 50.0}) report(level, tolerance);
    }
}
