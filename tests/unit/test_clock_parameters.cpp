#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <limits>
#include <numbers>
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
    PLANETSIM_EXPECT_NEAR(test, parameters.mass_kg, 5.9722e24, 0.0);
    PLANETSIM_EXPECT(test, parameters.mesh_subdivision == 6);
    PLANETSIM_EXPECT_NEAR(test, parameters.sidereal_rotation_period_s, 86'164.0905, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.orbital_period_s, 31'556'925.216, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.axial_tilt_rad, 0.4090926006005829, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.semi_major_axis_m, 149'597'870'700.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.eccentricity, 0.0167086, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.longitude_periapsis_rad, std::numbers::pi_v<double>,
                          0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.star_luminosity_W, 3.827531850364947e26, 0.0);
    PLANETSIM_EXPECT(test, planetsim::PlanetParameters::earth_development().mesh_subdivision == 5);

    parameters.radius_m = -1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.sidereal_rotation_period_s = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.orbital_period_s = std::numeric_limits<double>::infinity();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.axial_tilt_rad = std::numbers::pi_v<double> + 0.01;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.mass_kg = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.semi_major_axis_m = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.eccentricity = 1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.longitude_periapsis_rad = std::numeric_limits<double>::quiet_NaN();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.star_luminosity_W = -1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.initial_rotation_angle_rad = std::numeric_limits<double>::quiet_NaN();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    return test.result();
}
