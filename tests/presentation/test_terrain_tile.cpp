#include "sim/presentation/terrain_tile.hpp"

#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>

namespace {

using planetsim::Vec3d;
using planetsim::presentation::TileGeometry;
using planetsim::presentation::TileKey;
using planetsim::presentation::tile_grid;

struct Fixture {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::TerrainSnapshot terrain;
    planetsim::presentation::TileDrawing drawing;
};

[[nodiscard]] Fixture make_fixture() {
    planetsim::Scenario scenario;
    scenario.subdivision = 4U;
    const planetsim::PlanetRun run(scenario);
    Fixture fixture;
    fixture.mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(scenario.subdivision, run.parameters().radius_m));
    fixture.terrain = run.terrain_snapshot();
    fixture.drawing.sea_level_m = fixture.terrain.sea_level_m;
    fixture.drawing.radius_m = run.parameters().radius_m;
    return fixture;
}

[[nodiscard]] std::size_t surface_vertex_count() { return 6U * tile_grid * tile_grid; }

[[nodiscard]] Vec3d absolute(const TileGeometry& tile, std::size_t vertex) {
    return tile.origin + Vec3d{tile.positions[3 * vertex], tile.positions[3 * vertex + 1],
                               tile.positions[3 * vertex + 2]};
}

// The surface point of grid line (i, j) of a tile, computed independently.
[[nodiscard]] Vec3d expected_point(const TileKey& key, std::int64_t i, std::int64_t j,
                                   const planetsim::presentation::TerrainRelief& relief,
                                   const planetsim::presentation::TileDrawing& drawing) {
    const double lines = static_cast<double>(tile_grid) * std::ldexp(1.0, key.level);
    const double u = -1.0 + 2.0 * (static_cast<double>(key.x) * tile_grid + static_cast<double>(i)) / lines;
    const double v = -1.0 + 2.0 * (static_cast<double>(key.y) * tile_grid + static_cast<double>(j)) / lines;
    const Vec3d direction = planetsim::presentation::tile_face_direction(key.face, u, v);
    const double height = relief.sample(direction).elevation_m - drawing.sea_level_m;
    return direction * (1.0 + drawing.relief_exaggeration / drawing.radius_m * std::max(0.0, height));
}

[[nodiscard]] double nearest(const TileGeometry& tile, const Vec3d& point) {
    double best = 1e300;
    for (std::size_t vertex = 0; vertex < surface_vertex_count(); ++vertex) {
        best = std::min(best, planetsim::length(absolute(tile, vertex) - point));
    }
    return best;
}

