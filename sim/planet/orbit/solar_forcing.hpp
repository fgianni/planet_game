#pragma once

#include "sim/core/math/vec3d.hpp"

namespace planetsim {

class PlanetState;
struct PlanetParameters;

[[nodiscard]] double solar_zenith_cosine(const Vec3d& surface_normal_unit,
                                         const Vec3d& sun_direction_body_unit) noexcept;

[[nodiscard]] double top_of_atmosphere_insolation_W_m2(const Vec3d& surface_normal_unit,
                                                       const Vec3d& sun_direction_body_unit,
                                                       double incident_solar_flux_W_m2) noexcept;

void update_solar_forcing(PlanetState& state, const PlanetParameters& parameters,
                          double simulation_time_s);

}  // namespace planetsim
