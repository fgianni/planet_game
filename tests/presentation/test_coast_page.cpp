#include "sim/presentation/coast_page.hpp"

#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>

// ADR-0030 Amendment 1, V-A1 (the canonical water field) and V-A2 (the coast
// pages), headless.
namespace {

using planetsim::CellId;
using planetsim::Vec3d;
using planetsim::presentation::CoastField;
using planetsim::presentation::CoastPage;
using planetsim::presentation::TileKey;
using planetsim::presentation::coast_page_size;

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::TerrainSnapshot terrain;
    std::unique_ptr<planetsim::presentation::TerrainRelief> relief;
    std::unique_ptr<CoastField> field;
};

[[nodiscard]] Planet make_planet(std::uint32_t level, std::uint64_t seed) {
    planetsim::Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = seed;
    const planetsim::PlanetRun run(scenario);
    Planet planet;
    planet.mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, run.parameters().radius_m));
    planet.terrain = run.terrain_snapshot();
    planet.relief = std::make_unique<planetsim::presentation::TerrainRelief>(planet.mesh, planet.terrain, seed);
    planet.field = std::make_unique<CoastField>(*planet.relief, planet.terrain);
    return planet;
}

[[nodiscard]] Vec3d fibonacci(std::size_t i, std::size_t n) {
    const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
    const double z = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(n);
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    return {r * std::cos(golden * static_cast<double>(i)), r * std::sin(golden * static_cast<double>(i)), z};
}

void half_floats(planetsim::test::Context& test) {
    using planetsim::presentation::half_from_float;
    using planetsim::presentation::half_to_float;
    // Every finite half converts to a float and back to itself.
    int mismatches = 0;
    for (std::uint32_t bits = 0; bits <= 0xffffU; ++bits) {
        if (((bits >> 10U) & 0x1fU) == 31U) continue;
        mismatches += half_from_float(half_to_float(static_cast<std::uint16_t>(bits))) != bits ? 1 : 0;
    }
    PLANETSIM_EXPECT(test, mismatches == 0);
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(65504.0F)) == 65504.0F);
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(-1.5F)) == -1.5F);
    // Ties to even: 1 + 2^-11 is half way between 1 and 1 + 2^-10.
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(1.0F + std::ldexp(1.0F, -11))) == 1.0F);
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(1.0F + 3.0F * std::ldexp(1.0F, -11))) ==
                               1.0F + std::ldexp(1.0F, -9));
    // A sign survives rounding: the coast's side never flips.
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(1e-3F)) > 0.0F);
    PLANETSIM_EXPECT(test, half_to_float(half_from_float(-1e-3F)) < 0.0F);
}

// V-A1 where there is no depression: s is the plain contour, exactly.
void plain_contour(planetsim::test::Context& test, const Planet& planet) {
    PLANETSIM_EXPECT(test, planet.field->depression_cell_count() == 0U);
    PLANETSIM_EXPECT(test, planet.field->depression_depth_m() == 0.0);
    int mismatches = 0;
    CellId hint{0};
    for (std::size_t i = 0; i < 4000U; ++i) {
        const Vec3d d = fibonacci(i, 4000U);
        const auto s = planet.field->sample(d, hint);
        hint = s.cell;
        mismatches += s.water_m != planet.terrain.sea_level_m - planet.relief->sample(d, hint).elevation_m ? 1 : 0;
    }
    PLANETSIM_EXPECT(test, mismatches == 0);
}

