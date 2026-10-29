// Proposed ADR-0030: read-only clipped coastal geometry experiment.
// Build as the other coast probes against libPlanetSimPresentation/PlanetSim.
// -DCOAST_GRID=256 selects a denser canonical sampling grid (default 128).
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
#ifndef COAST_GRID
#define COAST_GRID 128
#endif
constexpr int grid = COAST_GRID;
static_assert(grid >= 8 && (grid & (grid - 1)) == 0);

struct Point {
    double x, y; // integer-grid coordinates, interpolated on a shared edge
    double height_m;
};
struct Segment {
    Point a, b;
};
struct Counts {
    std::size_t wet_triangles = 0;
    std::size_t dry_triangles = 0;
    std::size_t coastal_squares = 0;
    std::vector<Vec3d> coastal_positions;
    double maximum_shore_radius_error = 0.0;
};
struct Patch {
    const TerrainRelief& relief;
    TileKey key;
    double sea_level_m;
    std::vector<double> heights;

    Patch(const TerrainRelief& r, TileKey k, double sea) : relief(r), key(k), sea_level_m(sea),
        heights(static_cast<std::size_t>((grid + 1) * (grid + 1))) {
        for (int y = 0; y <= grid; ++y) {
            for (int x = 0; x <= grid; ++x) heights[static_cast<std::size_t>(y * (grid + 1) + x)] = sample(x, y);
        }
    }
    Vec3d direction(double x, double y) const {
        const double scale = std::ldexp(1.0, key.level);
        return tile_face_direction(key.face,
            -1.0 + 2.0 * (static_cast<double>(key.x) + x / grid) / scale,
            -1.0 + 2.0 * (static_cast<double>(key.y) + y / grid) / scale);
    }
    double sample(double x, double y) const {
        return relief.sample(direction(x, y)).elevation_m - sea_level_m;
    }
    double at(int x, int y) const { return heights[static_cast<std::size_t>(y * (grid + 1) + x)]; }
};

Point crossing(const Point& a, const Point& b) {
    const double t = a.height_m / (a.height_m - b.height_m);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0};
}

// Sutherland–Hodgman clipping of a triangle against the sea-level plane.
std::vector<Point> clip(const std::array<Point, 3>& triangle, bool wet) {
    std::vector<Point> output;
    for (int i = 0; i < 3; ++i) {
        const Point& a = triangle[static_cast<std::size_t>(i)];
        const Point& b = triangle[static_cast<std::size_t>((i + 1) % 3)];
        const bool inside_a = wet ? a.height_m <= 0.0 : a.height_m >= 0.0;
        const bool inside_b = wet ? b.height_m <= 0.0 : b.height_m >= 0.0;
        if (inside_a) output.push_back(a);
        if (inside_a != inside_b) output.push_back(crossing(a, b));
    }
    return output;
}

void emit(const Patch& patch, const std::array<Point, 3>& triangle, bool coastal,
          std::vector<Segment>& shore, Counts& counts) {
    const auto wet = clip(triangle, true);
    const auto dry = clip(triangle, false);
    if (wet.size() >= 3) counts.wet_triangles += wet.size() - 2;
    if (dry.size() >= 3) counts.dry_triangles += dry.size() - 2;
    if (coastal) {
        // Materialise the clipped polygons as 3D triangle vertices, with
        // ocean at sea level and land raised by its signed height. Count
        // the existing tile format's 72-byte vertices for a cost estimate.
        const auto materialise = [&](const std::vector<Point>& polygon, bool water) {
            for (std::size_t t = 2; t < polygon.size(); ++t) {
                for (const Point& p : {polygon[0], polygon[t - 1], polygon[t]}) {
                    const Vec3d vertex = patch.direction(p.x, p.y) *
                        (1.0 + (water ? 0.0 : std::max(0.0, p.height_m) / patch.relief.mesh().radius_m()));
                    counts.coastal_positions.push_back(vertex);
                    if (p.height_m == 0.0)
                        counts.maximum_shore_radius_error = std::max(counts.maximum_shore_radius_error,
                                                                      std::abs(length(vertex) - 1.0));
                }
            }
        };
        materialise(wet, true);
        materialise(dry, false);
    }
    // Exactly two distinct zero-height points bound this triangle's shore.
    // Triangles on an exactly zero-height edge are degenerate: ownership of
    // that edge is resolved by the common signed sample, not by its winding.
    if (wet.size() < 3 || dry.size() < 3) return;
    std::vector<Point> boundary;
    for (const Point& p : wet) {
        if (p.height_m != 0.0) continue;
        if (std::none_of(boundary.begin(), boundary.end(), [&](const Point& q) {
            return std::abs(p.x - q.x) < 1e-12 && std::abs(p.y - q.y) < 1e-12;
        })) boundary.push_back(p);
    }
    if (boundary.size() == 2) shore.push_back({boundary[0], boundary[1]});
}

