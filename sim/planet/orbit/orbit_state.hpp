#pragma once

#include "sim/core/math/vec3d.hpp"

namespace planetsim {

struct PlanetParameters;

struct OrbitState {
    double rotation_angle_rad = 0.0;
    double orbital_phase_rad = 0.0;
    double eccentric_anomaly_rad = 0.0;
    double true_anomaly_rad = 0.0;
    double solar_longitude_rad = 0.0;
    double solar_declination_rad = 0.0;
    double orbital_distance_m = 0.0;
    double incident_solar_flux_W_m2 = 0.0;
    Vec3d rotation_axis_inertial_unit{0.0, 0.0, 1.0};
    Vec3d sun_direction_inertial_unit{1.0, 0.0, 0.0};
    Vec3d sun_direction_body_unit{1.0, 0.0, 0.0};
};

[[nodiscard]] double solve_eccentric_anomaly_rad(double mean_anomaly_rad, double eccentricity);
[[nodiscard]] OrbitState evaluate_orbit(const PlanetParameters& parameters,
                                        double simulation_time_s);

}  // namespace planetsim
