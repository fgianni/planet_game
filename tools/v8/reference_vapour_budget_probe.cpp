// INV-05: reference mode's vapour budget by 30° band, month by month, to
// compare levels: where the water that would reach the polar caps goes.
// kg/m²/day of each band: evaporation, resolved and convective
// condensation (the columns' condensate), cloud and rain re-evaporation,
// the dynamics' advection (its clipping included), and the band's vapour
// path (kg/m²). Not a gate.
//
//   reference_vapour_budget_probe LEVEL MONTHS [WORKERS]
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
    std::array<double, 6> band_area{};
    for (const auto& cell : mesh.cells()) {
        const double lat = std::asin(std::clamp(cell.center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        band_area[static_cast<std::size_t>(std::clamp(std::floor((lat + 90.0) / 30.0), 0.0, 5.0))] += cell.area_m2;
    }
    const char* names[6] = {"90-60S", "60-30S", "30S-0", "0-30N", "30-60N", "60-90N"};
    std::cout << std::fixed << std::setprecision(2);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        double seconds = 0.0, clipped = 0.0;
        std::array<double, 6> evap{}, cond{}, conv{}, reevap{}, adv{}, path{};
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                const auto& step = run.last_step();
                const double dt = step.duration_s;
                seconds += dt;
                for (std::size_t b = 0; b < 6; ++b) {
                    const auto& v = step.vapour_budget[b];
                    for (std::size_t k = 0; k < max_atmosphere_layer_count; ++k) {
                        evap[b] += v.evaporation_kg[k];
                        cond[b] += v.condensation_kg[k];
                        conv[b] += v.convective_kg[k];
                        reevap[b] += v.cloud_evaporation_kg[k] + v.rain_evaporation_kg[k];
                        path[b] += v.vapour_kg[k] * dt;
                    }
                }
                if (run.dynamics() != nullptr) {
                    const auto& d = run.dynamics()->last();
                    clipped += d.clipped_kg;
                    for (std::size_t b = 0; b < 6; ++b) {
                        for (std::size_t k = 0; k < max_atmosphere_layer_count; ++k) adv[b] += d.advection_by_band_kg[b][k];
                    }
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        const double days = seconds / 86400.0;
        double total_area = 0.0;
        for (double a : band_area) total_area += a;
        std::cout << "month " << std::setw(3) << month + 1 << " (clipping " << std::setprecision(4)
                  << clipped / total_area / days << " kg/m²/day)" << std::setprecision(2) << std::endl;
        for (std::size_t b = 0; b < 6; ++b) {
            const double a = band_area[b] * days;
            std::cout << "  " << names[b] << ": evap " << std::setw(5) << evap[b] / a << " cond "
                      << std::setw(5) << cond[b] / a << " conv " << std::setw(5) << conv[b] / a << " re-evap "
                      << std::setw(5) << reevap[b] / a << " advection " << std::setw(6) << adv[b] / a
                      << " | vapour " << std::setw(5) << path[b] / (band_area[b] * seconds) << " kg/m²"
                      << std::endl;
        }
    }
    return 0;
}
