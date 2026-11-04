// INV-05: reference mode's polar caps (poleward of 60°), month by month,
// to compare levels: the heat the dynamics carries into each cap across 60°
// and 70° (dry and latent, W/m² of the cap; the dynamics' northward
// transports, INV-04), the caps' cloud radiative effects and cloud cover,
// their surface temperature, and the hemispheres' ice and snow cover.
// Not a gate.
//
//   reference_polar_budget_probe LEVEL MONTHS [WORKERS]
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace planetsim;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto level = static_cast<std::uint32_t>(std::atoi(argv[1]));
    const int months = std::atoi(argv[2]);
    const std::size_t workers = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 16U;
    Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    scenario.initial_mode = SimulationMode::reference;
    PlanetRun run(scenario, workers);
    const auto& mesh = run.state().mesh();
    const std::size_t cells = mesh.cell_count();
    // hemisphere 0 south, 1 north; -1 equatorward of 60°
    std::vector<int> cap(cells, -1);
    double cap_area[2] = {}, cap70_area[2] = {};
    for (std::size_t c = 0; c < cells; ++c) {
        const double lat = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        if (std::abs(lat) > 60.0) {
            cap[c] = lat > 0.0 ? 1 : 0;
            cap_area[cap[c]] += mesh.cells()[c].area_m2;
            if (std::abs(lat) > 70.0) cap70_area[cap[c]] += mesh.cells()[c].area_m2;
        }
    }
    // Crossings at 80° S, 70° S, ..., 80° N: 60° S is 2, 70° S 1, 60° N 14, 70° N 15.
    std::cout << std::fixed << std::setprecision(1);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        double seconds = 0.0;
        double dry60[2] = {}, lat60[2] = {}, dry70[2] = {}, sw[2] = {}, lw[2] = {}, cover[2] = {}, ts[2] = {};
        double ice[2] = {}, snow[2] = {};
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                const auto& step = run.last_step();
                const double dt = step.duration_s;
                seconds += dt;
                if (run.dynamics() != nullptr) {
                    const auto& d = run.dynamics()->last();
                    dry60[0] += -d.northward_dry_W[2] * dt;   // southward into the southern cap
                    lat60[0] += -d.northward_latent_W[2] * dt;
                    dry70[0] += -d.northward_dry_W[1] * dt;
                    dry60[1] += d.northward_dry_W[14] * dt;
                    lat60[1] += d.northward_latent_W[14] * dt;
                    dry70[1] += d.northward_dry_W[15] * dt;
                }
                const auto& forcing = run.state().forcing();
                const bool clouds = forcing.cloud_shortwave_effect_W_m2.size() == cells;
                for (std::size_t c = 0; c < cells; ++c) {
                    if (cap[c] < 0) continue;
                    const double w = mesh.cells()[c].area_m2 * dt;
                    ts[cap[c]] += forcing.surface_temperature_K[c] * w;
                    if (clouds) {
                        sw[cap[c]] += forcing.cloud_shortwave_effect_W_m2[c] * w;
                        lw[cap[c]] += forcing.cloud_longwave_effect_W_m2[c] * w;
                    }
                }
                const auto& z = step.zonal_cloud_cover;   // 10° bands from 90° S
                double band_w[2] = {}, band_c[2] = {};
                for (int b : {0, 1, 2, 15, 16, 17}) {
                    const int h = b < 9 ? 0 : 1;
                    const double lat = -85.0 + 10.0 * b;
                    const double w = std::cos(lat * std::numbers::pi / 180.0);
                    band_c[h] += z[b] * w; band_w[h] += w;
                }
                for (int h = 0; h < 2; ++h) cover[h] += band_c[h] / band_w[h] * dt;
                ice[0] += step.ice_cover_south_m2 * dt; ice[1] += step.ice_cover_north_m2 * dt;
                snow[0] += step.snow_cover_south_m2 * dt; snow[1] += step.snow_cover_north_m2 * dt;
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        std::cout << "month " << std::setw(3) << month + 1;
        for (int h = 0; h < 2; ++h) {
            const double a = cap_area[h] * seconds;
            std::cout << (h == 0 ? " | S: " : " | N: ") << "Ts " << ts[h] / a << " K, in@60 dry "
                      << std::setw(6) << dry60[h] / a << " latent " << std::setw(5) << lat60[h] / a
                      << ", in@70 dry " << std::setw(6) << dry70[h] / (cap70_area[h] * seconds)
                      << " W/m², cloud SW " << std::setw(6) << sw[h] / a << " LW " << std::setw(5) << lw[h] / a
                      << " cover " << std::setprecision(2) << cover[h] / seconds << std::setprecision(1)
                      << ", ice " << ice[h] / (cap_area[h] * seconds) << " snow "
                      << snow[h] / (cap_area[h] * seconds) << " of cap";
        }
        std::cout << std::endl;
    }
    return 0;
}
