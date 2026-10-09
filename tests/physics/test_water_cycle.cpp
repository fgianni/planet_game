#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/atmosphere/water.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

// Evaporation and the bucket in the surface step (ADR-0021 §4.3, V2, V3, V5;
// task M7-02), with prescribed precipitation and no rainout yet.
namespace {

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface =
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    explicit Planet(bool water)
        : mesh(std::make_shared<const planetsim::PlanetMesh>(
              planetsim::make_icosphere(3U, 6'371'000.0))),
          state(mesh) {
        surface.water_cycle = water;
        static_cast<void>(planetsim::generate_terrain(
            state, 1U, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_climate(*mesh, state.slow(), parameters, surface, 4U);
        auto& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
        for (std::size_t cell = 0; cell < precipitation.size(); ++cell) {
            precipitation[cell] = 2.5e-5F;   // about 0.8 m/yr
        }
    }

    planetsim::SurfaceEnergyDiagnostics step(std::int64_t month, std::size_t workers) {
        const auto substep = planetsim::climate_substep(month, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep, workers);
        return planetsim::step_surface_energy(
            state, parameters, surface, fractions, state.forcing().substep_mean_insolation_W_m2,
            planetsim::simulation_time_s(substep.length_ticks()), workers);
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;
    Planet wet(true);
    Planet serial(true);
    Planet dry(false);
    double worst_energy = 0.0;
    double worst_water = 0.0;
    double worst_cycle = 0.0;
    double first_latent_W_m2 = 0.0;
    double latent_W = 0.0;
    double evaporation_kg = 0.0;
    double duration = 0.0;
    double area = 0.0;
    for (const auto& cell : wet.mesh->cells()) {
        area += cell.area_m2;
    }
    planetsim::SurfaceEnergyDiagnostics last_wet;
    planetsim::SurfaceEnergyDiagnostics last_dry;
    for (std::int64_t month = 0; month < 12; ++month) {
        last_wet = wet.step(month, 4U);
        static_cast<void>(serial.step(month, 1U));
        last_dry = dry.step(month, 4U);
        worst_energy = std::max(worst_energy, last_wet.closure_residual_J() / last_wet.closure_gate_J());
        worst_water = std::max(worst_water, last_wet.water_residual_kg() / last_wet.water_gate_kg());
        worst_cycle = std::max(worst_cycle,
                               last_wet.water_cycle_residual_kg() / last_wet.water_cycle_gate_kg());
        if (month == 0) {
            first_latent_W_m2 = last_wet.evaporation_latent_J / last_wet.duration_s / area;
        }
        latent_W += last_wet.evaporation_latent_J;
        evaporation_kg += last_wet.water_evaporation_kg + last_wet.bucket_evaporation_kg +
                          last_wet.snow_sublimation_kg + last_wet.ice_sublimation_kg;
        duration += last_wet.duration_s;
    }
    const double latent_W_m2 = latent_W / duration / area;
    const double evaporation_m_yr = evaporation_kg / area / 1000.0 / duration * 365.25 * 86'400.0;
    // The bottom layer's area-weighted humidity relative to saturation at the
    // surface below it, after the year.
    const auto& slow = wet.state.slow();
    double humidity_m2 = 0.0;
    for (std::size_t cell = 0; cell < wet.mesh->cell_count(); ++cell) {
        humidity_m2 += wet.mesh->cells()[cell].area_m2 *
                       slow.atmosphere_specific_humidity_kg_kg.layer(0)[cell] /
                       planetsim::saturation_specific_humidity(
                           wet.state.forcing().surface_temperature_K[cell],
                           slow.atmosphere_surface_pressure_Pa[cell]);
    }
    const double relative_humidity = humidity_m2 / area;
    std::cout << "water cycle: energy " << worst_energy << " water " << worst_water << " cycle "
              << worst_cycle << " | latent first month " << first_latent_W_m2 << " W/m2, year "
              << latent_W_m2 << " W/m2, evaporation " << evaporation_m_yr
              << " m/yr | bottom layer q / q_sat(T_s) " << relative_humidity << " | mean T wet "
              << last_wet.mean_surface_temperature_K << " K, dry "
              << last_dry.mean_surface_temperature_K << " K\n";
    // V3: energy, with the latent heat; V2: water, including the vapour.
    PLANETSIM_EXPECT(test, worst_energy <= 1.0);
    PLANETSIM_EXPECT(test, worst_water <= 1.0);
    PLANETSIM_EXPECT(test, worst_cycle <= 1.0);
    // From the initial 60 % the first month evaporates tens of W/m². With no
    // rainout yet (M7-03) and no humidity transport (M7-04), the bottom layer
    // then fills to about the surface's saturation and the exchange stops
    // (it is supersaturated at its own, colder, temperature until rainout).
    PLANETSIM_EXPECT(test, first_latent_W_m2 > 20.0 && first_latent_W_m2 < 200.0);
    PLANETSIM_EXPECT(test, std::abs(latent_W_m2) < first_latent_W_m2);
    PLANETSIM_EXPECT(test, relative_humidity > 0.8 && relative_humidity < 1.3);
    // The stores stay within their bounds.
    bool bounded = true;
    for (std::size_t cell = 0; cell < wet.mesh->cell_count(); ++cell) {
        const double bucket = slow.land_surface_water_kg_m2[cell];
        bounded = bounded && bucket >= 0.0 && bucket <= planetsim::bucket_capacity_kg_m2 &&
                  slow.atmosphere_specific_humidity_kg_kg.layer(0)[cell] >= 0.0;
    }
    PLANETSIM_EXPECT(test, bounded);
    // Deterministic: 1 and 4 workers agree.
    bool same = true;
    for (std::size_t cell = 0; cell < wet.mesh->cell_count(); ++cell) {
        same = same &&
               slow.atmosphere_specific_humidity_kg_kg.layer(0)[cell] ==
                   serial.state.slow().atmosphere_specific_humidity_kg_kg.layer(0)[cell] &&
               slow.land_surface_water_kg_m2[cell] ==
                   serial.state.slow().land_surface_water_kg_m2[cell];
    }
    PLANETSIM_EXPECT(test, same);
    return test.result();
}
