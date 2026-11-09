// R1-05 option A experiment: one fixed geographic sea-level contour in a
// coastal tile, measured against the existing filtered and detailed geometry.
// This does not change production tiles or any authoritative state.
// Build from the repository root; the command is in R1-05 ("The
// connected-ocean rule").
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using planetsim::Vec3d;
using planetsim::presentation::TileKey;
using planetsim::presentation::TileGeometry;
constexpr std::uint32_t grid = 256U;

struct Sample {
    Vec3d direction;
    double height_m = 0.0;
    bool water = false;
    bool disconnected_low = false;
};

[[nodiscard]] double coordinate(std::uint32_t tile, std::uint8_t level, double i, double size) {
    return -1.0 + 2.0 * (static_cast<double>(tile) + i / size) / std::ldexp(1.0, level);
}

[[nodiscard]] std::size_t vertex(std::uint32_t i, std::uint32_t j) {
    constexpr auto n = planetsim::presentation::tile_grid;
    const std::uint32_t qi = std::min(i, n - 1U);
    const std::uint32_t qj = std::min(j, n - 1U);
    const std::size_t base = 6U * (qj * n + qi);
    if (i == n && j == n) return base + 1U;
    if (i == n) return base + 2U;
    if (j == n) return base + 4U;
    return base;
}

// Piecewise linear height actually carried by a tile's two triangles, not
// the continuous relief function. i,j are fractional grid coordinates.
[[nodiscard]] double mesh_height(const TileGeometry& tile, double i, double j) {
    constexpr auto n = planetsim::presentation::tile_grid;
    const auto x = std::min(static_cast<std::uint32_t>(i), n - 1U);
    const auto y = std::min(static_cast<std::uint32_t>(j), n - 1U);
    const double u = i - static_cast<double>(x);
    const double v = j - static_cast<double>(y);
    const auto h = [&](std::uint32_t dx, std::uint32_t dy) {
        return static_cast<double>(tile.custom1[4U * vertex(x + dx, y + dy) + 3U]);
    };
    // The tile emits (v00,v11,v10) below the diagonal v=u, and
    // (v00,v01,v11) above it.
    return v <= u ? (1.0 - u) * h(0U, 0U) + (u - v) * h(1U, 0U) + v * h(1U, 1U)
                  : (1.0 - v) * h(0U, 0U) + (v - u) * h(0U, 1U) + u * h(1U, 1U);
}

[[nodiscard]] Sample sample(const TileKey& tile, double i, double j, double size,
                            const planetsim::presentation::TerrainRelief& relief,
                            const planetsim::TerrainSnapshot& terrain, planetsim::CellId& hint) {
    const Vec3d direction = planetsim::presentation::tile_face_direction(
        tile.face, coordinate(tile.x, tile.level, i, size),
        coordinate(tile.y, tile.level, j, size));
    const auto result = relief.sample(direction, hint);
    hint = result.cell;
    const double height = result.elevation_m - terrain.sea_level_m;
    const bool connected = terrain.land_fraction[result.cell.to_index()] < 1.0F - 1e-6F;
    return {direction, height, connected && height <= 0.0, !connected && height <= 0.0};
}

[[nodiscard]] std::size_t crossings(const TileKey& tile, std::uint32_t n,
                                    const planetsim::presentation::TerrainRelief& relief,
                                    const planetsim::TerrainSnapshot& terrain) {
    std::vector<bool> row(n + 1U), previous(n + 1U);
    auto hint = planetsim::CellId{0};
    std::size_t edges = 0U;
    for (std::uint32_t j = 0U; j <= n; ++j) {
        for (std::uint32_t i = 0U; i <= n; ++i) {
            row[i] = sample(tile, i, j, n, relief, terrain, hint).water;
            if (i > 0U) edges += row[i] != row[i - 1U] ? 1U : 0U;
            if (j > 0U) edges += row[i] != previous[i] ? 1U : 0U;
        }
        previous.swap(row);
    }
    return edges;
}

} // namespace

