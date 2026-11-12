// INV-05: the polar caps' full energy budget (poleward of 60°) by month,
// to find which term carries the levels' difference. W/m² of the cap:
// - top: sunlight absorbed (the tiles'), outgoing longwave (the columns'),
//   the clouds' shortwave and longwave effects (so clear-sky = all − cloud),
//   net = absorbed − outgoing;
// - the dynamics' convergence: dry (enthalpy and potential energy) and
//   latent (L_v μ q), the column changes the winds made (INV-04);
// - storage: total (tiles and atmosphere) and the atmosphere's part;
// - surface: sensible heat to the air, evaporation's latent heat;
// - melt (the tiles' melt and freeze heat) and the condensation's latent
//   heat; physics-residual = net + transport − storage − melt − evaporation
//   + condensation, the surface step's own closure (≈ 0), as a check.
// Not a gate.
//
//   reference_cap_energy_probe LEVEL MONTHS [WORKERS] [ORDER]
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace planetsim;

namespace {
enum Term { absorbed, outgoing, cloud_sw, cloud_lw, dry, latent, storage, atmosphere, sensible, evaporation, melt, condensation, transport, surface_K, terms };
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto level = static_cast<std::uint32_t>(std::atoi(argv[1]));
    const int months = std::atoi(argv[2]);
    const std::size_t workers = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 16U;
    const int order = argc > 4 ? std::atoi(argv[4]) : 1;
    Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    scenario.initial_mode = SimulationMode::reference;
    PlanetRun run(scenario, workers, ClimateCirculationUse::coupled,
                  [=](SurfaceEnergyParameters& p) { p.vapour_advection_order = order; });
    const auto& mesh = run.state().mesh();
    const std::size_t cells = mesh.cell_count();
    std::vector<int> cap(cells, -1);
    double cap_area[2] = {};
    for (std::size_t c = 0; c < cells; ++c) {
        const double z = mesh.cells()[c].center_unit.z;
        if (z > std::sin(std::numbers::pi / 3.0)) cap[c] = 1;
        if (z < -std::sin(std::numbers::pi / 3.0)) cap[c] = 0;
        if (cap[c] >= 0) cap_area[cap[c]] += mesh.cells()[c].area_m2;
    }
    const char* names[terms] = {"absorbed", "OLR", "cloudSW", "cloudLW", "dry", "latent", "storage", "atm-storage", "sensible", "evap-latent", "melt", "cond-latent", "transport", "Ts"};
    std::cout << std::fixed << std::setprecision(1);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        double seconds = 0.0;
        std::array<std::array<double, terms>, 2> sum{};
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                const auto& step = run.last_step();
                const double dt = step.duration_s;
                seconds += dt;
                const auto& forcing = run.state().forcing();
                const bool clouds = forcing.cloud_shortwave_effect_W_m2.size() == cells;
                const auto* dynamics = run.dynamics() != nullptr ? &run.dynamics()->last() : nullptr;
                const bool moved = dynamics != nullptr && dynamics->cell_dry_W_m2.size() == cells;
                if (step.cell_absorbed_W_m2.size() != cells) continue;
                for (std::size_t c = 0; c < cells; ++c) {
                    if (cap[c] < 0) continue;
                    auto& s = sum[static_cast<std::size_t>(cap[c])];
                    const double w = mesh.cells()[c].area_m2 * dt;
                    s[absorbed] += w * step.cell_absorbed_W_m2[c];
                    s[outgoing] += w * step.cell_outgoing_W_m2[c];
                    if (clouds) {
                        s[cloud_sw] += w * forcing.cloud_shortwave_effect_W_m2[c];
                        s[cloud_lw] += w * forcing.cloud_longwave_effect_W_m2[c];
                    }
                    if (moved) {
                        s[dry] += w * dynamics->cell_dry_W_m2[c];
                        s[latent] += w * dynamics->cell_latent_W_m2[c];
                    }
                    s[storage] += w * step.cell_storage_W_m2[c];
                    s[atmosphere] += w * step.cell_atmosphere_storage_W_m2[c];
                    s[sensible] += w * step.cell_sensible_W_m2[c];
                    s[evaporation] += w * step.cell_evaporation_latent_W_m2[c];
                    s[melt] += w * step.cell_melt_latent_W_m2[c];
                    s[condensation] += w * step.cell_condensation_latent_W_m2[c];
                    if (step.cell_transport_W_m2.size() == cells) s[transport] += w * step.cell_transport_W_m2[c];
                    s[surface_K] += w * forcing.surface_temperature_K[c];
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        std::cout << "month " << std::setw(3) << month + 1;
        for (std::size_t h = 0; h < 2; ++h) {
            const double a = cap_area[h] * seconds;
            std::array<double, terms> v{};
            for (std::size_t t = 0; t < terms; ++t) v[t] = sum[h][t] / a;
            std::cout << (h == 0 ? " | S:" : " | N:");
            for (std::size_t t = 0; t < terms; ++t) std::cout << ' ' << names[t] << ' ' << v[t];
            std::cout << " net " << v[absorbed] - v[outgoing] << " physics-residual "
                      << std::setprecision(4)
                      << v[absorbed] - v[outgoing] + v[transport] - v[storage] - v[melt] - v[evaporation] + v[condensation]
                      << std::setprecision(1);
        }
        std::cout << std::endl;
    }
    return 0;
}
