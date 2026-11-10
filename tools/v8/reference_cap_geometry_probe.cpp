// INV-05: the polar caps' surface by mesh level, before any climate step:
// for each cap poleward of 60° (and the 60–70° and 70–90° rings), the land
// and ocean shares, the land's area-weighted mean height (each cell's
// median sub-cell elevation, hypsometry quantile 5 of 9), the share of the
// cap in mixed coast cells (0 < land < 1) and the share above 1.5 km. The
// same seed's planet at each level; not a gate.
//
//   reference_cap_geometry_probe LEVEL
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>

using namespace planetsim;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    Scenario scenario;
    scenario.subdivision = static_cast<std::uint32_t>(std::atoi(argv[1]));
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    scenario.initial_mode = SimulationMode::reference;
    PlanetRun run(scenario, 8U);
    const auto& mesh = run.state().mesh();
    const auto& fractions = run.fractions();
    const auto median = run.state().slow().hypsometry_m.layer(4);
    // [hemisphere][ring]: ring 0 the whole cap, 1 60–70°, 2 70–90°.
    double area[2][3] = {}, land[2][3] = {}, height[2][3] = {}, coast[2][3] = {}, high[2][3] = {};
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
        const double lat = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        if (std::abs(lat) <= 60.0) continue;
        const int h = lat > 0.0 ? 1 : 0;
        const double a = mesh.cells()[c].area_m2;
        const double f = fractions.land_fraction[c];
        const double z = std::max(0.0, static_cast<double>(median[c]));
        for (int r : {0, std::abs(lat) < 70.0 ? 1 : 2}) {
            area[h][r] += a;
            land[h][r] += a * f;
            height[h][r] += a * f * z;
            coast[h][r] += (f > 0.0 && f < 1.0) ? a : 0.0;
            high[h][r] += z >= 1500.0 ? a * f : 0.0;
        }
    }
    const char* rings[3] = {">60", "60-70", "70-90"};
    double planet = 0.0, planet_land = 0.0;
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
        planet += mesh.cells()[c].area_m2;
        planet_land += mesh.cells()[c].area_m2 * fractions.land_fraction[c];
    }
    std::cout << std::fixed << std::setprecision(3) << "L" << scenario.subdivision << " (" << mesh.cell_count()
              << " cells, land " << planet_land / planet << ")\n";
    for (int h = 0; h < 2; ++h) {
        for (int r = 0; r < 3; ++r) {
            std::cout << "  " << (h == 0 ? "S " : "N ") << std::setw(5) << rings[r] << ": land "
                      << land[h][r] / area[h][r] << ", mean land height " << std::setprecision(0)
                      << (land[h][r] > 0.0 ? height[h][r] / land[h][r] : 0.0) << std::setprecision(3)
                      << " m, coast cells " << coast[h][r] / area[h][r] << ", land ≥ 1.5 km "
                      << high[h][r] / area[h][r] << '\n';
        }
    }
    return 0;
}
