#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <memory>
#include <stdexcept>

int main() {
    planetsim::test::Context test;

    auto mesh = std::make_shared<const planetsim::PlanetMesh>(planetsim::make_icosphere(0, 1.0));
    planetsim::PlanetState state(mesh);
    PLANETSIM_EXPECT(test, state.mesh().cell_count() == 20);
    PLANETSIM_EXPECT(test, state.forcing().top_of_atmosphere_insolation_W_m2.size() == 20);
    PLANETSIM_EXPECT_NEAR(test, state.forcing().top_of_atmosphere_insolation_W_m2[0], 0.0, 0.0);

    planetsim::SimulationClock clock;
    clock.advance(15.0);
    const auto parameters = planetsim::PlanetParameters::earth_development();
    planetsim::update_solar_forcing(state, parameters, clock.time_s());
    const auto snapshot = planetsim::make_state_snapshot(state, clock);
    PLANETSIM_EXPECT(test, snapshot.schema_version == planetsim::state_snapshot_schema_version);
    PLANETSIM_EXPECT_NEAR(test, snapshot.simulation_time_s, 15.0, 0.0);
    PLANETSIM_EXPECT(test, snapshot.step_count == 1);
    PLANETSIM_EXPECT(test, snapshot.top_of_atmosphere_insolation_W_m2.size() == 20);
    PLANETSIM_EXPECT_NEAR(test, snapshot.rotation_angle_rad,
                          state.forcing().orbit.rotation_angle_rad, 0.0);
    PLANETSIM_EXPECT_NEAR(test, snapshot.orbital_phase_rad, state.forcing().orbit.orbital_phase_rad,
                          0.0);
    PLANETSIM_EXPECT_NEAR(test, snapshot.orbital_distance_m,
                          state.forcing().orbit.orbital_distance_m, 0.0);
    PLANETSIM_EXPECT_NEAR(test, snapshot.incident_solar_flux_W_m2,
                          state.forcing().incident_solar_flux_W_m2, 0.0);
    PLANETSIM_EXPECT_NEAR(test, snapshot.sun_direction_body_unit.x,
                          state.forcing().orbit.sun_direction_body_unit.x, 0.0);

    const float copied_insolation = snapshot.top_of_atmosphere_insolation_W_m2[0];
    state.forcing().top_of_atmosphere_insolation_W_m2[0] = 0.0;
    PLANETSIM_EXPECT_NEAR(test, snapshot.top_of_atmosphere_insolation_W_m2[0], copied_insolation,
                          0.0);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::PlanetState(std::shared_ptr<const planetsim::PlanetMesh>{}));

    return test.result();
}
