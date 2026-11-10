#include "sim/presentation/coast_page.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

// ADR-0030 A1.4: a terrain tile drawn with its own coast page follows the
// page's classification, and is unchanged without one.
using namespace planetsim;
using namespace planetsim::presentation;

namespace {

[[nodiscard]] Vec3d absolute(const TileGeometry& tile, std::size_t vertex) {
    return tile.origin + Vec3d{tile.positions[3 * vertex], tile.positions[3 * vertex + 1], tile.positions[3 * vertex + 2]};
}

[[nodiscard]] bool same_bytes(const TileGeometry& a, const TileGeometry& b) {
    return a.positions == b.positions && a.normals == b.normals && a.cell_texel == b.cell_texel &&
           a.corner_texel == b.corner_texel && a.custom0 == b.custom0 && a.custom1 == b.custom1 &&
           a.coast_uv == b.coast_uv;
}

void check(test::Context& test, const TileKey& key, const TerrainRelief& relief, const CoastWaterField& field,
           const TileDrawing& drawing) {
    const CoastPage page = make_coast_page(key, field, 8U);
    const TileGeometry plain = make_terrain_tile(key, relief, drawing);
    const TileGeometry tile = make_terrain_tile(key, relief, drawing, &page, &field);
    PLANETSIM_EXPECT(test, plain.coast_uv.empty());
    PLANETSIM_EXPECT(test, same_bytes(plain, make_terrain_tile(key, relief, drawing)));
    PLANETSIM_EXPECT(test, same_bytes(tile, make_terrain_tile(key, relief, drawing, &page, &field)));
    const std::size_t vertices = tile.positions.size() / 3U;
    PLANETSIM_EXPECT(test, vertices == plain.positions.size() / 3U);
    PLANETSIM_EXPECT(test, tile.coast_uv.size() == 2U * vertices);

    // Every surface vertex lies on a page node and follows its sign.
    const std::size_t surface = 6U * tile_grid * tile_grid;
    int off_node = 0;
    int wrong_side = 0;
    int water = 0;
    int changed = 0;
    for (std::size_t v = 0; v < surface; ++v) {
        const double fx = tile.coast_uv[2 * v] * coast_page_intervals;
        const double fy = tile.coast_uv[2 * v + 1] * coast_page_intervals;
        const auto nx = static_cast<std::uint32_t>(std::lround(fx));
        const auto ny = static_cast<std::uint32_t>(std::lround(fy));
        off_node += std::abs(fx - nx) > 1e-4 || std::abs(fy - ny) > 1e-4 || nx % 8U != 0U || ny % 8U != 0U ? 1 : 0;
        const bool wet = page.signed_water_m(std::min(nx, coast_page_intervals), std::min(ny, coast_page_intervals)) > 0.0;
        const double height = tile.custom1[4 * v + 3];
        const double radius = length(absolute(tile, v));
        if (wet) {
            ++water;
            wrong_side += height > 0.0 || std::abs(radius - 1.0) > 1e-6 ? 1 : 0;
        } else {
            wrong_side += height < 0.0 ? 1 : 0;
        }
        // Where the plain tile's own height disagreed with the page.
        changed += (plain.custom1[4 * v + 3] > 0.0) == wet ? 1 : 0;
    }
    PLANETSIM_EXPECT(test, off_node == 0);
    PLANETSIM_EXPECT(test, wrong_side == 0);
    PLANETSIM_EXPECT(test, water > 0 && water < static_cast<int>(surface));

    // The neighbour, with its own page, shares the edge: the same points and
    // normals (to the float rounding of the two tiles' origins).
    const TileKey right{key.face, key.level, key.x ^ 1U, key.y};
    const CoastPage right_page = make_coast_page(right, field, 8U);
    const TileGeometry beside = make_terrain_tile(right, relief, drawing, &right_page, &field);
    const float mine = key.x < right.x ? 1.0F : 0.0F;
    double worst_position = 0.0;
    double worst_normal = 0.0;
    int shared = 0;
    for (std::size_t a = 0; a < surface; ++a) {
        if (tile.coast_uv[2 * a] != mine) continue;
        for (std::size_t b = 0; b < surface; ++b) {
            if (beside.coast_uv[2 * b] != 1.0F - mine || beside.coast_uv[2 * b + 1] != tile.coast_uv[2 * a + 1]) continue;
            ++shared;
            worst_position = std::max(worst_position, length(absolute(tile, a) - absolute(beside, b)));
            const Vec3d na{tile.normals[3 * a], tile.normals[3 * a + 1], tile.normals[3 * a + 2]};
            const Vec3d nb{beside.normals[3 * b], beside.normals[3 * b + 1], beside.normals[3 * b + 2]};
            worst_normal = std::max(worst_normal, length(na - nb));
            break;
        }
    }
    std::cout << "tile face " << int(key.face) << " L" << int(key.level) << " (" << key.x << ", " << key.y << "), "
              << (page.kind == CoastPageKind::coverage ? "coverage" : "point") << " page: " << water << " of "
              << surface << " surface vertices water, " << changed << " where the plain tile's height disagreed; " << shared
              << " shared edge vertices, worst position " << worst_position << ", normal " << worst_normal << '\n';
    PLANETSIM_EXPECT(test, shared > 0);
    PLANETSIM_EXPECT(test, worst_position < 1e-6);
    PLANETSIM_EXPECT(test, worst_normal < 1e-5);
}

// A tile drawn from an ancestor's page, `up` levels up (A1.3: while its own
// loads): its vertices follow the ancestor's bilinear value, their
// coast_uv is their place in that page, and up to three levels up every
// vertex is an ancestor node.
void ancestor(test::Context& test, const TileKey& key, int up, const TerrainRelief& relief,
              const CoastWaterField& field, const TileDrawing& drawing) {
    TileKey page_key = key;
    for (int k = 0; k < up; ++k) page_key = tile_parent(page_key);
    const CoastPage page = make_coast_page(page_key, field, 8U);
    const TileGeometry tile = make_terrain_tile(key, relief, drawing, &page, &field);
    PLANETSIM_EXPECT(test, tile.coast_key == page_key);
    const std::size_t surface = 6U * tile_grid * tile_grid;
    int wrong_side = 0;
    int off_node = 0;
    for (std::size_t v = 0; v < surface; ++v) {
        const double fx = tile.coast_uv[2 * v] * coast_page_intervals;
        const double fy = tile.coast_uv[2 * v + 1] * coast_page_intervals;
        off_node += std::abs(fx - std::round(fx)) > 1e-4 || std::abs(fy - std::round(fy)) > 1e-4 ? 1 : 0;
        const bool wet = page.bilinear_signed_water_m(fx, fy) > 0.0;
        const double height = tile.custom1[4 * v + 3];
        wrong_side += wet ? (height > 0.0 ? 1 : 0) : (height < 0.0 ? 1 : 0);
    }
    std::cout << "tile L" << int(key.level) << " from its ancestor " << up << " levels up: " << off_node
              << " vertices off a node, " << wrong_side << " on the wrong side\n";
    PLANETSIM_EXPECT(test, wrong_side == 0);
    if (up <= 3) PLANETSIM_EXPECT(test, off_node == 0);
    // Its own page, or no page: the tile's key.
    const CoastPage own = make_coast_page(key, field, 8U);
    PLANETSIM_EXPECT(test, make_terrain_tile(key, relief, drawing, &own, &field).coast_key == key);
}

}  // namespace

int main() {
    test::Context test;
    Scenario scenario;
    scenario.seed = 20260928;
    scenario.subdivision = 4;
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);
    TileDrawing drawing;
    drawing.sea_level_m = terrain.sea_level_m;
    drawing.radius_m = run.parameters().radius_m;
    // A detailed coastal tile and a filtered one (R1-05's probes' tiles).
    check(test, TileKey{5, 2, 3, 0}, relief, field, drawing);
    check(test, TileKey{5, 1, 0, 1}, relief, field, drawing);
    // From an ancestor's page, one, three and five levels up.
    const TileKey deep{5, 5, 24, 1};
    ancestor(test, deep, 1, relief, field, drawing);
    ancestor(test, deep, 3, relief, field, drawing);
    ancestor(test, deep, 5, relief, field, drawing);
    // A page must be the tile's own or an ancestor's, with its field.
    const CoastPage other = make_coast_page(TileKey{5, 2, 2, 0}, field, 8U);
    bool rejected = false;
    try { (void)make_terrain_tile(TileKey{5, 2, 3, 0}, relief, drawing, &other, &field); }
    catch (const std::invalid_argument&) { rejected = true; }
    PLANETSIM_EXPECT(test, rejected);
    return test.result();
}
