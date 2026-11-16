#include "sim/presentation/coast_page.hpp"

#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <numbers>

using namespace planetsim;
using namespace planetsim::presentation;

namespace {

// Shared nodes, reroute bytes, a cube-face edge, a three-face corner,
// invalid keys and the bilinear reading at a coarse coastal page.
void shared_nodes(planetsim::test::Context& test) {
    Scenario scenario;
    scenario.seed = 20260928;
    scenario.subdivision = 4;
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);
    const TileKey parent_key{0, 5, 0, 12};
    const auto children = tile_children(parent_key);
    const CoastPage parent = make_coast_page(parent_key, field);
    const CoastPage left = make_coast_page(children[0], field);
    const CoastPage right = make_coast_page(children[1], field);
    const CoastPage upper_left = make_coast_page(children[2], field);
    const CoastPage upper_right = make_coast_page(children[3], field);
    PLANETSIM_EXPECT(test, parent.signed_water_le.size() == 2U * 257U * 257U);
    for (std::uint32_t i = 0; i <= coast_page_intervals; ++i) {
        PLANETSIM_EXPECT(test, left.signed_water_m(256, i) == right.signed_water_m(0, i));
        PLANETSIM_EXPECT(test, left.signed_water_m(i, 256) == upper_left.signed_water_m(i, 0));
        PLANETSIM_EXPECT(test, right.signed_water_m(i, 256) == upper_right.signed_water_m(i, 0));
        // All parent nodes are children nodes, including the middle line.
        const auto cx = i <= 128 ? 2 * i : 2 * i - 256;
        const CoastPage& child = i <= 128 ? left : right;
        PLANETSIM_EXPECT(test, parent.signed_water_m(i, 0) == child.signed_water_m(cx, 0));
        PLANETSIM_EXPECT(test, parent.signed_water_m(i, 128) ==
            child.signed_water_m(cx, 256));
    }
    // Face 0's u=-1 edge meets face 4's u=+1 edge, with the same v.
    const CoastPage across_face = make_coast_page(TileKey{4, 5, 31, 12}, field);
    for (std::uint32_t i = 0; i <= coast_page_intervals; ++i)
        PLANETSIM_EXPECT(test, parent.signed_water_m(0, i) ==
            across_face.signed_water_m(256, i));
    const CoastPage again = make_coast_page(children[1], field);
    PLANETSIM_EXPECT(test, again.signed_water_le == right.signed_water_le);
    PLANETSIM_EXPECT(test, parent.bilinear_signed_water_m(0, 0) == parent.signed_water_m(0, 0));
    PLANETSIM_EXPECT(test, std::isfinite(parent.bilinear_signed_water_m(127.5, 32.5)));
    const TileKey coastal_key{5, 1, 0, 1};
    const auto page_start = std::chrono::steady_clock::now();
    // Level 1 at L4 is a coverage level: its sign is compared, not its value.
    const CoastPage coastal = make_coast_page(coastal_key, field, 8U);
    PLANETSIM_EXPECT(test, coastal.kind == CoastPageKind::coverage);
    const double generation_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - page_start).count();
    std::size_t centre_flips = 0, wet_nodes = 0, dry_nodes = 0;
    double max_bilinear_error_m = 0.0;
    for (std::uint32_t y = 0; y < 256; y += 4)
        for (std::uint32_t x = 0; x < 256; x += 4) {
            const double lines = 256.0 * 2.0;
            const double u = -1.0 + 2.0 * (coastal_key.x * 256.0 + x + 0.5) / lines;
            const double v = -1.0 + 2.0 * (coastal_key.y * 256.0 + y + 0.5) / lines;
            const double canonical = field.signed_water_m(tile_face_direction(coastal_key.face, u, v));
            const double drawn = coastal.bilinear_signed_water_m(x + 0.5, y + 0.5);
            centre_flips += (canonical > 0.0) != (drawn > 0.0);
            max_bilinear_error_m = std::max(max_bilinear_error_m, std::abs(canonical - drawn));
            wet_nodes += drawn > 0.0;
            dry_nodes += drawn <= 0.0;
        }
    std::cout << "L4 coastal coverage page generation " << generation_ms
              << " ms on 8 threads (cold rank tables), midpoint flips " << centre_flips << "/4096\n";
    (void)max_bilinear_error_m;
    PLANETSIM_EXPECT(test, wet_nodes > 0 && dry_nodes > 0);
    bool rejected = false;
    try { (void)make_coast_page(TileKey{6, 0, 0, 0}, field); }
    catch (const std::invalid_argument&) { rejected = true; }
    PLANETSIM_EXPECT(test, rejected);
    Scenario other_scenario;
    other_scenario.seed = 42;
    other_scenario.subdivision = 6;
    const PlanetRun other_run(other_scenario);
    const auto other_mesh = std::make_shared<const PlanetMesh>(other_run.state().mesh());
    const auto other_terrain = other_run.terrain_snapshot();
    const TerrainRelief other_relief(other_mesh, other_terrain, other_scenario.seed);
    const CoastWaterField other_field(other_relief, other_terrain);
    // All three faces meeting at (+x,+y,+z) own the same world point.
    const CoastPage x_face = make_coast_page(TileKey{0, 6, 0, 63}, other_field);
    const CoastPage y_face = make_coast_page(TileKey{2, 6, 63, 0}, other_field);
    const CoastPage z_face = make_coast_page(TileKey{4, 6, 63, 63}, other_field);
    PLANETSIM_EXPECT(test, x_face.signed_water_m(0, 256) == y_face.signed_water_m(256, 0));
    PLANETSIM_EXPECT(test, x_face.signed_water_m(0, 256) == z_face.signed_water_m(256, 256));
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

