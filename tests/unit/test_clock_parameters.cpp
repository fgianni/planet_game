#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <limits>
#include <stdexcept>

int main() {
    planetsim::test::Context test;

    planetsim::SimulationClock clock;
    PLANETSIM_EXPECT_NEAR(test, clock.time_s(), 0.0, 0.0);
    PLANETSIM_EXPECT(test, clock.step_count() == 0);

    clock.advance(60.0);
    clock.advance(30.0);
    PLANETSIM_EXPECT_NEAR(test, clock.time_s(), 90.0, 0.0);
    PLANETSIM_EXPECT(test, clock.step_count() == 2);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, clock.advance(0.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, clock.advance(-1.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            clock.advance(std::numeric_limits<double>::infinity()));

    clock.reset();
    PLANETSIM_EXPECT_NEAR(test, clock.time_s(), 0.0, 0.0);
    PLANETSIM_EXPECT(test, clock.step_count() == 0);

    auto parameters = planetsim::PlanetParameters::earth_reference();
    parameters.validate();
    PLANETSIM_EXPECT_NEAR(test, parameters.radius_m, 6'371'000.0, 0.0);
    PLANETSIM_EXPECT(test, parameters.mesh_subdivision == 6);

    parameters.radius_m = -1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    return test.result();
}
