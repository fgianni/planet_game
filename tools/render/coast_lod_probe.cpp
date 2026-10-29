// Diagnose the filtered-to-detailed terrain handoff and a 1D shared-coast
// contour sampling spike without changing the tile generator.
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_lod_probe.cpp build-godot/libPlanetSimPresentation.a \
//   build-godot/libPlanetSim.a -lzstd -pthread -o build-godot/coast_lod_probe
// Run: ./build-godot/coast_lod_probe [4|6]
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    using namespace planetsim;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 4U;
    if (scenario.subdivision != 4U && scenario.subdivision != 6U) {
        std::cerr << "usage: coast_lod_probe [4|6]\n";
        return 2;
    }
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(make_icosphere(scenario.subdivision, run.parameters().radius_m));
    const auto terrain = run.terrain_snapshot();
    const presentation::TerrainRelief relief(mesh, terrain, scenario.seed);
    std::size_t count = 0;
    std::size_t changed_side = 0;
    double sum_difference_m = 0.0;
    double worst_difference_m = 0.0;
    // A 1D contour spike: along fixed east-west transects in coastal cells,
    // compare linearly reconstructed sea-level crossings at several fixed
    // spacings against a 256-interval reference of the full-rank relief.
    // It cannot establish the 2D contour topology or tile seam contract.
    constexpr int reference_intervals = 256;
    constexpr std::array<int, 4> intervals{8, 16, 32, 64};
    struct Measure {
        std::size_t root_count = 0;
        std::size_t missed_roots = 0;
        std::size_t wrong_side = 0;
        std::size_t compared = 0;
        double maximum_shift_m = 0.0;
    };
    std::array<Measure, intervals.size()> measures{};
    std::size_t transects = 0;
    const auto start = std::chrono::steady_clock::now();
    const std::size_t stride = scenario.subdivision == 4U ? 7U : 53U;
    for (std::size_t index = 0; index < mesh->cell_count(); index += stride) {
        const float* q = terrain.hypsometry_m.data() + index * hypsometry_quantile_count;
        if (q[0] >= terrain.sea_level_m || q[8] <= terrain.sea_level_m) continue;
        const CellId cell{static_cast<std::uint32_t>(index)};
        const Vec3d center = mesh->cell(cell).center_unit;
        const Vec3d axis = std::abs(center.y) > 0.999999 ? Vec3d{1.0, 0.0, 0.0} : Vec3d{0.0, 1.0, 0.0};
        const Vec3d east = normalized(cross(center, axis));
        const auto direction_at = [&](double u) {
            return normalized(center + east * ((u - 0.5) * 0.6 * relief.cell_spacing_rad()));
        };
        std::array<double, reference_intervals + 1> heights{};
        for (int k = 0; k <= reference_intervals; ++k) {
            heights[k] = relief.sample(direction_at(static_cast<double>(k) / reference_intervals), cell)
                             .elevation_m - terrain.sea_level_m;
        }
        ++transects;
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
        for (std::size_t resolution = 0; resolution < intervals.size(); ++resolution) {
            auto& result = measures[resolution];
            const int step = reference_intervals / intervals[resolution];
            const double transect_m = 0.6 * relief.cell_spacing_rad() * mesh->radius_m();
            std::vector<int> crossings(static_cast<std::size_t>(intervals[resolution]), 0);
            for (int k = 0; k < reference_intervals; ++k) {
                if ((heights[k] <= 0.0) != (heights[k + 1] <= 0.0))
                    ++crossings[static_cast<std::size_t>(k / step)];
            }
            for (int k = 0; k < reference_intervals; ++k) {
                const int coarse_begin = (k / step) * step;
                const int coarse_end = coarse_begin + step;
                const double first = heights[coarse_begin];
                const double last = heights[coarse_end];
                const double interpolated = first + (last - first) *
                    (static_cast<double>(k - coarse_begin) / step);
                // Ignore points within a metre of sea level: they are
                // sensitive to the contour's eventual numeric tolerance.
                if (std::abs(heights[k]) > 1.0) {
                    result.wrong_side += (heights[k] <= 0.0) != (interpolated <= 0.0);
                    ++result.compared;
                }
                if ((heights[k] <= 0.0) == (heights[k + 1] <= 0.0)) continue;
                ++result.root_count;
                // An even number of crossings can hide inside a coarse
                // segment. Its one interpolated root cannot represent them.
                if (crossings[static_cast<std::size_t>(k / step)] != 1 ||
                    (first <= 0.0) == (last <= 0.0)) {
                    ++result.missed_roots;
                    continue;
                }
                const double true_root = (k + heights[k] / (heights[k] - heights[k + 1])) /
                    reference_intervals;
                const double coarse_root = (coarse_begin + step * first / (first - last)) /
                    reference_intervals;
                result.maximum_shift_m = std::max(result.maximum_shift_m,
                                                  std::abs(true_root - coarse_root) * transect_m);
            }
        }
    }
    std::uint8_t first_detailed = 0;
    while (presentation::tile_angular_size({0, first_detailed, 0, 0}) /
               static_cast<double>(presentation::tile_grid) >
           presentation::tile_filtered_cells * relief.cell_spacing_rad()) ++first_detailed;
    std::cout << "L" << scenario.subdivision << " first detailed tile level " << static_cast<int>(first_detailed)
              << "; coastal samples " << count << "; water/land flips " << changed_side
              << " (" << 100.0 * static_cast<double>(changed_side) / static_cast<double>(count)
              << " %); mean height difference " << sum_difference_m / static_cast<double>(count)
               << " m; worst " << worst_difference_m << " m\n";
    std::cout << "L" << scenario.subdivision << " 1D coast spike: " << transects
              << " coastal transects, 0.6 cell wide, 256-interval full-rank reference\n";
    for (std::size_t i = 0; i < intervals.size(); ++i) {
        const auto& result = measures[i];
        std::cout << "  " << intervals[i] << " intervals: " << result.wrong_side << "/"
                  << result.compared << " fixed samples change side; " << result.missed_roots << "/"
                  << result.root_count << " reference crossings missed; max matched-root shift "
                  << result.maximum_shift_m << " m\n";
    }
    std::cout << "probe time " << std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count() << " s (includes rank-table construction)\n";
}