// V-A1 with depressions: their own ground is dry, κ is in [0, 1], s is the
// plain contour where κ = 1, and s is continuous across the cells' edges.
void depressions(planetsim::test::Context& test, const Planet& planet) {
    const CoastField& field = *planet.field;
    const auto& mesh = *planet.mesh;
    PLANETSIM_EXPECT(test, field.depression_cell_count() > 0U);
    PLANETSIM_EXPECT(test, field.depression_depth_m() > 1.0);
    int wet = 0;
    int checked = 0;
    for (const auto& cell : mesh.cells()) {
        if (!field.is_depression(cell.id)) continue;
        // The centre and half way to each corner: the cell's own curve.
        const auto corners = mesh.cell_corners(cell.id);
        wet += field.sample(cell.center_unit, cell.id).water_m > 0.0 ? 1 : 0;
        for (const auto corner : corners) {
            const Vec3d half = planetsim::normalized(cell.center_unit + mesh.corners_unit()[corner]);
            wet += field.sample(half, cell.id).water_m > 0.0 ? 1 : 0;
            ++checked;
        }
    }
    PLANETSIM_EXPECT(test, checked > 0);
    PLANETSIM_EXPECT(test, wet == 0);

    int outside = 0;
    int unequal = 0;
    double lowest_kappa = 1.0;
    double highest_kappa = 0.0;
    CellId hint{0};
    for (std::size_t i = 0; i < 20000U; ++i) {
        const Vec3d d = fibonacci(i, 20000U);
        const double kappa = field.connectivity(d, hint);
        lowest_kappa = std::min(lowest_kappa, kappa);
        highest_kappa = std::max(highest_kappa, kappa);
        const auto s = field.sample(d, hint);
        hint = s.cell;
        outside += kappa < 0.0 || kappa > 1.0 ? 1 : 0;
        if (kappa == 1.0) unequal += s.water_m != field.sea_level_m() - s.elevation_m ? 1 : 0;
    }
    std::cout.precision(17);
    std::cout << "connectivity range " << lowest_kappa << ".." << highest_kappa
              << ", out of range " << outside << '\n';
    std::cout.precision(6);
    PLANETSIM_EXPECT(test, outside == 0);
    PLANETSIM_EXPECT(test, unequal == 0);

    // Continuity: from depression cells to each neighbour, in steps of about
    // 3 m; the relief's steepest slope (INV R1-05: under 0.7 m/m) bounds a
    // step's change near 2 m, while a jump in κ would be of order D.
    double worst = 0.0;
    int walks = 0;
    for (const auto& cell : mesh.cells()) {
        if (!field.is_depression(cell.id) || walks >= 24) continue;
        for (const auto& edge : mesh.cell_edges(cell.id)) {
            const Vec3d a = cell.center_unit;
            const Vec3d b = mesh.cell(edge.neighbor).center_unit;
            constexpr int steps = 40000;
            double previous = field.sample(a, cell.id).water_m;
            CellId walk_hint = cell.id;
            for (int k = 1; k <= steps; ++k) {
                const double t = static_cast<double>(k) / steps;
                const auto s = field.sample(planetsim::normalized(a * (1.0 - t) + b * t), walk_hint);
                walk_hint = s.cell;
                worst = std::max(worst, std::abs(s.water_m - previous));
                previous = s.water_m;
            }
            ++walks;
        }
    }
    std::cout << "depressions " << field.depression_cell_count() << ", D " << field.depression_depth_m()
              << " m, checked " << checked << " dry points, " << walks << " edge walks, largest step change "
              << worst << " m\n";
    PLANETSIM_EXPECT(test, walks > 0);
    PLANETSIM_EXPECT(test, worst < 10.0);
}

// The face, u and v of a direction on a given face (the inverse of the
// equal-angle mapping), or false if the direction is not on that face.
bool on_face(std::uint8_t face, const Vec3d& d, double& u, double& v) {
    // Recover the face frame from tile_face_direction itself.
    const Vec3d n = planetsim::presentation::tile_face_direction(face, 0.0, 0.0);
    const Vec3d along_u = planetsim::presentation::tile_face_direction(face, 0.5, 0.0);
    const Vec3d along_v = planetsim::presentation::tile_face_direction(face, 0.0, 0.5);
    const double depth = dot(d, n);
    if (depth <= 0.0) return false;
    const Vec3d p = d / depth;   // on the face plane: n + U u_axis + V v_axis
    const Vec3d u_axis = planetsim::normalized(along_u / dot(along_u, n) - n);
    const Vec3d v_axis = planetsim::normalized(along_v / dot(along_v, n) - n);
    const double big_u = dot(p, u_axis);
    const double big_v = dot(p, v_axis);
    if (std::abs(big_u) > 1.0 + 1e-12 || std::abs(big_v) > 1.0 + 1e-12) return false;
    u = std::atan(big_u) * 4.0 / std::numbers::pi;
    v = std::atan(big_v) * 4.0 / std::numbers::pi;
    return true;
}

