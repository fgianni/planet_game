#include "sim/core/math/vec3d.hpp"
#include "sim/planet/orbit/orbit_state.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

int main() {
    planetsim::test::Context test;
    const auto parameters = planetsim::PlanetParameters::earth_reference();
    auto circular_parameters = parameters;
    circular_parameters.eccentricity = 0.0;
    constexpr double geometry_tolerance = 2.0e-15;

    const auto epoch = planetsim::evaluate_orbit(parameters, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.rotation_angle_rad, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.orbital_phase_rad, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.solar_declination_rad, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.rotation_axis_inertial_unit.x, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.rotation_axis_inertial_unit.y, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.rotation_axis_inertial_unit.z, 1.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_inertial_unit.x, 1.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_inertial_unit.y, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_inertial_unit.z, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_body_unit.x, 1.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_body_unit.y, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, epoch.sun_direction_body_unit.z, 0.0, 0.0);

    const auto northern_solstice =
        planetsim::evaluate_orbit(circular_parameters, 0.25 * circular_parameters.orbital_period_s);
    PLANETSIM_EXPECT_NEAR(test, northern_solstice.orbital_phase_rad,
                          0.5 * std::numbers::pi_v<double>, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, northern_solstice.solar_declination_rad,
                          circular_parameters.axial_tilt_rad, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, northern_solstice.sun_direction_inertial_unit.x, 0.0,
                          geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, northern_solstice.sun_direction_inertial_unit.y,
                          std::cos(circular_parameters.axial_tilt_rad), geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, northern_solstice.sun_direction_inertial_unit.z,
                          std::sin(circular_parameters.axial_tilt_rad), geometry_tolerance);

    const auto autumn_equinox =
        planetsim::evaluate_orbit(circular_parameters, 0.5 * circular_parameters.orbital_period_s);
    PLANETSIM_EXPECT_NEAR(test, autumn_equinox.solar_declination_rad, 0.0, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, autumn_equinox.sun_direction_inertial_unit.x, -1.0,
                          geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, autumn_equinox.sun_direction_inertial_unit.z, 0.0,
                          geometry_tolerance);

    const auto southern_solstice =
        planetsim::evaluate_orbit(circular_parameters, 0.75 * circular_parameters.orbital_period_s);
    PLANETSIM_EXPECT_NEAR(test, southern_solstice.solar_declination_rad,
                          -circular_parameters.axial_tilt_rad, geometry_tolerance);

    auto day_cycle_parameters = circular_parameters;
    day_cycle_parameters.axial_tilt_rad = 0.0;
    const double solar_day_s = 1.0 / (1.0 / day_cycle_parameters.sidereal_rotation_period_s -
                                      1.0 / day_cycle_parameters.orbital_period_s);
    const auto local_midnight = planetsim::evaluate_orbit(day_cycle_parameters, 0.5 * solar_day_s);
    PLANETSIM_EXPECT_NEAR(test, local_midnight.sun_direction_body_unit.x, -1.0, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, local_midnight.sun_direction_body_unit.y, 0.0, geometry_tolerance);
    const auto next_local_noon = planetsim::evaluate_orbit(day_cycle_parameters, solar_day_s);
    PLANETSIM_EXPECT_NEAR(test, next_local_noon.sun_direction_body_unit.x, 1.0, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, next_local_noon.sun_direction_body_unit.y, 0.0, geometry_tolerance);

    const auto periapsis = planetsim::evaluate_orbit(parameters, 0.0);
    const auto apoapsis = planetsim::evaluate_orbit(parameters, 0.5 * parameters.orbital_period_s);
    PLANETSIM_EXPECT_NEAR(test, periapsis.orbital_distance_m,
                          parameters.semi_major_axis_m * (1.0 - parameters.eccentricity), 1.0e-4);
    PLANETSIM_EXPECT_NEAR(test, apoapsis.orbital_distance_m,
                          parameters.semi_major_axis_m * (1.0 + parameters.eccentricity), 1.0e-4);
    const double expected_flux_ratio =
        std::pow((1.0 + parameters.eccentricity) / (1.0 - parameters.eccentricity), 2.0);
    PLANETSIM_EXPECT_NEAR(test,
                          periapsis.incident_solar_flux_W_m2 / apoapsis.incident_solar_flux_W_m2,
                          expected_flux_ratio, 2.0e-15);

    for (const double eccentricity : {0.0, 0.0167086, 0.5, 0.95}) {
        for (const double mean_anomaly : {0.0, 0.1, 1.0, 3.0, 5.9}) {
            const double eccentric_anomaly =
                planetsim::solve_eccentric_anomaly_rad(mean_anomaly, eccentricity);
            PLANETSIM_EXPECT_NEAR(test,
                                  eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly),
                                  mean_anomaly, 2.0e-15);
        }
    }

    const auto one_rotation =
        planetsim::evaluate_orbit(parameters, parameters.sidereal_rotation_period_s);
    PLANETSIM_EXPECT_NEAR(test, one_rotation.rotation_angle_rad, 0.0, 0.0);

    const auto one_orbit = planetsim::evaluate_orbit(parameters, parameters.orbital_period_s);
    PLANETSIM_EXPECT_NEAR(test, one_orbit.orbital_phase_rad, 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, one_orbit.sun_direction_inertial_unit.x, 1.0, 0.0);

    auto phased_parameters = parameters;
    phased_parameters.initial_rotation_angle_rad = -0.25;
    phased_parameters.initial_orbital_phase_rad = 2.0 * std::numbers::pi_v<double> + 0.75;
    const auto phased_epoch = planetsim::evaluate_orbit(phased_parameters, 0.0);
    PLANETSIM_EXPECT_NEAR(test, phased_epoch.rotation_angle_rad,
                          2.0 * std::numbers::pi_v<double> - 0.25, geometry_tolerance);
    PLANETSIM_EXPECT_NEAR(test, phased_epoch.orbital_phase_rad, 0.75, geometry_tolerance);

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::evaluate_orbit(parameters, -1.0));
    PLANETSIM_EXPECT_THROWS(
        test, std::invalid_argument,
        planetsim::evaluate_orbit(parameters, std::numeric_limits<double>::infinity()));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::solve_eccentric_anomaly_rad(0.0, 1.0));

    return test.result();
}
