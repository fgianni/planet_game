#include "sim/planet/orbit/solar_forcing.hpp"

#include "sim/planet/orbit/orbit_state.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>

namespace planetsim {

double solar_zenith_cosine(const Vec3d& surface_normal_unit,
                           const Vec3d& sun_direction_body_unit) noexcept {
    return std::clamp(dot(surface_normal_unit, sun_direction_body_unit), 0.0, 1.0);
}

double top_of_atmosphere_insolation_W_m2(const Vec3d& surface_normal_unit,
                                         const Vec3d& sun_direction_body_unit,
                                         double incident_solar_flux_W_m2) noexcept {
    return incident_solar_flux_W_m2 *
           solar_zenith_cosine(surface_normal_unit, sun_direction_body_unit);
}

void update_solar_forcing(PlanetState& state, const PlanetParameters& parameters,
                          double simulation_time_s) {
    auto& forcing = state.forcing();
    forcing.orbit = evaluate_orbit(parameters, simulation_time_s);
    forcing.incident_solar_flux_W_m2 = forcing.orbit.incident_solar_flux_W_m2;

    for (const auto& cell : state.mesh().cells()) {
        forcing.top_of_atmosphere_insolation_W_m2[cell.id] = top_of_atmosphere_insolation_W_m2(
            cell.center_unit, forcing.orbit.sun_direction_body_unit,
            forcing.incident_solar_flux_W_m2);
    }
}

}  // namespace planetsim
