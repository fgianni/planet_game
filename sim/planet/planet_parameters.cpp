#include "sim/planet/planet_parameters.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {

PlanetParameters PlanetParameters::earth_development() noexcept { return {}; }

PlanetParameters PlanetParameters::earth_reference() noexcept {
    PlanetParameters parameters;
    parameters.mesh_subdivision = 6;
    return parameters;
}

void PlanetParameters::validate() const {
    if (!(radius_m > 0.0) || !std::isfinite(radius_m)) {
        throw std::invalid_argument("planet radius must be finite and positive");
    }
    if (!(mass_kg > 0.0) || !std::isfinite(mass_kg)) {
        throw std::invalid_argument("planet mass must be finite and positive");
    }
    if (!(sidereal_rotation_period_s > 0.0) || !std::isfinite(sidereal_rotation_period_s)) {
        throw std::invalid_argument("sidereal rotation period must be finite and positive");
    }
    if (!(orbital_period_s > 0.0) || !std::isfinite(orbital_period_s)) {
        throw std::invalid_argument("orbital period must be finite and positive");
    }
    if (!std::isfinite(axial_tilt_rad) || axial_tilt_rad < 0.0 ||
        axial_tilt_rad > std::numbers::pi_v<double>) {
        throw std::invalid_argument("axial tilt must be finite and in [0, pi]");
    }
    if (!(semi_major_axis_m > 0.0) || !std::isfinite(semi_major_axis_m)) {
        throw std::invalid_argument("semi-major axis must be finite and positive");
    }
    if (!std::isfinite(eccentricity) || eccentricity < 0.0 || eccentricity >= 1.0) {
        throw std::invalid_argument("eccentricity must be finite and in [0, 1)");
    }
    if (!std::isfinite(longitude_periapsis_rad)) {
        throw std::invalid_argument("longitude of periapsis must be finite");
    }
    if (!std::isfinite(star_luminosity_W) || star_luminosity_W < 0.0) {
        throw std::invalid_argument("star luminosity must be finite and non-negative");
    }
    if (!std::isfinite(initial_rotation_angle_rad)) {
        throw std::invalid_argument("initial rotation angle must be finite");
    }
    if (!std::isfinite(initial_orbital_phase_rad)) {
        throw std::invalid_argument("initial orbital phase must be finite");
    }
}

}  // namespace planetsim
