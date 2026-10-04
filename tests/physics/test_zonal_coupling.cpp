#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/dynamics/orography.hpp"
#include "sim/planet/dynamics/zonal_coupling.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// The climate mode's zonal circulation on the Earth-like planet (ADR-0011
// §4.4 step 1, §14, task M6-04 step B part 2): the column physics' heating
// at the slow state, its band means, and one month's circulation.
int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 4;
    scenario.spin_up_years = 1;
    planetsim::PlanetRun run(scenario, 4U);
    const auto& mesh = run.state().mesh();
    const auto& surface = run.surface_parameters();
    const std::size_t cells = mesh.cell_count();
    const std::size_t n = surface.atmosphere.layer_count;
    const auto begin = run.tick();
    run.run_until(planetsim::climate_substep_containing(begin, run.parameters()).end_tick);
    const auto& insolation = run.state().forcing().substep_mean_insolation_W_m2;

    // 1. The heating: the same for any worker count, damped in every layer,
    // and its column sum is the longwave and sensible budget of the column.
    planetsim::AtmosphereHeating heating;
    planetsim::compute_atmosphere_heating(run.state(), run.parameters(), surface, run.fractions(),
                                          insolation, heating, 1U);
    planetsim::AtmosphereHeating parallel;
    planetsim::compute_atmosphere_heating(run.state(), run.parameters(), surface, run.fractions(),
                                          insolation, parallel, 4U);
    bool identical = true;
    bool damped = true;
    double budget_error = 0.0;
    const double gravity = planetsim::surface_gravity_m_s2(run.parameters());
    for (std::size_t i = 0; i < cells; ++i) {
        const double capacity = planetsim::dry_air_heat_capacity_J_kg_K *
                                run.state().slow().atmosphere_surface_pressure_Pa[i] /
                                (gravity * static_cast<double>(n));
        double column = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            identical = identical &&
                        heating.rate_K_s.layer(k)[i] == parallel.rate_K_s.layer(k)[i] &&
                        heating.derivative_s.layer(k)[i] == parallel.derivative_s.layer(k)[i];
            damped = damped && heating.derivative_s.layer(k)[i] < 0.0;
            column += heating.rate_K_s.layer(k)[i] * capacity;
        }
        const double budget = heating.surface_upward_W_m2[i] + heating.sensible_W_m2[i] -
                              heating.surface_downward_W_m2[i] - heating.outgoing_W_m2[i];
        budget_error = std::max(budget_error, std::abs(column - budget));
    }
    PLANETSIM_EXPECT(test, identical);
    PLANETSIM_EXPECT(test, damped);
    PLANETSIM_EXPECT_NEAR(test, budget_error, 0.0, 1e-9);

    // 2. The band means hold the atmosphere's mass and stay within the
    // cells' range.
    planetsim::Field2D<double> height;
    planetsim::compute_surface_height(mesh, run.state().slow(), run.fractions(), height);
    const auto dynamics_height =
        planetsim::limit_dynamics_orography_steps(mesh, height).height_m;
    constexpr std::size_t bands = 36;
    const auto forcing = planetsim::zonal_forcing_from_state(
        mesh, run.state().slow(), heating, dynamics_height, run.fractions(), bands);
    {
        const auto band_of = planetsim::zonal_band_of_cells(mesh, bands);
        std::vector<double> band_area(bands, 0.0);
        double mass = 0.0;
        double t_min = 1e9;
        double t_max = 0.0;
        for (std::size_t i = 0; i < cells; ++i) {
            band_area[band_of[i]] += mesh.cells()[i].area_m2;
            mass += mesh.cells()[i].area_m2 * run.state().slow().atmosphere_surface_pressure_Pa[i];
            for (std::size_t k = 0; k < n; ++k) {
                t_min = std::min(t_min, run.state().slow().atmosphere_temperature_K.layer(k)[i]);
                t_max = std::max(t_max, run.state().slow().atmosphere_temperature_K.layer(k)[i]);
            }
        }
        double band_mass = 0.0;
        for (std::size_t j = 0; j < bands; ++j) {
            band_mass += band_area[j] * forcing.surface_pressure_Pa[j];
        }
        PLANETSIM_EXPECT_NEAR(test, band_mass / mass, 1.0, 1e-12);
        for (const double t : forcing.temperature_K) {
            PLANETSIM_EXPECT(test, t >= t_min && t <= t_max);
        }
        for (const double lambda : forcing.heating_derivative_s) {
            PLANETSIM_EXPECT(test, lambda < 0.0);
        }
    }

    // 3. The month's circulation: converged, an eddy-driven jet in each
    // hemisphere's middle latitudes, and no net surface torque.
    const auto parameters =
        planetsim::zonal_circulation_parameters(run.parameters(), surface.atmosphere, bands);
    PLANETSIM_EXPECT(test, parameters.layer_count == n && parameters.critical_lapse_rate_K_m > 0.0);
    const planetsim::ZonalCirculation model(parameters);
    const auto solution = model.solve(forcing);
    PLANETSIM_EXPECT(test, solution.residual < parameters.tolerance);
    const std::size_t top = n - 1U;
    for (const bool north : {false, true}) {
        std::size_t jet = north ? bands / 2U : 0U;
        for (std::size_t j = north ? bands / 2U : 0U; j < (north ? bands : bands / 2U); ++j) {
            if (solution.at_band(solution.eastward_m_s, top, j) >
                solution.at_band(solution.eastward_m_s, top, jet)) {
                jet = j;
            }
        }
        const double latitude = std::abs(solution.latitude_deg[jet]);
        PLANETSIM_EXPECT(test, latitude >= 37.5 && latitude <= 67.5);
        PLANETSIM_EXPECT(test, solution.at_band(solution.eastward_m_s, top, jet) > 15.0);
    }
    PLANETSIM_EXPECT_NEAR(test, solution.total_torque_N_m, 0.0, 1e-9 * solution.gross_torque_N_m);
    return test.result();
}
