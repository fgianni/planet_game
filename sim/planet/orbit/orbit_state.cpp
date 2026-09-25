#include "sim/planet/orbit/orbit_state.hpp"

#include "sim/planet/planet_parameters.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {
namespace {

constexpr double full_turn_rad = 2.0 * std::numbers::pi_v<double>;

[[nodiscard]] double wrap_angle(double angle_rad) noexcept {
    double wrapped = std::fmod(angle_rad, full_turn_rad);
    if (wrapped < 0.0) {
        wrapped += full_turn_rad;
    }
    return wrapped;
}

[[nodiscard]] double phase_at_time(double initial_phase_rad, double period_s,
                                   double simulation_time_s) noexcept {
    const double elapsed_in_period_s = std::fmod(simulation_time_s, period_s);
    return wrap_angle(initial_phase_rad + full_turn_rad * elapsed_in_period_s / period_s);
}

[[nodiscard]] Vec3d rotate_about_z(const Vec3d& vector, double angle_rad) noexcept {
    const double cosine = std::cos(angle_rad);
    const double sine = std::sin(angle_rad);
    return {
        cosine * vector.x - sine * vector.y,
        sine * vector.x + cosine * vector.y,
        vector.z,
    };
}

}  // namespace

double solve_eccentric_anomaly_rad(double mean_anomaly_rad, double eccentricity) {
    if (!std::isfinite(mean_anomaly_rad)) {
        throw std::invalid_argument("mean anomaly must be finite");
    }
    if (!std::isfinite(eccentricity) || eccentricity < 0.0 || eccentricity >= 1.0) {
        throw std::invalid_argument("eccentricity must be finite and in [0, 1)");
    }

    const double mean_anomaly = wrap_angle(mean_anomaly_rad);
    if (mean_anomaly == 0.0 || eccentricity == 0.0) {
        return mean_anomaly;
    }

    double lower = 0.0;
    double upper = full_turn_rad;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double eccentric_anomaly = 0.5 * (lower + upper);
        const double residual =
            eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly) - mean_anomaly;
        if (residual > 0.0) {
            upper = eccentric_anomaly;
        } else {
            lower = eccentric_anomaly;
        }
    }
    return 0.5 * (lower + upper);
}

OrbitState evaluate_orbit(const PlanetParameters& parameters, double simulation_time_s) {
    parameters.validate();
    if (!std::isfinite(simulation_time_s) || simulation_time_s < 0.0) {
        throw std::invalid_argument("simulation time must be finite and non-negative");
    }

    OrbitState result;
    result.rotation_angle_rad =
        phase_at_time(parameters.initial_rotation_angle_rad, parameters.sidereal_rotation_period_s,
                      simulation_time_s);
    result.orbital_phase_rad = phase_at_time(parameters.initial_orbital_phase_rad,
                                             parameters.orbital_period_s, simulation_time_s);

    result.eccentric_anomaly_rad =
        solve_eccentric_anomaly_rad(result.orbital_phase_rad, parameters.eccentricity);
    const double cosine_eccentric_anomaly = std::cos(result.eccentric_anomaly_rad);
    const double sine_eccentric_anomaly = std::sin(result.eccentric_anomaly_rad);
    const double radial_factor = 1.0 - parameters.eccentricity * cosine_eccentric_anomaly;
    result.orbital_distance_m = parameters.semi_major_axis_m * radial_factor;

    const double cosine_true_anomaly =
        (cosine_eccentric_anomaly - parameters.eccentricity) / radial_factor;
    const double sine_true_anomaly =
        std::sqrt(1.0 - parameters.eccentricity * parameters.eccentricity) *
        sine_eccentric_anomaly / radial_factor;
    result.true_anomaly_rad = wrap_angle(std::atan2(sine_true_anomaly, cosine_true_anomaly));
    result.solar_longitude_rad = wrap_angle(parameters.longitude_periapsis_rad +
                                            result.true_anomaly_rad + std::numbers::pi_v<double>);

    const double cosine_solar_longitude = std::cos(result.solar_longitude_rad);
    const double sine_solar_longitude = std::sin(result.solar_longitude_rad);
    const double cosine_tilt = std::cos(parameters.axial_tilt_rad);
    const double sine_tilt = std::sin(parameters.axial_tilt_rad);

    result.sun_direction_inertial_unit = normalized({
        cosine_solar_longitude,
        cosine_tilt * sine_solar_longitude,
        sine_tilt * sine_solar_longitude,
    });
    result.incident_solar_flux_W_m2 =
        parameters.star_luminosity_W /
        (4.0 * std::numbers::pi_v<double> * result.orbital_distance_m * result.orbital_distance_m);
    result.solar_declination_rad =
        std::asin(std::clamp(result.sun_direction_inertial_unit.z, -1.0, 1.0));
    result.sun_direction_body_unit =
        normalized(rotate_about_z(result.sun_direction_inertial_unit, -result.rotation_angle_rad));
    return result;
}

}  // namespace planetsim
