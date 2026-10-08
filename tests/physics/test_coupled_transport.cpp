#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/dynamics/pressure_redistribution.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

// The coupled climate transport (ADR-0011 §17; task M6-05 step C): the
// circulation first in each climate step, carrying the heat.
namespace {

struct YearRecord {
    bool every_month_coupled = true;
    double worst_closure = 0.0;      // closure residual over the ADR-0007 V2 gate
    double worst_imbalance = 0.0;    // |Σ A H| over Σ A |H|
    double worst_consistency_W_m2 = 0.0;
};

// One climate year, month by month.
[[nodiscard]] YearRecord run_year(planetsim::PlanetRun& run) {
    YearRecord record;
    for (std::int64_t month = 0; month < planetsim::climate_substeps_per_year; ++month) {
        run.run_until(planetsim::climate_substep(month, run.parameters()).end_tick);
        const auto& step = run.last_step();
        record.every_month_coupled = record.every_month_coupled && step.transport_coupled;
        record.worst_closure =
            std::max(record.worst_closure, step.closure_residual_J() / step.closure_gate_J());
        record.worst_imbalance = std::max(
            record.worst_imbalance, std::abs(step.transport_W) / step.transport_absolute_W);
        record.worst_consistency_W_m2 =
            std::max(record.worst_consistency_W_m2, step.transport_consistency_W_m2);
    }
    return record;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    using planetsim::ClimateCirculationUse;

    // 1. V10 with three layers: every climate step is coupled and closes its
    // energy budget; the transport sums to zero; 1 and 4 workers agree (V11).
    {
        planetsim::PlanetRun coupled(scenario, 4U, ClimateCirculationUse::coupled);
        planetsim::PlanetRun serial(scenario, 1U, ClimateCirculationUse::coupled);
        planetsim::PlanetRun diagnosed(scenario, 4U, ClimateCirculationUse::diagnostic);
        const auto year = run_year(coupled);
        static_cast<void>(run_year(serial));
        static_cast<void>(run_year(diagnosed));
        std::cout << "N=3 closure=" << year.worst_closure << " imbalance=" << year.worst_imbalance
                  << " consistency_W_m2=" << year.worst_consistency_W_m2
                  << " mean_K=" << coupled.last_step().mean_surface_temperature_K << '\n';
        PLANETSIM_EXPECT(test, year.every_month_coupled);
        PLANETSIM_EXPECT(test, year.worst_closure <= 1.0);
        PLANETSIM_EXPECT(test, year.worst_imbalance <= 1.0e-10);
        PLANETSIM_EXPECT(test, year.worst_consistency_W_m2 <= 1.0e-6);
        PLANETSIM_EXPECT(test, coupled.state_hash() == serial.state_hash());
        PLANETSIM_EXPECT(test, coupled.state_hash() != diagnosed.state_hash());
        PLANETSIM_EXPECT(test, coupled.circulation()->diagnostics().failed_months == 0U);
        // Both the eddies and the overturning carry heat.
        const auto& last = coupled.last_step();
        PLANETSIM_EXPECT(test, last.transport_eddy_absolute_W > 0.0 &&
                                   last.transport_advective_absolute_W >
                                       0.05 * last.transport_eddy_absolute_W);

        // The balanced p_s is the slow state's (§17.3): written every month,
        // the atmosphere's mass held, energy closed to rounding.
        const auto& d = coupled.circulation()->diagnostics();
        std::cout << "p_s writes=" << d.pressure_writes
                  << " last max change_Pa=" << d.last_pressure.max_change_Pa
                  << " max dT_K=" << d.last_pressure.max_temperature_change_K
                  << " cg=" << d.last_pressure.cg_iterations
                  << " worst energy ratio=" << d.worst_pressure_energy_ratio << '\n';
        PLANETSIM_EXPECT(test, d.pressure_writes == 12U);
        PLANETSIM_EXPECT(test, d.worst_pressure_energy_ratio <= 1.0e-13);
        PLANETSIM_EXPECT(test, d.last_pressure.relative_residual <= 1.0e-6);
        const auto& slow = coupled.state().slow();
        const auto& balanced = coupled.state().circulation().balanced_surface_pressure_Pa;
        bool written = true;
        double mass = 0.0;
        double initial_mass = 0.0;
        for (std::size_t i = 0; i < balanced.size(); ++i) {
            written = written && slow.atmosphere_surface_pressure_Pa[i] == balanced[i];
            const double area = coupled.state().mesh().cells()[i].area_m2;
            mass += area * slow.atmosphere_surface_pressure_Pa[i];
            initial_mass += area * diagnosed.state().slow().atmosphere_surface_pressure_Pa[i];
        }
        PLANETSIM_EXPECT(test, written);
        PLANETSIM_EXPECT_NEAR(test, mass / initial_mass, 1.0, 1.0e-12);
    }

    // 2. V10 with five layers.
    {
        planetsim::Scenario five = scenario;
        five.atmosphere_layers = 5U;
        planetsim::PlanetRun coupled(five, 4U, ClimateCirculationUse::coupled);
        const auto year = run_year(coupled);
        std::cout << "N=5 closure=" << year.worst_closure << " imbalance=" << year.worst_imbalance
                  << " failed=" << coupled.circulation()->diagnostics().failed_months << '\n';
        PLANETSIM_EXPECT(test, year.worst_closure <= 1.0);
        PLANETSIM_EXPECT(test, year.worst_imbalance <= 1.0e-10);
        PLANETSIM_EXPECT(test, coupled.circulation()->diagnostics().failed_months > 0U ||
                                   year.every_month_coupled);
    }

    // 3. The fallback (§17.4): a step given an inactive circulation is
    // ADR-0009's diffusion, bit for bit.
    {
        const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(3U, 6'371'000.0));
        const auto parameters = planetsim::PlanetParameters::earth_development();
        const auto surface = planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);
        const auto make = [&] {
            auto state = std::make_unique<planetsim::PlanetState>(mesh);
            static_cast<void>(planetsim::generate_terrain(
                *state, 1U, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like),
                4U));
            return state;
        };
        auto plain = make();
        auto fallback = make();
        const auto fractions = planetsim::compute_surface_fractions(
            *mesh, plain->slow().hypsometry_m, plain->slow().sea_level_m, 4U);
        for (auto* state : {plain.get(), fallback.get()}) {
            planetsim::initialise_climate(*mesh, state->slow(), parameters, surface, 4U);
            planetsim::update_substep_mean_insolation(
                *state, parameters, planetsim::climate_substep(0, parameters), 4U);
        }
        const double dt = planetsim::simulation_time_s(
            planetsim::climate_substep(0, parameters).length_ticks());
        planetsim::CirculationTransport inactive;
        static_cast<void>(planetsim::step_surface_energy(
            *plain, parameters, surface, fractions, plain->forcing().substep_mean_insolation_W_m2,
            dt, 4U));
        const auto step = planetsim::step_surface_energy(
            *fallback, parameters, surface, fractions,
            fallback->forcing().substep_mean_insolation_W_m2, dt, 4U, &inactive);
        PLANETSIM_EXPECT(test, !step.transport_coupled);
        PLANETSIM_EXPECT(test,
                         planetsim::slow_state_hash(*plain) == planetsim::slow_state_hash(*fallback));
    }

    // 4. The redistribution alone: an imposed change of p_s (mass held)
    // moves energy between columns and conserves it; no change, no change.
    {
        const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(3U, 6'371'000.0));
        const auto parameters = planetsim::PlanetParameters::earth_development();
        const auto surface =
            planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);
        planetsim::PlanetState state(mesh);
        static_cast<void>(planetsim::generate_terrain(
            state, 1U, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like), 4U));
        const auto fractions = planetsim::compute_surface_fractions(
            *mesh, state.slow().hypsometry_m, state.slow().sea_level_m, 4U);
        planetsim::initialise_climate(*mesh, state.slow(), parameters, surface, 4U);
        planetsim::Field2D<double> height;
        planetsim::compute_surface_height(*mesh, state.slow(), fractions, height);
        const std::vector<std::size_t>* group_of_cell = nullptr;
        const auto& graph = planetsim::agglomerated_transport_graph(*mesh, group_of_cell);
        const planetsim::PressureRedistribution redistribution(
            *mesh, graph, *group_of_cell, height, planetsim::surface_gravity_m_s2(parameters),
            planetsim::dry_air_gas_constant_J_kg_K, planetsim::dry_air_heat_capacity_J_kg_K);

        const auto before = state.slow().atmosphere_temperature_K;
        const auto same = redistribution.apply(state.slow(), state.slow().atmosphere_surface_pressure_Pa, 4U);
        double largest = 0.0;
        for (std::size_t l = 0; l < before.layer_count(); ++l) {
            for (std::size_t i = 0; i < mesh->cell_count(); ++i) {
                largest = std::max(largest, std::abs(state.slow().atmosphere_temperature_K.layer(l)[i] -
                                                     before.layer(l)[i]));
            }
        }
        PLANETSIM_EXPECT(test, same.cg_iterations == 0 && largest < 1.0e-10);

        planetsim::Field2D<double> changed = state.slow().atmosphere_surface_pressure_Pa;
        double old_mass = 0.0;
        double new_mass = 0.0;
        for (const auto& cell : mesh->cells()) {
            const std::size_t i = cell.id.to_index();
            old_mass += cell.area_m2 * changed[i];
            changed[i] *= 1.0 + 0.02 * cell.center_unit.z;
            new_mass += cell.area_m2 * changed[i];
        }
        for (std::size_t i = 0; i < changed.size(); ++i) {
            changed[i] *= old_mass / new_mass;
        }
        auto copy = state.slow();
        const auto moved = redistribution.apply(state.slow(), changed, 4U);
        static_cast<void>(redistribution.apply(copy, changed, 1U));
        std::cout << "redistribution: max change_Pa=" << moved.max_change_Pa
                  << " max dT_K=" << moved.max_temperature_change_K
                  << " enthalpy_J=" << moved.enthalpy_change_J
                  << " potential_J=" << moved.potential_change_J
                  << " energy_ratio=" << moved.energy_change_J / moved.energy_J
                  << " cg=" << moved.cg_iterations
                  << " residual=" << moved.relative_residual << '\n';
        PLANETSIM_EXPECT(test, std::abs(moved.energy_change_J) <= 1.0e-13 * moved.energy_J);
        PLANETSIM_EXPECT(test, moved.relative_residual <= 1.0e-6);
        PLANETSIM_EXPECT(test, moved.max_temperature_change_K > 0.0 &&
                                   moved.max_temperature_change_K < 20.0);
        bool identical = true;
        for (std::size_t l = 0; l < copy.atmosphere_layer_count(); ++l) {
            for (std::size_t i = 0; i < mesh->cell_count(); ++i) {
                identical = identical && copy.atmosphere_temperature_K.layer(l)[i] ==
                                             state.slow().atmosphere_temperature_K.layer(l)[i];
            }
        }
        PLANETSIM_EXPECT(test, identical);
    }
    return test.result();
}
