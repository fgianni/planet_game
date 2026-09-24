#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <memory>
#include <stdexcept>

int main() {
    planetsim::test::Context test;

    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(0, 1.0));
    planetsim::PlanetState state(mesh, 0.25);
    PLANETSIM_EXPECT(test, state.mesh().cell_count() == 20);
    PLANETSIM_EXPECT(test, state.surface().debug_scalar.size() == 20);
    PLANETSIM_EXPECT_NEAR(test, state.surface().debug_scalar[0], 0.25, 0.0);

    planetsim::SimulationClock clock;
    clock.advance(15.0);
    const auto snapshot = planetsim::make_state_snapshot(state, clock);
    PLANETSIM_EXPECT_NEAR(test, snapshot.simulation_time_s, 15.0, 0.0);
    PLANETSIM_EXPECT(test, snapshot.step_count == 1);
    PLANETSIM_EXPECT(test, snapshot.surface_scalar.size() == 20);
    PLANETSIM_EXPECT_NEAR(test, snapshot.surface_scalar[0], 0.25, 0.0);

    state.surface().debug_scalar[0] = 0.75;
    PLANETSIM_EXPECT_NEAR(test, snapshot.surface_scalar[0], 0.25, 0.0);
    PLANETSIM_EXPECT_THROWS(
        test, std::invalid_argument,
        planetsim::PlanetState(std::shared_ptr<const planetsim::PlanetMesh>{}));

    return test.result();
}