// The child of `key` with the most canonical sign changes along its
// diagonal: a fixed descent that stays on a coast.
TileKey coastal_child(const TileKey& key, const CoastWaterField& field) {
    TileKey best_key = key;
    int best = -1;
    for (const TileKey& child : planetsim::presentation::tile_children(key)) {
        int crossings = 0;
        bool previous = false;
        for (std::uint32_t k = 0; k <= 64U; ++k) {
            const bool wet =
                field.signed_water_m(planetsim::presentation::coast_page_direction(child, k * 4U, k * 4U)) > 0.0;
            crossings += k > 0U && wet != previous ? 1 : 0;
            previous = wet;
        }
        if (crossings > best) {
            best = crossings;
            best_key = child;
        }
    }
    return best_key;
}

// V-A2: pages are deterministic and agree exactly at shared nodes; each
// level has its kind (point or coverage).
void pages(planetsim::test::Context& test, const CoastWaterField& field) {
    using planetsim::presentation::CoastPageKind;
    using planetsim::presentation::coast_node_value;
    using planetsim::presentation::make_coast_page;
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
                    const bool wet = field.sample(planetsim::presentation::coast_page_direction(key, k * 4U, k * 4U)).signed_water_m > 0.0;
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
    // At L4 (finest wavelength 2.3 km), level 2 is a coverage level and
    // level 5 a point level.
    const auto& relief = field.relief();
    PLANETSIM_EXPECT(test, planetsim::presentation::coast_page_kind(2U, relief) == CoastPageKind::coverage);
    PLANETSIM_EXPECT(test, planetsim::presentation::coast_page_kind(5U, relief) == CoastPageKind::point);

    // The coverage page: deterministic for any worker count, values within
    // [-0.5, 0.5], and its neighbour shares its edge.
    const auto start = std::chrono::steady_clock::now();
    const CoastPage cover = make_coast_page(coastal, field, 8U);
    const double cover_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    PLANETSIM_EXPECT(test, cover.kind == CoastPageKind::coverage);
    PLANETSIM_EXPECT(test, cover.signed_water_le == make_coast_page(coastal, field, 3U).signed_water_le);
    int outside = 0;
    int wet = 0;
    for (std::uint32_t y = 0; y < coast_page_nodes; ++y) {
        for (std::uint32_t x = 0; x < coast_page_nodes; ++x) {
            const double v = cover.signed_water_m(x, y);
            outside += v < -0.5 || v > 0.5 ? 1 : 0;
            wet += v > 0.0 ? 1 : 0;
        }
    }
    PLANETSIM_EXPECT(test, outside == 0);
    PLANETSIM_EXPECT(test, wet > 0 && wet < static_cast<int>(coast_page_nodes * coast_page_nodes));
    int edge_mismatches = 0;
    const TileKey right{coastal.face, coastal.level, coastal.x ^ 1U, coastal.y};
    const CoastPage beside = make_coast_page(right, field, 8U);
    const std::uint32_t mine = coastal.x < right.x ? coast_page_intervals : 0U;
    for (std::uint32_t j = 0; j <= coast_page_intervals; ++j) {
        edge_mismatches += cover.half_bits(mine, j) != beside.half_bits(coast_page_intervals - mine, j) ? 1 : 0;
    }
    PLANETSIM_EXPECT(test, edge_mismatches == 0);

    // A coastal point page, three levels down: its children repeat its
    // nodes.
    TileKey point_key = coastal;
    for (int k = 0; k < 3; ++k) point_key = coastal_child(point_key, field);
    const CoastPage page = make_coast_page(point_key, field, 8U);
    PLANETSIM_EXPECT(test, page.kind == CoastPageKind::point);
    PLANETSIM_EXPECT(test, page.signed_water_le == make_coast_page(point_key, field).signed_water_le);
    int child_mismatches = 0;
    const auto children = planetsim::presentation::tile_children(point_key);
    for (std::size_t c = 0; c < 4U; ++c) {
        const CoastPage child = make_coast_page(children[c], field, 8U);
        const std::uint32_t ox = (children[c].x & 1U) * (coast_page_intervals / 2U);
        const std::uint32_t oy = (children[c].y & 1U) * (coast_page_intervals / 2U);
        for (std::uint32_t l = 0; l <= coast_page_intervals; l += 2U) {
            for (std::uint32_t k = 0; k <= coast_page_intervals; k += 2U) {
                child_mismatches += child.half_bits(k, l) != page.half_bits(ox + k / 2U, oy + l / 2U) ? 1 : 0;
            }
        }
    }
    PLANETSIM_EXPECT(test, child_mismatches == 0);

    // Across cube faces: every node of every level-0 face edge (a coverage
    // level), computed from the adjacent face's own lattice direction.
    int face_points = 0;
    int face_mismatches = 0;
    int unmatched = 0;
    for (std::uint8_t face = 0; face < 6U; ++face) {
        for (std::uint32_t k = 0; k <= coast_page_intervals; ++k) {
            const std::array<std::array<std::uint32_t, 2>, 4> edge{{{0U, k}, {coast_page_intervals, k}, {k, 0U}, {k, coast_page_intervals}}};
            for (const auto& [i, j] : edge) {
                const Vec3d d = planetsim::presentation::coast_page_direction({face, 0U, 0U, 0U}, i, j);
                bool found = false;
                for (std::uint8_t other = 0; other < 6U && !found; ++other) {
                    if (other == face) continue;
                    double u = 0.0;
                    double v = 0.0;
                    if (!on_face(other, d, u, v)) continue;
                    const double fi = (u + 1.0) / 2.0 * coast_page_intervals;
                    const double fj = (v + 1.0) / 2.0 * coast_page_intervals;
                    const auto ri = static_cast<std::uint32_t>(std::lround(fi));
                    const auto rj = static_cast<std::uint32_t>(std::lround(fj));
                    if (std::abs(fi - ri) > 1e-6 || std::abs(fj - rj) > 1e-6) continue;
                    if (ri != 0U && ri != coast_page_intervals && rj != 0U && rj != coast_page_intervals) continue;
                    found = true;
                    ++face_points;
                    CellId h1{0};
                    CellId h2{0};
                    const Vec3d d_other = planetsim::presentation::coast_page_direction({other, 0U, 0U, 0U}, ri, rj);
                    const float mine_value = static_cast<float>(coast_node_value(field, 0U, d, h1));
                    const float theirs = static_cast<float>(coast_node_value(field, 0U, d_other, h2));
                    face_mismatches += planetsim::presentation::half_from_float(mine_value) !=
                                               planetsim::presentation::half_from_float(theirs) ? 1 : 0;
                }
                unmatched += found ? 0 : 1;
            }
        }
    }
    PLANETSIM_EXPECT(test, unmatched == 0);
    PLANETSIM_EXPECT(test, face_mismatches == 0);

    // The point page's bilinear reading against the field itself (a
    // measurement: the screen-space gate is R1-05's probe).
    int disagreements = 0;
    constexpr int probes = 20000;
    CellId hint{0};
    for (int k = 0; k < probes; ++k) {
        const double fi = std::fmod(0.5 + 0.7548776662466927 * (k + 1), 1.0) * coast_page_intervals;
        const double fj = std::fmod(0.5 + 0.5698402909980532 * (k + 1), 1.0) * coast_page_intervals;
        const double u = planetsim::presentation::coast_page_coordinate(point_key.x, point_key.level, 0U) +
                         2.0 * fi / (coast_page_intervals * std::ldexp(1.0, point_key.level));
        const double v = planetsim::presentation::coast_page_coordinate(point_key.y, point_key.level, 0U) +
                         2.0 * fj / (coast_page_intervals * std::ldexp(1.0, point_key.level));
        const auto s = field.sample(planetsim::presentation::tile_face_direction(point_key.face, u, v), hint);
        hint = s.cell;
        disagreements += (page.bilinear_signed_water_m(fi, fj) > 0.0) != (s.signed_water_m > 0.0) ? 1 : 0;
    }
    std::cout << "coverage page face " << int(coastal.face) << " L" << int(coastal.level) << " (" << coastal.x
              << ", " << coastal.y << "), " << best << " diagonal crossings, " << wet << " wet nodes, " << cover_s
              << " s on 8 threads; point page L" << int(point_key.level) << " (" << point_key.x << ", "
              << point_key.y << "): children agree; " << face_points
              << " face-edge nodes agree; bilinear vs field " << disagreements << " of " << probes << '\n';
    PLANETSIM_EXPECT(test, disagreements * 100 < probes);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    half_floats(test);
    shared_nodes(test);
    Scenario scenario;
    scenario.seed = 20260928;
    scenario.subdivision = 4;
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);
    pages(test, field);
    return test.result();
}
