// Diagnose the filtered-to-detailed terrain handoff without changing it.
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_lod_probe.cpp build-godot/libPlanetSimPresentation.a \
//   build-godot/libPlanetSim.a -lzstd -pthread -o build-godot/coast_lod_probe
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

int main() {
    using namespace planetsim;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = 4U;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(make_icosphere(scenario.subdivision, run.parameters().radius_m));
    const auto terrain = run.terrain_snapshot();
    const presentation::TerrainRelief relief(mesh, terrain, scenario.seed);
    std::size_t count = 0;
    std::size_t changed_side = 0;
    double sum_difference_m = 0.0;
    double worst_difference_m = 0.0;
    for (std::size_t index = 0; index < mesh->cell_count(); index += 7U) {
        const float* q = terrain.hypsometry_m.data() + index * hypsometry_quantile_count;
        if (q[0] >= terrain.sea_level_m || q[8] <= terrain.sea_level_m) continue;
        const CellId cell{static_cast<std::uint32_t>(index)};
        const Vec3d center = mesh->cell(cell).center_unit;
        const Vec3d east = normalized(cross(center, Vec3d{0.0, 1.0, 0.0}));
        for (int k = -3; k <= 3; ++k) {
            const Vec3d direction = normalized(center + east * (0.1 * k * relief.cell_spacing_rad()));
            const double coarse = relief.smooth_sample(direction, cell).elevation_m - terrain.sea_level_m;
            const double detailed = relief.sample(direction, cell).elevation_m - terrain.sea_level_m;
            const double difference = std::abs(detailed - coarse);
            changed_side += (coarse <= 0.0) != (detailed <= 0.0) ? 1U : 0U;
            sum_difference_m += difference;
            worst_difference_m = std::max(worst_difference_m, difference);
            ++count;
        }
    }
    std::uint8_t first_detailed = 0;
    while (presentation::tile_angular_size({0, first_detailed, 0, 0}) /
               static_cast<double>(presentation::tile_grid) >
           presentation::tile_filtered_cells * relief.cell_spacing_rad()) ++first_detailed;
    std::cout << "L4 first detailed tile level " << static_cast<int>(first_detailed)
              << "; coastal samples " << count << "; water/land flips " << changed_side
              << " (" << 100.0 * static_cast<double>(changed_side) / static_cast<double>(count)
              << " %); mean height difference " << sum_difference_m / static_cast<double>(count)
              << " m; worst " << worst_difference_m << " m\n";
}
