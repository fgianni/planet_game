#include "sim/planet/orbit/solar_forcing.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/orbit/orbit_state.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>
#include <cmath>

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
                          SimulationTick tick, std::size_t worker_count) {
    auto& forcing = state.forcing();
    forcing.orbit = evaluate_orbit(parameters, simulation_time_s(tick));
    forcing.incident_solar_flux_W_m2 = forcing.orbit.incident_solar_flux_W_m2;

    const auto& mesh = state.mesh();
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell_index = block.begin; cell_index < block.end; ++cell_index) {
                const auto& cell = mesh.cells()[cell_index];
                const double value = top_of_atmosphere_insolation_W_m2(
                    cell.center_unit, forcing.orbit.sun_direction_body_unit,
                    forcing.incident_solar_flux_W_m2);
                float stored = static_cast<float>(value);
                if (static_cast<double>(stored) > forcing.incident_solar_flux_W_m2) {
                    stored = std::nextafter(stored, 0.0F);
                }
                forcing.top_of_atmosphere_insolation_W_m2[cell.id] = stored;
            }
        });
}

}  // namespace planetsim
