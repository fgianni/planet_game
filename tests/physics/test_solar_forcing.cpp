#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_diagnostics.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <stdexcept>

int main() {
    planetsim::test::Context test;
    constexpr double incident_solar_flux_W_m2 = 1'361.0;
    constexpr double geometry_tolerance = 2.0e-12;
    const planetsim::Vec3d equinox_sun{1.0, 0.0, 0.0};

    PLANETSIM_EXPECT_NEAR(test,
                          planetsim::top_of_atmosphere_insolation_W_m2({1.0, 0.0, 0.0}, equinox_sun,
                                                                       incident_solar_flux_W_m2),
                          incident_solar_flux_W_m2, 0.0);
    PLANETSIM_EXPECT_NEAR(test,
                          planetsim::top_of_atmosphere_insolation_W_m2(
                              {-1.0, 0.0, 0.0}, equinox_sun, incident_solar_flux_W_m2),
                          0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test,
                          planetsim::top_of_atmosphere_insolation_W_m2({0.0, 1.0, 0.0}, equinox_sun,
                                                                       incident_solar_flux_W_m2),
                          0.0, 0.0);

    const double latitude_rad = 0.25 * std::numbers::pi_v<double>;
    const planetsim::Vec3d north_equinox{std::cos(latitude_rad), 0.0, std::sin(latitude_rad)};
    const planetsim::Vec3d south_equinox{std::cos(latitude_rad), 0.0, -std::sin(latitude_rad)};
    const double north_equinox_flux = planetsim::top_of_atmosphere_insolation_W_m2(
        north_equinox, equinox_sun, incident_solar_flux_W_m2);
    const double south_equinox_flux = planetsim::top_of_atmosphere_insolation_W_m2(
        south_equinox, equinox_sun, incident_solar_flux_W_m2);
    PLANETSIM_EXPECT_NEAR(test, north_equinox_flux, south_equinox_flux, geometry_tolerance);

    auto parameters = planetsim::PlanetParameters::earth_development();
    // L5 has 41 fixed logical blocks, so the 2/8/16-worker cases below all
    // exercise genuinely concurrent execution rather than collapsing to one worker.
    parameters.mesh_subdivision = 5;
    parameters.eccentricity = 0.0;
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(parameters.mesh_subdivision, parameters.radius_m));
    planetsim::PlanetState state(mesh);
    const auto northern_solstice_tick = static_cast<planetsim::SimulationTick>(
        std::llround(0.25 * parameters.orbital_period_s /
                     static_cast<double>(planetsim::simulation_seconds_per_tick)));
    planetsim::update_solar_forcing(state, parameters, northern_solstice_tick, 1U);

    const auto& sun = state.forcing().orbit.sun_direction_body_unit;
    const double horizontal_length = std::hypot(sun.x, sun.y);
    const planetsim::Vec3d horizontal_sun{sun.x / horizontal_length, sun.y / horizontal_length,
                                          0.0};
    const planetsim::Vec3d north_solstice =
        horizontal_sun * std::cos(latitude_rad) +
        planetsim::Vec3d{0.0, 0.0, 1.0} * std::sin(latitude_rad);
    const planetsim::Vec3d south_solstice =
        horizontal_sun * std::cos(latitude_rad) -
        planetsim::Vec3d{0.0, 0.0, 1.0} * std::sin(latitude_rad);
    const double north_solstice_flux =
        planetsim::top_of_atmosphere_insolation_W_m2(north_solstice, sun, incident_solar_flux_W_m2);
    const double south_solstice_flux =
        planetsim::top_of_atmosphere_insolation_W_m2(south_solstice, sun, incident_solar_flux_W_m2);
    PLANETSIM_EXPECT(test, north_solstice_flux > south_solstice_flux);
    PLANETSIM_EXPECT(test, planetsim::top_of_atmosphere_insolation_W_m2(
                               {0.0, 0.0, 1.0}, sun, incident_solar_flux_W_m2) > 0.0);
    PLANETSIM_EXPECT_NEAR(test,
                          planetsim::top_of_atmosphere_insolation_W_m2({0.0, 0.0, -1.0}, sun,
                                                                       incident_solar_flux_W_m2),
                          0.0, 0.0);

    const auto diagnostics = planetsim::analyze_solar_forcing(state);
    PLANETSIM_EXPECT(test, diagnostics.forcing_valid());
    PLANETSIM_EXPECT(test, diagnostics.illuminated_cell_count + diagnostics.night_cell_count ==
                               mesh->cell_count());
    PLANETSIM_EXPECT_NEAR(test, diagnostics.min_insolation_W_m2, 0.0, 0.0);
    PLANETSIM_EXPECT(test,
                     diagnostics.max_insolation_W_m2 <= state.forcing().incident_solar_flux_W_m2);

    const auto single_thread_diagnostics = planetsim::analyze_solar_forcing(state, 1U);
    for (const std::size_t worker_count : std::array<std::size_t, 3>{2U, 8U, 16U}) {
        planetsim::PlanetState repeated_state(mesh);
        planetsim::update_solar_forcing(repeated_state, parameters, northern_solstice_tick,
                                        worker_count);
        PLANETSIM_EXPECT(test, state.forcing().orbit.sun_direction_body_unit.x ==
                                   repeated_state.forcing().orbit.sun_direction_body_unit.x);
        for (std::size_t index = 0; index < mesh->cell_count(); ++index) {
            PLANETSIM_EXPECT(test,
                             state.forcing().top_of_atmosphere_insolation_W_m2[index] ==
                                 repeated_state.forcing().top_of_atmosphere_insolation_W_m2[index]);
        }
        const auto threaded_diagnostics =
            planetsim::analyze_solar_forcing(repeated_state, worker_count);
        PLANETSIM_EXPECT(test, single_thread_diagnostics.total_incoming_power_W ==
                                   threaded_diagnostics.total_incoming_power_W);
        PLANETSIM_EXPECT(test, single_thread_diagnostics.global_mean_insolation_W_m2 ==
                                   threaded_diagnostics.global_mean_insolation_W_m2);
        PLANETSIM_EXPECT(test, single_thread_diagnostics.relative_global_mean_error ==
                                   threaded_diagnostics.relative_global_mean_error);
    }

    planetsim::SimulationClock direct_clock;
    direct_clock.set_tick(northern_solstice_tick);
    planetsim::SimulationClock batched_clock;
    const auto quarter_ticks = northern_solstice_tick / 4;
    batched_clock.advance_ticks(quarter_ticks);
    batched_clock.advance_ticks(quarter_ticks);
    batched_clock.advance_ticks(quarter_ticks);
    batched_clock.advance_ticks(northern_solstice_tick - 3 * quarter_ticks);
    PLANETSIM_EXPECT(test, direct_clock.tick() == batched_clock.tick());

    planetsim::PlanetState invalid_worker_state(mesh);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::update_solar_forcing(invalid_worker_state, parameters,
                                                            northern_solstice_tick, 0U));

    return test.result();
}
