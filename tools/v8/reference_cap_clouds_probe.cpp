// INV-05: reference mode's clouds over the polar caps (poleward of 60°),
// month by month, sampled hourly, to compare levels. Separates less cloud
// water from the same water concentrated into overcast cells, where the
// fraction f_k = min(1, q_l m / W_c) (ADR-0023 §13.3) stops growing:
// the caps' mean cloud water path, column cover 1 − Π(1 − f_k), the share
// of cell layers at overcast, and the share of the cloud water beyond
// overcast (which adds longwave depth but no cover). Not a gate.
//
//   reference_cap_clouds_probe LEVEL MONTHS [WORKERS]
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
    const double w_c = AtmosphereParameters{}.cloud_in_cloud_water_path_kg_m2;
    const double g = 9.80665;
    std::vector<int> cap(cells, -1);
    for (std::size_t c = 0; c < cells; ++c) {
        const double lat = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        if (std::abs(lat) > 60.0) cap[c] = lat > 0.0 ? 1 : 0;
    }
    std::cout << std::fixed << std::setprecision(3);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        int steps = 0;
        double area[2] = {}, path[2] = {}, cover[2] = {}, beyond[2] = {}, overcast[2] = {}, layers_n[2] = {};
        double vapour[2] = {};
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                if (++steps % 6 != 0) continue;   // hourly
                const auto& slow = run.state().slow();
                const auto& q_l = slow.atmosphere_cloud_water_kg_kg;
                const auto& q_v = slow.atmosphere_specific_humidity_kg_kg;
                const std::size_t n = q_l.layer_count();
                if (n == 0) continue;
                for (std::size_t c = 0; c < cells; ++c) {
                    if (cap[c] < 0) continue;
                    const int h = cap[c];
                    const double a = mesh.cells()[c].area_m2;
                    const double m = slow.atmosphere_surface_pressure_Pa[c] / (g * static_cast<double>(n));
                    double clear = 1.0;
                    for (std::size_t k = 0; k < n; ++k) {
                        const double water = q_l.layer(k)[c] * m;
                        const double f = std::min(1.0, water / w_c);
                        clear *= 1.0 - f;
                        path[h] += a * water;
                        beyond[h] += a * std::max(0.0, water - w_c);
                        overcast[h] += a * (f >= 1.0 ? 1.0 : 0.0);
                        layers_n[h] += a;
                        if (q_v.layer_count() == n) vapour[h] += a * q_v.layer(k)[c] * m;
                    }
                    cover[h] += a * (1.0 - clear);
                    area[h] += a;
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        std::cout << "month " << std::setw(3) << month + 1;
        for (int h = 0; h < 2; ++h) {
            std::cout << (h == 0 ? " | S: " : " | N: ") << "vapour " << std::setprecision(2) << vapour[h] / area[h]
                      << " kg/m², cloud water " << std::setprecision(4) << path[h] / area[h] << " kg/m², cover "
                      << std::setprecision(3) << cover[h] / area[h] << ", overcast layers "
                      << overcast[h] / layers_n[h] << ", water beyond overcast "
                      << (path[h] > 0.0 ? beyond[h] / path[h] : 0.0);
        }
        std::cout << std::endl;
    }
    return 0;
}
