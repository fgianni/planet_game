#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstddef>

namespace planetsim {

class PlanetState;
struct PlanetParameters;

[[nodiscard]] double solar_zenith_cosine(const Vec3d& surface_normal_unit,
                                         const Vec3d& sun_direction_body_unit) noexcept;

[[nodiscard]] double top_of_atmosphere_insolation_W_m2(const Vec3d& surface_normal_unit,
                                                       const Vec3d& sun_direction_body_unit,
                                                       double incident_solar_flux_W_m2) noexcept;

void update_solar_forcing(PlanetState& state, const PlanetParameters& parameters,
                          SimulationTick tick, std::size_t worker_count = 1U);

}  // namespace planetsim
