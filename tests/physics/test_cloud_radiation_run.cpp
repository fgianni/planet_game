#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

// Cloud radiation on the planet (ADR-0023 §4.4, §4.7, V6; task M8-04): the
// Earth-like planet in climate mode with moist convection and clouds, L3,
// from the initial state. Every month closes its energy and water budgets;
// the clouds reflect (a negative shortwave effect in every cell) and warm
// in the longwave (positive globally); the cloud fields are filled.
// `test_cloud_radiation_run <months>` runs longer and prints each month.
int main(int argc, char** argv) {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    scenario.initial_mode = planetsim::SimulationMode::climate;
    scenario.water_cycle = true;
    scenario.moist_convection = true;
    const int months = argc > 1 ? std::atoi(argv[1]) : 3;
    planetsim::PlanetRun run(scenario, 4U);
    const auto& mesh = run.state().mesh();
    double area = 0.0;
    for (const auto& cell : mesh.cells()) area += cell.area_m2;
    for (int month = 0; month < months; ++month) {
        run.run_until(planetsim::climate_substep_containing(run.tick(), run.parameters()).end_tick);
        const auto& step = run.last_step();
        PLANETSIM_EXPECT(test, step.closure_residual_J() <= step.closure_gate_J());
        PLANETSIM_EXPECT(test, step.water_cycle_residual_kg() <= step.water_cycle_gate_kg());
        const double cover = step.cloud_cover_m2 / area;
        const double shortwave = step.cloud_shortwave_effect_W / area;
        const double longwave = step.cloud_longwave_effect_W / area;
        std::cout << "month " << month << ": surface " << step.mean_surface_temperature_K
                  << " K, cloud cover " << cover << ", effect SW " << shortwave << " LW "
                  << longwave << " W/m2, cloud water " << step.cloud_water_kg / area * 1000.0
                  << " g/m2\n";
        // The first month starts without cloud water: clear (ADR-0023 §13).
        if (month == 0) {
            PLANETSIM_EXPECT(test, cover == 0.0 && shortwave == 0.0 && longwave == 0.0);
        } else {
            PLANETSIM_EXPECT(test, cover > 0.0 && cover < 1.0);
            PLANETSIM_EXPECT(test, longwave > 0.0);
            PLANETSIM_EXPECT(test, shortwave < 0.0);
            PLANETSIM_EXPECT(test, step.cloud_water_kg > 0.0);
        }
    }
    const auto& forcing = run.state().forcing();
    PLANETSIM_EXPECT(test, forcing.cloud_fraction.layer_count() == run.state().slow().atmosphere_layer_count());
    bool shortwave_signs = true;
    bool fractions = true;
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        shortwave_signs = shortwave_signs && forcing.cloud_shortwave_effect_W_m2[cell] <= 0.0F;
        for (std::size_t layer = 0; layer < forcing.cloud_fraction.layer_count(); ++layer) {
            const float f = forcing.cloud_fraction.layer(layer)[cell];
            fractions = fractions && f >= 0.0F && f <= 1.0F;
        }
    }
    PLANETSIM_EXPECT(test, shortwave_signs && fractions);
    // The presentation snapshot (schema 6, ADR-0023 §4.8, task M8-06): the
    // column cover by random overlap of the layers' fractions, and the
    // column optical depth, whose global mean follows the cloud water.
    planetsim::SimulationClock clock;
    clock.set_tick(run.tick());
    const auto frame = planetsim::make_state_snapshot(run.state(), clock);
    bool covers = frame.cloud_cover.size() == mesh.cell_count() &&
                  frame.cloud_optical_depth.size() == mesh.cell_count();
    double cover_area = 0.0;
    for (std::size_t cell = 0; covers && cell < mesh.cell_count(); ++cell) {
        double clear = 1.0;
        for (std::size_t layer = 0; layer < forcing.cloud_fraction.layer_count(); ++layer) {
            clear *= 1.0 - static_cast<double>(forcing.cloud_fraction.layer(layer)[cell]);
        }
        covers = std::abs(frame.cloud_cover[cell] - static_cast<float>(1.0 - clear)) <= 1e-6F &&
                 frame.cloud_optical_depth[cell] >= 0.0F &&
                 (frame.cloud_cover[cell] > 0.0F) == (frame.cloud_optical_depth[cell] > 0.0F);
        cover_area += mesh.cells()[cell].area_m2 * frame.cloud_cover[cell];
    }
    PLANETSIM_EXPECT(test, covers);
    PLANETSIM_EXPECT_NEAR(test, cover_area / area, run.last_step().cloud_cover_m2 / area, 1e-6);
    return test.result();
}
