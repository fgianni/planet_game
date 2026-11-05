// INV-05: reference mode's poleward transport without condensation. A
// passive tracer χ held at 1 in the bottom layer equatorward of 30° and
// decaying everywhere with a 10-day lifetime (AtmosphereDynamicsParameters::
// passive_tracer_lifetime_s), carried by the same winds and scheme as the
// vapour but never raining out. Month by month: what the winds carry into
// each cap poleward of 60° (kg/m²/day of the cap, of μ χ) and the caps'
// mean χ. If it falls with level as the vapour does, the transport itself
// changes; if not, the rain-out does. Not a gate.
//
//   reference_tracer_probe LEVEL MONTHS [WORKERS] [LIFETIME_DAYS] [ORDER]
//
// ORDER: the vapour's and the tracer's horizontal advection (1 upwind, the
// preset; 2 limited second-order).
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

using namespace planetsim;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto level = static_cast<std::uint32_t>(std::atoi(argv[1]));
    const int months = std::atoi(argv[2]);
    const std::size_t workers = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 16U;
    const double lifetime_days = argc > 4 ? std::atof(argv[4]) : 10.0;
    const int order = argc > 5 ? std::atoi(argv[5]) : 1;
    Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    scenario.initial_mode = SimulationMode::reference;
    PlanetRun run(scenario, workers, ClimateCirculationUse::coupled, [=](SurfaceEnergyParameters& p) {
        p.passive_tracer_lifetime_s = lifetime_days * 86400.0;
        p.vapour_advection_order = order;
    });
    const auto& mesh = run.state().mesh();
    double cap_area[2] = {};
    for (const auto& cell : mesh.cells()) {
        const double z = cell.center_unit.z;
        if (z > std::sin(std::numbers::pi / 3.0)) cap_area[1] += cell.area_m2;
        if (z < -std::sin(std::numbers::pi / 3.0)) cap_area[0] += cell.area_m2;
    }
    std::cout << std::fixed << std::setprecision(4);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        double seconds = 0.0, into[2] = {}, mean[2] = {};
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                const double dt = run.last_step().duration_s;
                seconds += dt;
                if (run.dynamics() == nullptr) continue;
                const auto& d = run.dynamics()->last();
                for (int h = 0; h < 2; ++h) {
                    into[h] += d.tracer_into_caps_kg_s[static_cast<std::size_t>(h)] * dt;
                    mean[h] += d.tracer_cap_mean[static_cast<std::size_t>(h)] * dt;
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        std::cout << "month " << std::setw(3) << month + 1;
        for (int h = 0; h < 2; ++h) {
            std::cout << (h == 0 ? " | S: " : " | N: ") << "into the cap " << into[h] / cap_area[h] / (seconds / 86400.0)
                      << " kg/m²/day, cap mean χ " << mean[h] / seconds;
        }
        std::cout << std::endl;
    }
    return 0;
}
