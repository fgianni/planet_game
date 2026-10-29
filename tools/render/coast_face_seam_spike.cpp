// Probe-only cube-face boundary audit for proposed ADR-0030.
// Build against libPlanetSimPresentation.a and libPlanetSim.a.
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

struct Frame { Vec3d n, u, v; };
// Mirrors the accepted R1-04 cube-face convention, not the renderer's
// topology code: an independent inverse is used to expose seam mistakes.
constexpr std::array<Frame, 6> faces{{
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
}};

std::uint8_t containing_face(const Vec3d& p) {
    std::uint8_t best = 0;
    double largest = -1e300;
    for (std::uint8_t i = 0; i < faces.size(); ++i) {
        const double value = dot(p, faces[i].n);
        if (value > largest) { largest = value; best = i; }
    }
    return best;
}

std::array<double, 2> face_coordinates(std::uint8_t face, const Vec3d& p) {
    const auto& f = faces[face];
    const double normal = dot(p, f.n);
    return {std::atan2(dot(p, f.u), normal) * 4.0 / std::numbers::pi,
            std::atan2(dot(p, f.v), normal) * 4.0 / std::numbers::pi};
}

Vec3d edge_direction(std::uint8_t face, int edge, double t) {
    switch (edge) {
        case 0: return tile_face_direction(face, -1.0, t);
        case 1: return tile_face_direction(face, 1.0, t);
        case 2: return tile_face_direction(face, t, -1.0);
        default: return tile_face_direction(face, t, 1.0);
    }
}

std::uint8_t neighbour(std::uint8_t face, int edge) {
    switch (edge) {
        case 0: return containing_face(tile_face_direction(face, -1.001, 0.0));
        case 1: return containing_face(tile_face_direction(face, 1.001, 0.0));
        case 2: return containing_face(tile_face_direction(face, 0.0, -1.001));
        default: return containing_face(tile_face_direction(face, 0.0, 1.001));
    }
}