// V-A2: pages are deterministic and agree exactly at shared points.
void pages(planetsim::test::Context& test, const Planet& planet) {
    using planetsim::presentation::make_coast_page;
    const CoastField& field = *planet.field;
    // A coastal level-2 key, found by a fixed coarse scan.
    TileKey coastal{};
    int best = -1;
    for (std::uint8_t face = 0; face < 6U; ++face) {
        for (std::uint32_t y = 0; y < 4U; ++y) {
            for (std::uint32_t x = 0; x < 4U; ++x) {
                const TileKey key{face, 2U, x, y};
                int crossings = 0;
                bool previous = false;
                for (std::uint32_t k = 0; k <= 64U; ++k) {
                    const bool wet = field.sample(planetsim::presentation::coast_page_direction(key, k * 4U, k * 4U)).water_m > 0.0;
                    crossings += k > 0U && wet != previous ? 1 : 0;
                    previous = wet;
                }
                if (crossings > best) {
                    best = crossings;
                    coastal = key;
                }
            }
        }
    }
    const auto start = std::chrono::steady_clock::now();
    const CoastPage page = make_coast_page(coastal, field);
    const double page_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    PLANETSIM_EXPECT(test, page.water_half == make_coast_page(coastal, field).water_half);

    // Parent and children: every other child point is the parent's.
    int child_mismatches = 0;
    const auto children = planetsim::presentation::tile_children(coastal);
    for (std::size_t c = 0; c < 4U; ++c) {
        const CoastPage child = make_coast_page(children[c], field);
        const std::uint32_t ox = (children[c].x & 1U) * (coast_page_size / 2U);
        const std::uint32_t oy = (children[c].y & 1U) * (coast_page_size / 2U);
        for (std::uint32_t l = 0; l <= coast_page_size; l += 2U) {
            for (std::uint32_t k = 0; k <= coast_page_size; k += 2U) {
                child_mismatches += child.bits(k, l) != page.bits(ox + k / 2U, oy + l / 2U) ? 1 : 0;
            }
        }
    }
    PLANETSIM_EXPECT(test, child_mismatches == 0);

    // Same-face neighbours share an edge.
    int edge_mismatches = 0;
    const TileKey right{coastal.face, coastal.level, coastal.x ^ 1U, coastal.y};
    const CoastPage beside = make_coast_page(right, field);
    const std::uint32_t mine = coastal.x < right.x ? coast_page_size : 0U;
    for (std::uint32_t j = 0; j <= coast_page_size; ++j) {
        edge_mismatches += page.bits(mine, j) != beside.bits(coast_page_size - mine, j) ? 1 : 0;
    }
    PLANETSIM_EXPECT(test, edge_mismatches == 0);

    // Across cube faces: every level-0 face edge, matched to the page of the
    // adjacent face that holds the same point.
    std::array<CoastPage, 6> faces;
    for (std::uint8_t face = 0; face < 6U; ++face) faces[face] = make_coast_page({face, 0U, 0U, 0U}, field);
    int face_points = 0;
    int face_mismatches = 0;
    int unmatched = 0;
    for (std::uint8_t face = 0; face < 6U; ++face) {
        for (std::uint32_t k = 0; k <= coast_page_size; ++k) {
            const std::array<std::array<std::uint32_t, 2>, 4> edge{{{0U, k}, {coast_page_size, k}, {k, 0U}, {k, coast_page_size}}};
            for (const auto& [i, j] : edge) {
                const Vec3d d = planetsim::presentation::coast_page_direction({face, 0U, 0U, 0U}, i, j);
                bool found = false;
                for (std::uint8_t other = 0; other < 6U && !found; ++other) {
                    if (other == face) continue;
                    double u = 0.0;
                    double v = 0.0;
                    if (!on_face(other, d, u, v)) continue;
                    const double fi = (u + 1.0) / 2.0 * coast_page_size;
                    const double fj = (v + 1.0) / 2.0 * coast_page_size;
                    const auto ri = static_cast<std::uint32_t>(std::lround(fi));
                    const auto rj = static_cast<std::uint32_t>(std::lround(fj));
                    if (std::abs(fi - ri) > 1e-6 || std::abs(fj - rj) > 1e-6) continue;
                    if (ri != 0U && ri != coast_page_size && rj != 0U && rj != coast_page_size) continue;
                    found = true;
                    ++face_points;
                    face_mismatches += faces[other].bits(ri, rj) != faces[face].bits(i, j) ? 1 : 0;
                }
                unmatched += found ? 0 : 1;
            }
        }
    }
    PLANETSIM_EXPECT(test, unmatched == 0);
    PLANETSIM_EXPECT(test, face_mismatches == 0);

    // The bilinear reading against the field itself, inside the coastal page
    // (a measurement: the screen-space gate is task 2's).
    int disagreements = 0;
    constexpr int probes = 20000;
    CellId hint{0};
    for (int k = 0; k < probes; ++k) {
        const double fi = std::fmod(0.5 + 0.7548776662466927 * (k + 1), 1.0) * coast_page_size;
        const double fj = std::fmod(0.5 + 0.5698402909980532 * (k + 1), 1.0) * coast_page_size;
        const double u = planetsim::presentation::coast_page_coordinate(coastal.x, coastal.level, 0U) +
                         2.0 * fi / (coast_page_size * std::ldexp(1.0, coastal.level));
        const double v = planetsim::presentation::coast_page_coordinate(coastal.y, coastal.level, 0U) +
                         2.0 * fj / (coast_page_size * std::ldexp(1.0, coastal.level));
        const auto s = field.sample(planetsim::presentation::tile_face_direction(coastal.face, u, v), hint);
        hint = s.cell;
        disagreements += (page.interpolated_water_m(fi, fj) > 0.0) != (s.water_m > 0.0) ? 1 : 0;
    }
    std::cout << "coastal page face " << int(coastal.face) << " L" << int(coastal.level) << " (" << coastal.x << ", "
              << coastal.y << "), " << best << " diagonal crossings, " << page_s << " s; child and neighbour edges "
              << "agree; " << face_points << " face-edge points agree; bilinear vs field "
              << disagreements << " of " << probes << '\n';
    PLANETSIM_EXPECT(test, disagreements * 100 < probes);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    half_floats(test);
    const Planet l4 = make_planet(4U, 20260928U);
    plain_contour(test, l4);
    pages(test, l4);
    const Planet l6 = make_planet(6U, 20260928U);
    depressions(test, l6);
    return test.result();
}