int main() {
    using namespace planetsim;
    using namespace planetsim::presentation;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = 4U;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(make_icosphere(scenario.subdivision, run.parameters().radius_m));
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);

    // Choose one L1 tile by a coarse, fixed geographic scan; no camera or
    // tile-generation order chooses the study region.
    TileKey chosen{};
    std::size_t most_crossings = 0U;
    for (std::uint8_t face = 0U; face < 6U; ++face) {
        for (std::uint32_t y = 0U; y < 2U; ++y) {
            for (std::uint32_t x = 0U; x < 2U; ++x) {
                const TileKey key{face, 1U, x, y};
                const std::size_t count = crossings(key, 24U, relief, terrain);
                if (count > most_crossings) {
                    chosen = key;
                    most_crossings = count;
                }
            }
        }
    }

    const auto begin = std::chrono::steady_clock::now();
    TileDrawing drawing;
    drawing.sea_level_m = terrain.sea_level_m;
    drawing.radius_m = run.parameters().radius_m;
    const auto parent = make_terrain_tile(chosen, relief, drawing);
    const auto child_keys = tile_children(chosen);
    std::array<TileGeometry, 4> children;
    for (std::size_t index = 0U; index < 4U; ++index)
        children[index] = make_terrain_tile(child_keys[index], relief, drawing);

    std::vector<Sample> samples((grid + 1U) * (grid + 1U));
    auto hint = CellId{0};
    std::size_t disconnected_low = 0U;
    std::size_t reference_water = 0U;
    std::size_t parent_disagreement = 0U;
    std::size_t child_disagreement = 0U;
    const auto at = [&](std::uint32_t i, std::uint32_t j) -> Sample& {
        return samples[j * (grid + 1U) + i];
    };
    for (std::uint32_t j = 0U; j <= grid; ++j) {
        for (std::uint32_t i = 0U; i <= grid; ++i) {
            Sample& point = at(i, j);
            point = sample(chosen, i, j, grid, relief, terrain, hint);
            disconnected_low += point.disconnected_low ? 1U : 0U;
            reference_water += point.water ? 1U : 0U;
            const double pi = static_cast<double>(i) * tile_grid / grid;
            const double pj = static_cast<double>(j) * tile_grid / grid;
            parent_disagreement += (mesh_height(parent, pi, pj) <= 0.0) != point.water ? 1U : 0U;
            const std::uint32_t cx = i >= grid / 2U ? 1U : 0U;
            const std::uint32_t cy = j >= grid / 2U ? 1U : 0U;
            child_disagreement += (mesh_height(children[2U * cy + cx],
                2.0 * (pi - cx * tile_grid / 2U), 2.0 * (pj - cy * tile_grid / 2U)) <= 0.0) != point.water ? 1U : 0U;
        }
    }

    // March the fixed grid's water mask to estimate how many additional
    // geographic vertices a traced coast would need on this tile. This is
    // NOT yet compatible geometry; connectivity boundaries cannot be
    // interpolated as a continuous height zero.
    std::size_t coast_edges = 0U;
    std::size_t connectivity_edges = 0U;
    for (std::uint32_t j = 0U; j <= grid; ++j) {
        for (std::uint32_t i = 0U; i <= grid; ++i) {
            const auto inspect = [&](const Sample& a, const Sample& b) {
                if (a.water != b.water) {
                    ++coast_edges;
                    if ((a.height_m <= 0.0) == (b.height_m <= 0.0)) ++connectivity_edges;
                }
            };
            if (i > 0U) inspect(at(i - 1U, j), at(i, j));
            if (j > 0U) inspect(at(i, j - 1U), at(i, j));
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double points = static_cast<double>(samples.size());
    std::cout << "L4 seed " << scenario.seed << " L1 coastal tile face " << static_cast<int>(chosen.face)
              << " x " << chosen.x << " y " << chosen.y << " (coarse crossings " << most_crossings << ")\n"
              << "fixed connected-ocean field on " << grid << "x" << grid << " grid: water "
              << 100.0 * reference_water / points << " %, disconnected low " << disconnected_low
              << ", coast edge crossings " << coast_edges << " (connectivity-driven " << connectivity_edges << ")\n"
              << "classification disagreement vs fixed field: filtered L1 "
              << 100.0 * parent_disagreement / points << " %, detailed L2 "
              << 100.0 * child_disagreement / points << " %; five meshes + reference samples "
              << seconds << " s\n";
}