void audit(std::uint32_t level, std::uint64_t seed) {
    Scenario scenario;
    scenario.seed = seed;
    scenario.subdivision = level;
    const PlanetRun planet(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    // One full face edge at this level's 128-sample-per-tile lattice.
    const int n = 128 * (1 << level);
    std::size_t samples = 0, coast_crossings = 0, unmatched_crossings = 0, flips = 0;
    double max_height_m = 0.0, max_position_m = 0.0, max_crossing_m = 0.0;
    for (std::uint8_t face = 0; face < 6; ++face) {
        for (int edge = 0; edge < 4; ++edge) {
            const std::uint8_t other = neighbour(face, edge);
            if (other == face) throw std::runtime_error("face edge has no neighbour");
            const auto midpoint = face_coordinates(other, edge_direction(face, edge, 0.0));
            const bool fixed_u = std::abs(midpoint[0]) > std::abs(midpoint[1]);
            const double fixed = (fixed_u ? midpoint[0] : midpoint[1]) > 0.0 ? 1.0 : -1.0;
            const auto endpoint = face_coordinates(other, edge_direction(face, edge, 1.0));
            const double sense = (fixed_u ? endpoint[1] : endpoint[0]) > 0.0 ? 1.0 : -1.0;
            std::vector<double> first(static_cast<std::size_t>(n + 1));
            std::vector<double> second(static_cast<std::size_t>(n + 1));
            for (int i = 0; i <= n; ++i) {
                const double t = -1.0 + 2.0 * i / n;
                const Vec3d a = edge_direction(face, edge, t);
                // Rebuild the neighbouring edge directly from *its own*
                // fixed face edge and dyadic varying coordinate; no point
                // is projected from the first face for this sample.
                const Vec3d b = fixed_u ? tile_face_direction(other, fixed, sense * t) :
                                          tile_face_direction(other, sense * t, fixed);
                first[static_cast<std::size_t>(i)] = relief.sample(a).elevation_m - terrain.sea_level_m;
                second[static_cast<std::size_t>(i)] = relief.sample(b).elevation_m - terrain.sea_level_m;
                max_position_m = std::max(max_position_m, length(a - b) * mesh->radius_m());
                max_height_m = std::max(max_height_m,
                    std::abs(first[static_cast<std::size_t>(i)] - second[static_cast<std::size_t>(i)]));
                flips += (first[static_cast<std::size_t>(i)] <= 0.0) !=
                         (second[static_cast<std::size_t>(i)] <= 0.0);
                ++samples;
                if (i == 0) continue;
                const double ah = first[static_cast<std::size_t>(i - 1)];
                const double bh = first[static_cast<std::size_t>(i)];
                const double ch = second[static_cast<std::size_t>(i - 1)];
                const double dh = second[static_cast<std::size_t>(i)];
                const bool cross_a = (ah <= 0.0) != (bh <= 0.0);
                const bool cross_b = (ch <= 0.0) != (dh <= 0.0);
                if (cross_a != cross_b) ++unmatched_crossings;
                if (!cross_a || !cross_b) continue;
                ++coast_crossings;
                const double ta = (i - 1 + ah / (ah - bh)) / n * 2.0 - 1.0;
                const double tb = (i - 1 + ch / (ch - dh)) / n * 2.0 - 1.0;
                const Vec3d wa = edge_direction(face, edge, ta);
                const Vec3d wb = edge_direction(face, edge, tb);
                max_crossing_m = std::max(max_crossing_m, length(wa - wb) * mesh->radius_m());
            }
        }
    }
    std::size_t corners = 0, corner_flips = 0;
    double corner_height_m = 0.0, corner_position_m = 0.0;
    for (int sx : {-1, 1}) for (int sy : {-1, 1}) for (int sz : {-1, 1}) {
        const Vec3d cube{static_cast<double>(sx), static_cast<double>(sy), static_cast<double>(sz)};
        bool first = true;
        double reference = 0.0;
        Vec3d reference_direction{};
        for (std::uint8_t face = 0; face < 6; ++face) {
            if (dot(cube, faces[face].n) <= 0.0) continue;
            const auto [u, v] = face_coordinates(face, cube);
            const Vec3d direction = tile_face_direction(face, u, v);
            const double height_m = relief.sample(direction).elevation_m - terrain.sea_level_m;
            if (!first) {
                corner_height_m = std::max(corner_height_m, std::abs(height_m - reference));
                corner_position_m = std::max(corner_position_m,
                    length(direction - reference_direction) * mesh->radius_m());
                corner_flips += (height_m <= 0.0) != (reference <= 0.0);
            } else {
                reference = height_m;
                reference_direction = direction;
                first = false;
            }
            ++corners;
        }
    }
    std::cout << "seed " << seed << " L" << level << " all 24 oriented cube-face edges: " << samples
              << " paired vertex samples, " << coast_crossings << " paired sea-level crossings; "
              << flips << " class flips, " << unmatched_crossings << " unmatched crossings;"
              << " max height/position/crossing delta " << max_height_m << " m / "
              << max_position_m << " m / " << max_crossing_m << " m; " << corners
              << " corner samples across 8 three-face corners, " << corner_flips
              << " class flips, max height/position delta " << corner_height_m
              << " m / " << corner_position_m << " m\n";
    if (corners != 24 || flips || unmatched_crossings || corner_flips)
        throw std::runtime_error("cube-face seam classification is not consistent");

    // The probe-only fixed atlas samples texel *centres*, not the shared
    // cube-face edge. Test the actual nearest-texel classification on both
    // sides of all face borders before treating it as a shared mask.
    for (const int resolution : {512, 1024}) {
        std::vector<std::uint8_t> border(static_cast<std::size_t>(6 * 4 * resolution));
        std::vector<std::uint8_t> pinned(border.size());
        const auto index = [resolution](int face, int edge, int j) {
            return static_cast<std::size_t>((face * 4 + edge) * resolution + j);
        };
        for (int face = 0; face < 6; ++face) {
            for (int edge = 0; edge < 4; ++edge) {
                for (int j = 0; j < resolution; ++j) {
                    const double t = -1.0 + 2.0 * (j + 0.5) / resolution;
                    const double fixed = (edge == 0 || edge == 2 ? -1.0 : 1.0) *
                                         (1.0 - 1.0 / resolution);
                    const Vec3d direction = (edge < 2) ? tile_face_direction(face, fixed, t) :
                                                              tile_face_direction(face, t, fixed);
                    border[index(face, edge, j)] = static_cast<std::uint8_t>(
                        relief.sample(direction).elevation_m <= terrain.sea_level_m);
                    pinned[index(face, edge, j)] = static_cast<std::uint8_t>(
                        relief.sample(edge_direction(static_cast<std::uint8_t>(face), edge, t))
                                  .elevation_m <= terrain.sea_level_m);
                }
            }
        }
        std::size_t disagreements = 0, runs = 0, largest_run = 0, pinned_disagreements = 0;
        for (std::uint8_t face = 0; face < 6; ++face) {
            for (int edge = 0; edge < 4; ++edge) {
                const std::uint8_t other = neighbour(face, edge);
                const auto midpoint = face_coordinates(other, edge_direction(face, edge, 0.0));
                const bool fixed_u = std::abs(midpoint[0]) > std::abs(midpoint[1]);
                const double fixed = fixed_u ? midpoint[0] : midpoint[1];
                const int other_edge = fixed_u ? (fixed > 0.0 ? 1 : 0) : (fixed > 0.0 ? 3 : 2);
                const auto endpoint = face_coordinates(other, edge_direction(face, edge, 1.0));
                const bool reversed = (fixed_u ? endpoint[1] : endpoint[0]) < 0.0;
                std::size_t current_run = 0;
                for (int j = 0; j < resolution; ++j) {
                    const int other_j = reversed ? resolution - 1 - j : j;
                    const bool different = border[index(face, edge, j)] !=
                                           border[index(other, other_edge, other_j)];
                    disagreements += different;
                    pinned_disagreements += pinned[index(face, edge, j)] !=
                                            pinned[index(other, other_edge, other_j)];
                    current_run = different ? current_run + 1 : 0;
                    if (different && current_run == 1) ++runs;
                    largest_run = std::max(largest_run, current_run);
                }
            }
        }
        std::size_t corner_conflicts = 0;
        for (int face = 0; face < 6; ++face) {
            corner_conflicts += pinned[index(face, 0, 0)] != pinned[index(face, 2, 0)];
            corner_conflicts += pinned[index(face, 1, 0)] != pinned[index(face, 2, resolution - 1)];
            corner_conflicts += pinned[index(face, 0, resolution - 1)] != pinned[index(face, 3, 0)];
            corner_conflicts += pinned[index(face, 1, resolution - 1)] !=
                                pinned[index(face, 3, resolution - 1)];
        }
        std::cout << "seed " << seed << " L" << level << " fixed face atlas " << resolution << "²: " << disagreements
                  << '/' << 24 * resolution << " oriented border texels disagree in " << runs
                  << " runs (longest " << largest_run << " texels); counts include each"
                  << " physical seam twice; experimental exact-edge pinning "
                  << pinned_disagreements << " paired disagreements, "
                  << corner_conflicts << " within-face corner ownership conflicts\n";
    }
}
} // namespace

int main() {
    for (const std::uint64_t seed : {20260928ULL, 42ULL}) {
        audit(4, seed);
        audit(6, seed);
    }
}
