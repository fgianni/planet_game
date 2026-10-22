// INV-04: climate mode's drift from the initial state, not a V8 gate. L3,
// seed 1, three layers, no spin-up before tick 0. Yearly means of the
// surface temperature and the top-of-atmosphere net flux, with the
// ocean-weighted deep-ocean temperature and the snow and sea-ice masses at
// each year's end. Usage: climate_drift_probe <years>.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <cstdlib>
#include <iostream>

using namespace planetsim;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int years = std::atoi(argv[1]);
    if (years < 1) return 2;
    Scenario scenario;
    scenario.subdivision = 3;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    PlanetRun run(scenario, 4);
    double area = 0.0;
    for (const auto& cell : run.state().mesh().cells()) area += cell.area_m2;
    for (int year = 0; year < years; ++year) {
        double seconds = 0.0;
        double surface_K_s = 0.0;
        double land_K_s = 0.0;
        double net_J = 0.0;
        for (int month = 0; month < 12; ++month) {
            run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
            const auto& step = run.last_step();
            seconds += step.duration_s;
            surface_K_s += step.mean_surface_temperature_K * step.duration_s;
            land_K_s += step.land_mean_surface_temperature_K * step.duration_s;
            net_J += (step.absorbed_W - step.emitted_W) * step.duration_s;
        }
        const auto& deep = run.state().slow().ocean_deep_temperature_K;
        const auto& fractions = run.fractions();
        double ocean_m2 = 0.0;
        double deep_K_m2 = 0.0;
        for (const auto& cell : run.state().mesh().cells()) {
            const double weight = cell.area_m2 * fractions.ocean_fraction[cell.id];
            ocean_m2 += weight;
            deep_K_m2 += weight * deep[cell.id];
        }
        std::cout << "year=" << year + 1 << " surface_K=" << surface_K_s / seconds
                  << " land_K=" << land_K_s / seconds << " toa_W_m2=" << net_J / seconds / area
                  << " deep_ocean_K=" << deep_K_m2 / ocean_m2
                  << " snow_kg=" << run.last_step().snow_kg
                  << " sea_ice_kg=" << run.last_step().ice_kg << std::endl;
    }
}
