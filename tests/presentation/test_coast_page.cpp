#include "sim/presentation/coast_page.hpp"

#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace planetsim;
using namespace planetsim::presentation;

int main() {
    planetsim::test::Context test;
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
    const CoastPage coastal = make_coast_page(coastal_key, field);
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
    std::cout << "L4 coastal page generation " << generation_ms << " ms (cold rank tables), midpoint flips "
              << centre_flips
              << "/4096, max signed-field error " << max_bilinear_error_m << " m\n";
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
    return test.result();
}
