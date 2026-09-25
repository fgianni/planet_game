#pragma once

#include <cstdint>

namespace planetsim {

struct PlanetParameters {
    double radius_m = 6'371'000.0;
    double mass_kg = 5.9722e24;
    std::uint32_t mesh_subdivision = 5;
    double sidereal_rotation_period_s = 86'164.0905;
    double orbital_period_s = 31'556'925.216;
    double axial_tilt_rad = 0.4090926006005829;
    double semi_major_axis_m = 149'597'870'700.0;
    double eccentricity = 0.0167086;
    double longitude_periapsis_rad = 3.14159265358979323846;
    double star_luminosity_W = 3.827531850364947e26;
    double initial_rotation_angle_rad = 0.0;
    double initial_orbital_phase_rad = 0.0;

    [[nodiscard]] static PlanetParameters earth_development() noexcept;
    [[nodiscard]] static PlanetParameters earth_reference() noexcept;
    void validate() const;
};

}  // namespace planetsim