void region(const Patch& patch, int step, std::vector<Segment>& shore, Counts& counts) {
    for (int y = 0; y < grid; y += step) {
        for (int x = 0; x < grid; x += step) {
            const Point a{static_cast<double>(x), static_cast<double>(y), patch.at(x, y)};
            const Point b{static_cast<double>(x + step), static_cast<double>(y), patch.at(x + step, y)};
            const Point c{static_cast<double>(x), static_cast<double>(y + step), patch.at(x, y + step)};
            const Point d{static_cast<double>(x + step), static_cast<double>(y + step), patch.at(x + step, y + step)};
            const std::array<double, 4> h{a.height_m, b.height_m, c.height_m, d.height_m};
            const bool coastal = std::any_of(h.begin(), h.end(), [&](double value) {
                return (value <= 0.0) != (h[0] <= 0.0);
            });
            counts.coastal_squares += coastal;
            emit(patch, {a, d, b}, coastal, shore, counts);
            emit(patch, {a, c, d}, coastal, shore, counts);
        }
    }
}

// Compare exact, globally keyed edge positions, quantised to a sub-millimetre
// on this grid. Sorting removes triangle order and winding differences.
using Edge = std::array<std::int64_t, 4>;
std::multiset<Edge> edges(const std::vector<Segment>& segments) {
    std::multiset<Edge> result;
    const auto quantise = [](const Point& p) {
        return std::array<std::int64_t, 2>{std::llround(p.x * 1e9), std::llround(p.y * 1e9)};
    };
    for (const auto& segment : segments) {
        auto a = quantise(segment.a);
        auto b = quantise(segment.b);
        if (b < a) std::swap(a, b);
        result.insert({a[0], a[1], b[0], b[1]});
    }
    return result;
}

double distance_to_shore_m(const Patch& patch, const std::vector<Segment>& segments,
                           double x, double y) {
    double best_squared = 1e300;
    double nearest_x = x, nearest_y = y;
    for (const Segment& segment : segments) {
        const double dx = segment.b.x - segment.a.x;
        const double dy = segment.b.y - segment.a.y;
        const double length_squared = dx * dx + dy * dy;
        const double t = length_squared > 0.0
            ? std::clamp(((x - segment.a.x) * dx + (y - segment.a.y) * dy) / length_squared, 0.0, 1.0)
            : 0.0;
        const double px = segment.a.x + t * dx;
        const double py = segment.a.y + t * dy;
        const double squared = (x - px) * (x - px) + (y - py) * (y - py);
        if (squared < best_squared) {
            best_squared = squared;
            nearest_x = px;
            nearest_y = py;
        }
    }
    const Vec3d a = patch.direction(x, y);
    const Vec3d b = patch.direction(nearest_x, nearest_y);
    // atan2 remains accurate for short arcs, where acos(dot) loses digits.
    return std::atan2(length(cross(a, b)), dot(a, b)) * patch.relief.mesh().radius_m();
}

TileKey coastal_tile(const TerrainRelief& relief, double sea, std::uint8_t level) {
    const std::uint32_t side = 1U << level;
    for (std::uint8_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x + 1 < side; ++x) {
                const TileKey key{face, level, x, y};
                const auto sample = [&](double u, double v) {
                    const double scale = static_cast<double>(side);
                    return relief.sample(tile_face_direction(face,
                        -1.0 + 2.0 * (x + u) / scale,
                        -1.0 + 2.0 * (y + v) / scale)).elevation_m - sea;
                };
                if ((sample(0.0, 0.0) <= 0.0) != (sample(0.5, 0.5) <= 0.0)) return key;
            }
        }
    }
    return {};
}

