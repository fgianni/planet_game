#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {

using planetsim::PlanetPreset;

constexpr std::uint64_t test_seed = 20'261'001U;
// Not the calibrated value (M5-04): a greenhouse that gives this planet
// seasonal sea ice and snow-free land.
constexpr double test_optical_depth = 3.0;

[[nodiscard]] std::shared_ptr<const planetsim::PlanetMesh> mesh_at(std::uint32_t level) {
    return std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, 6'371'000.0));
}

// The Earth-like planet with ADR-0010's atmosphere replacing the grey layer.
struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface =
        planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh, std::uint32_t layers)
        : mesh(std::move(shared_mesh)), state(mesh) {
        surface.grey_emissivity = 0.0;
        surface.atmosphere.layer_count = layers;
        surface.atmosphere.longwave_optical_depth = test_optical_depth;
        static_cast<void>(planetsim::generate_terrain(
            state, test_seed, planetsim::geology_parameters_for(PlanetPreset::earth_like), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_surface_temperatures(*mesh, state.slow(), parameters, surface, 4U);
        planetsim::initialise_cryosphere(*mesh, state.slow());
        planetsim::initialise_atmosphere(*mesh, state.slow(), parameters, surface.atmosphere);
    }

    planetsim::SurfaceEnergyDiagnostics climate_step(std::int64_t index, std::size_t workers) {
        const auto substep = planetsim::climate_substep(index, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep, workers);
        return planetsim::step_surface_energy(
            state, parameters, surface, fractions, state.forcing().substep_mean_insolation_W_m2,
            planetsim::simulation_time_s(substep.length_ticks()), workers);
    }

    planetsim::SurfaceEnergyDiagnostics reference_step(planetsim::SimulationTick begin,
                                                       std::size_t workers) {
        planetsim::update_solar_forcing(state, parameters, begin + 5, workers);
        return planetsim::step_surface_energy(state, parameters, surface, fractions,
                                              state.forcing().top_of_atmosphere_insolation_W_m2,
                                              600.0, workers);
    }
};

struct Worst {
    double closure = 0.0;
    double water = 0.0;
    double column_residual_W_m2 = 0.0;
    double transport_sum = 0.0;
    std::size_t unconverged = 0;
    int column_iterations = 0;
    bool structured = true;

    void record(const planetsim::SurfaceEnergyDiagnostics& step) {
        closure = std::max(closure, step.closure_residual_J() / step.closure_gate_J());
        water = std::max(water, step.water_residual_kg() / step.water_gate_kg());
        column_residual_W_m2 = std::max(column_residual_W_m2, step.max_column_residual_W_m2);
        transport_sum = std::max(transport_sum, std::abs(step.transport_W) /
                                                    std::max(1.0, step.transport_absolute_W));
        unconverged += step.unconverged_columns;
        column_iterations = std::max(column_iterations, step.max_column_iterations);
        // Longwave leaving the top is less than the surface sends up (a
        // greenhouse), the surface receives longwave from the air, and the
        // layers cool upward. The top layer of five sits in radiative
        // equilibrium above the convective layers and may be slightly warmer
        // than the one below (a first tropopause, ADR-0010 §6).
        structured = structured && step.emitted_W < step.surface_upward_longwave_W &&
                     step.downward_longwave_W > 0.0;
        const std::size_t layers = step.atmosphere_layers;
        const auto& t = step.mean_layer_temperature_K;
        for (std::size_t layer = 1; layer + 1U < layers; ++layer) {
            structured = structured && t[layer] < t[layer - 1U];
        }
        if (layers > 1U) {
            structured = structured && t[layers - 1U] < t[0] &&
                         t[layers - 1U] < t[layers - 2U] + 5.0;
        }
    }
};

// ADR-0010 V5: every climate sub-step of the given years and a day of
// ten-minute reference steps close the energy budget, surface and
// atmosphere together, under the ADR-0007 V2 gate; the water budget
// closes; every column converges.
void check_budgets(planetsim::test::Context& test, std::uint32_t layers, int years) {
    Planet planet(mesh_at(3U), layers);
    Worst climate;
    double convective = 0.0;
    for (std::int64_t index = 0; index < years * planetsim::climate_substeps_per_year; ++index) {
        const auto step = planet.climate_step(index, 4U);
        climate.record(step);
        convective = std::max(convective, step.convective_area_m2);
    }
    Worst reference;
    const planetsim::SimulationTick begin =
        planetsim::climate_substep(years * planetsim::climate_substeps_per_year,
                                   planet.parameters)
            .begin_tick;
    for (int step = 0; step < 144; ++step) {
        reference.record(planet.reference_step(begin + 10 * step, 4U));
    }
    const double planet_area = 4.0 * std::numbers::pi * 6'371'000.0 * 6'371'000.0;
    std::cout << "budgets layers=" << layers << " climate closure=" << climate.closure
              << " water=" << climate.water << " column_residual=" << climate.column_residual_W_m2
              << " iterations=" << climate.column_iterations
              << " unconverged=" << climate.unconverged
              << " convective_fraction=" << convective / planet_area
              << " | reference closure=" << reference.closure
              << " column_residual=" << reference.column_residual_W_m2
              << " unconverged=" << reference.unconverged << '\n';
    // The column's tolerance is 1e-10 W/m² or its rounding floor
    // 16 ε (C/Δt) T, about 5e-9 W/m² on a ten-minute step.
    PLANETSIM_EXPECT(test, climate.column_residual_W_m2 <= 1e-9);
    PLANETSIM_EXPECT(test, reference.column_residual_W_m2 <= 2e-8);
    for (const Worst* worst : {&climate, &reference}) {
        PLANETSIM_EXPECT(test, worst->closure <= 1.0);
        PLANETSIM_EXPECT(test, worst->water <= 1.0);
        PLANETSIM_EXPECT(test, worst->unconverged == 0U);
        PLANETSIM_EXPECT(test, worst->transport_sum <= 1e-12);
        PLANETSIM_EXPECT(test, worst->structured);
    }
    PLANETSIM_EXPECT(test, convective > 0.5 * planet_area);
}

// ADR-0010 V7: a year of climate steps is bit-identical on 1 and 8 workers.
void check_determinism(planetsim::test::Context& test) {
    Planet single(mesh_at(3U), 3U);
    Planet parallel(mesh_at(3U), 3U);
    for (std::int64_t index = 0; index < planetsim::climate_substeps_per_year; ++index) {
        static_cast<void>(single.climate_step(index, 1U));
        static_cast<void>(parallel.climate_step(index, 8U));
    }
    PLANETSIM_EXPECT(test, planetsim::slow_state_hash(single.state) ==
                               planetsim::slow_state_hash(parallel.state));
}

// The step refuses an atmosphere that does not match the state, and the
// grey layer under an atmosphere.
void check_refusals(planetsim::test::Context& test) {
    Planet planet(mesh_at(2U), 3U);
    planet.surface.atmosphere.layer_count = 5U;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, planet.climate_step(0, 1U));
    planet.surface.atmosphere.layer_count = 3U;
    planet.surface.grey_emissivity = 0.4;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, planet.climate_step(0, 1U));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_refusals(test);
    check_budgets(test, 3U, 3);
    check_budgets(test, 5U, 1);
    check_determinism(test);
    return test.result();
}