[[nodiscard]] bool same_bytes(const TileGeometry& a, const TileGeometry& b) {
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.origin.z == b.origin.z &&
           a.positions == b.positions && a.normals == b.normals && a.cell_texel == b.cell_texel &&
           a.corner_texel == b.corner_texel && a.custom0 == b.custom0 && a.custom1 == b.custom1;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const Fixture fixture = make_fixture();
    const planetsim::presentation::TerrainRelief relief(fixture.mesh, fixture.terrain, 20260928U);
    const TileKey key{4U, 6U, 23U, 41U};
    const double tolerance = 1e-6 * planetsim::presentation::tile_angular_size(key);

    const auto begin = std::chrono::steady_clock::now();
    const TileGeometry tile = planetsim::presentation::make_terrain_tile(key, relief, fixture.drawing);
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "tile generation: " << milliseconds << " ms, " << tile.positions.size() / 3U
              << " vertices\n";
    PLANETSIM_EXPECT(test, tile.positions.size() / 3U == surface_vertex_count() + 4U * tile_grid * 6U);

    // V10: the same tile is byte-identical however it is reached: from a
    // fresh relief, after its neighbour, its parent and a child were made.
    {
        const planetsim::presentation::TerrainRelief fresh(fixture.mesh, fixture.terrain, 20260928U);
        static_cast<void>(planetsim::presentation::make_terrain_tile({4U, 6U, 24U, 41U}, fresh, fixture.drawing));
        static_cast<void>(planetsim::presentation::make_terrain_tile(planetsim::presentation::tile_parent(key), fresh, fixture.drawing));
        static_cast<void>(planetsim::presentation::make_terrain_tile(planetsim::presentation::tile_children(key)[3], fresh, fixture.drawing));
        PLANETSIM_EXPECT(test, same_bytes(tile, planetsim::presentation::make_terrain_tile(key, fresh, fixture.drawing)));
    }

    // Neighbours meet: every point of the shared edge is a vertex of both.
    {
        const TileKey right{key.face, key.level, key.x + 1U, key.y};
        const TileGeometry neighbour = planetsim::presentation::make_terrain_tile(right, relief, fixture.drawing);
        double worst = 0.0;
        for (std::int64_t j = 0; j <= tile_grid; ++j) {
            const Vec3d point = expected_point(key, tile_grid, j, relief, fixture.drawing);
            worst = std::max({worst, nearest(tile, point), nearest(neighbour, point)});
        }
        std::cout << "shared edge: worst gap " << worst / planetsim::presentation::tile_angular_size(key)
                  << " of the tile width\n";
        PLANETSIM_EXPECT(test, worst < tolerance);
    }

    // Levels agree: a child's every other vertex is a vertex of the parent.
    {
        const TileKey child = planetsim::presentation::tile_children(key)[1];
        const TileGeometry child_tile = planetsim::presentation::make_terrain_tile(child, relief, fixture.drawing);
        double worst = 0.0;
        for (std::int64_t j = 0; j <= tile_grid; j += 2) {
            for (std::int64_t i = 0; i <= tile_grid; i += 8) {
                const Vec3d point = expected_point(child, i, j, relief, fixture.drawing);
                worst = std::max({worst, nearest(child_tile, point), nearest(tile, point)});
            }
        }
        PLANETSIM_EXPECT(test, worst < tolerance);
    }

    // Winding: every surface triangle is clockwise seen from outside
    // (Godot's front face), and its texel addresses are a cell and two
    // corners, with weights that sum to one and stay near the triangle.
    {
        bool clockwise = true;
        bool addresses = true;
        const std::size_t cells = fixture.mesh->cell_count();
        for (std::size_t vertex = 0; vertex < surface_vertex_count(); vertex += 3) {
            const Vec3d a = absolute(tile, vertex);
            const Vec3d b = absolute(tile, vertex + 1);
            const Vec3d c = absolute(tile, vertex + 2);
            clockwise = clockwise && planetsim::dot(planetsim::cross(b - a, c - a), a) < 0.0;
            for (std::size_t k = vertex; k < vertex + 3; ++k) {
                const auto cell = static_cast<std::size_t>(tile.cell_texel[2 * k] + 2048.0F * tile.cell_texel[2 * k + 1]);
                const auto first = static_cast<std::size_t>(tile.corner_texel[2 * k] + 2048.0F * tile.corner_texel[2 * k + 1]);
                const float wc = tile.custom0[4 * k + 2];
                const float wa = tile.custom0[4 * k + 3];
                addresses = addresses && cell < cells && first >= cells && wc > -0.5F && wc < 1.5F &&
                            wa > -0.5F && wa < 1.5F && 1.0F - wc - wa > -0.5F;
            }
        }
        PLANETSIM_EXPECT(test, clockwise);
        PLANETSIM_EXPECT(test, addresses);
    }

    // CUSTOM1 carries the planet-centred direction and the height above sea.
    {
        bool matches = true;
        for (std::size_t vertex = 0; vertex < surface_vertex_count(); vertex += 97) {
            const Vec3d direction = planetsim::normalized(absolute(tile, vertex));
            const double expected = relief.sample(direction).elevation_m - fixture.drawing.sea_level_m;
            const Vec3d carried{tile.custom1[4 * vertex], tile.custom1[4 * vertex + 1], tile.custom1[4 * vertex + 2]};
            matches = matches && planetsim::length(carried - direction) < 1e-6;
            matches = matches && std::abs(tile.custom1[4 * vertex + 3] - expected) < 0.05 + 1e-4 * std::abs(expected);
        }
        PLANETSIM_EXPECT(test, matches);
    }
    // A coarse tile (vertex spacing over half a cell) draws the filtered
    // surface: the cells' means interpolated as the cell mesh draws them.
    {
        const TileKey coarse{2U, 0U, 0U, 0U};
        const TileGeometry root = planetsim::presentation::make_terrain_tile(coarse, relief, fixture.drawing);
        bool smooth = true;
        for (std::size_t vertex = 0; vertex < surface_vertex_count(); vertex += 101) {
            const Vec3d direction = planetsim::normalized(absolute(root, vertex));
            const double expected = relief.smooth_sample(direction).elevation_m - fixture.drawing.sea_level_m;
            smooth = smooth && std::abs(root.custom1[4 * vertex + 3] - expected) < 0.05 + 1e-4 * std::abs(expected);
        }
        PLANETSIM_EXPECT(test, smooth);
    }
    return test.result();
}