void run(std::uint32_t subdivision) {
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = subdivision;
    const PlanetRun planet(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const TileKey key = coastal_tile(relief, terrain.sea_level_m, static_cast<std::uint8_t>(subdivision));
    const auto start = std::chrono::steady_clock::now();
    const Patch patch(relief, key, terrain.sea_level_m);
    const double sampling_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::vector<Segment> parent;
    std::vector<Segment> children;
    Counts parent_counts, child_counts;
    region(patch, 1, parent, parent_counts);
    for (int child_y = 0; child_y < 2; ++child_y) {
        for (int child_x = 0; child_x < 2; ++child_x) {
            const TileKey child{key.face, static_cast<std::uint8_t>(key.level + 1),
                                key.x * 2U + static_cast<std::uint32_t>(child_x),
                                key.y * 2U + static_cast<std::uint32_t>(child_y)};
            const Patch child_patch(relief, child, terrain.sea_level_m);
            std::vector<Segment> local;
            region(child_patch, 2, local, child_counts);
            // Independent child coordinates back into the parent's fixed
            // global lattice; no child samples or segments are copied from
            // the parent patch.
            for (Segment segment : local) {
                for (Point* p : {&segment.a, &segment.b}) {
                    p->x = child_x * grid / 2.0 + p->x / 2.0;
                    p->y = child_y * grid / 2.0 + p->y / 2.0;
                }
                children.push_back(segment);
            }
        }
    }
    const bool same = edges(parent) == edges(children);
    // Sample the same canonical height at the right-hand neighbouring face
    // tile's edge. This does not prove mixed-LOD triangulation there.
    const double scale = std::ldexp(1.0, key.level);
    double seam_error_m = 0.0;
    for (int j = 0; j <= grid; ++j) {
        const auto other = tile_face_direction(key.face,
            -1.0 + 2.0 * (static_cast<double>(key.x + 1U)) / scale,
            -1.0 + 2.0 * (static_cast<double>(key.y) + static_cast<double>(j) / grid) / scale);
        seam_error_m = std::max(seam_error_m,
            std::abs(patch.at(grid, j) - (relief.sample(other).elevation_m - terrain.sea_level_m)));
    }
    std::size_t wrong = 0, compared = 0;
    double largest_wrong_height_m = 0.0;
    std::vector<double> wrong_distances_m;
    for (int y = 0; y < grid; ++y) {
        for (int x = 0; x < grid; ++x) {
            const double actual = patch.sample(x + 0.5, y + 0.5);
            if (std::abs(actual) <= 1.0) continue;
            const double a = patch.at(x, y);
            const double d = patch.at(x + 1, y + 1);
            const double reconstructed = 0.5 * (a + d); // centre on shared diagonal
            const bool mismatch = (actual <= 0.0) != (reconstructed <= 0.0);
            wrong += mismatch;
            if (mismatch) largest_wrong_height_m = std::max(largest_wrong_height_m, std::abs(actual));
            if (mismatch) wrong_distances_m.push_back(distance_to_shore_m(patch, parent, x + 0.5, y + 0.5));
            ++compared;
        }
    }
    const double total_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::sort(wrong_distances_m.begin(), wrong_distances_m.end());
    const double p95_distance_m = wrong_distances_m.empty() ? 0.0 :
        wrong_distances_m[static_cast<std::size_t>(0.95 * (wrong_distances_m.size() - 1U))];
    const double max_distance_m = wrong_distances_m.empty() ? 0.0 : wrong_distances_m.back();
    std::cout << "L" << subdivision << " tile " << static_cast<int>(key.face) << "/"
              << static_cast<int>(key.level) << "/" << key.x << "/" << key.y
              << " grid " << grid << ": " << parent_counts.coastal_squares << " coastal squares, "
              << parent.size() << " coast segments; parent/4-child edge sets "
              << (same ? "IDENTICAL" : "DIFFER") << "; shared tile-edge height max "
              << seam_error_m << " m; independent midpoint flips " << wrong << "/" << compared
              << " (largest |height| " << largest_wrong_height_m << " m; distance to generated shore"
              << " p95/max " << p95_distance_m << "/" << max_distance_m << " m;"
              << " at 30 km, 650 px focal: " << p95_distance_m * 650.0 / 30'000.0
              << "/" << max_distance_m * 650.0 / 30'000.0 << " px)"
              << "; generated triangles wet/dry " << parent_counts.wet_triangles << "/"
              << parent_counts.dry_triangles << "; sample " << sampling_s << " s, total "
              << total_s << " s; coastal vertices " << parent_counts.coastal_positions.size()
              << " (" << parent_counts.coastal_positions.size() * sizeof(Vec3d) << " B positions; ~"
              << parent_counts.coastal_positions.size() * 72U << " B in current tile format)"
              << "; shore radius error " << parent_counts.maximum_shore_radius_error
              << "; raw segment bytes " << parent.size() * sizeof(Segment) << "\n";
}
} // namespace

int main() {
    run(4);
    run(6);
}
