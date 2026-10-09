#include "sim/core/serialization/state_snapshot.hpp"

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_state.hpp"

#include <cstddef>

namespace planetsim {

StateSnapshot make_state_snapshot(const PlanetState& state, const SimulationClock& clock) {
    StateSnapshot snapshot;
    snapshot.simulation_tick = clock.tick();
    snapshot.simulation_time_s = clock.time_s();
    const auto& forcing = state.forcing();
    snapshot.rotation_angle_rad = forcing.orbit.rotation_angle_rad;
    snapshot.orbital_phase_rad = forcing.orbit.orbital_phase_rad;
    snapshot.eccentric_anomaly_rad = forcing.orbit.eccentric_anomaly_rad;
    snapshot.true_anomaly_rad = forcing.orbit.true_anomaly_rad;
    snapshot.solar_longitude_rad = forcing.orbit.solar_longitude_rad;
    snapshot.solar_declination_rad = forcing.orbit.solar_declination_rad;
    snapshot.orbital_distance_m = forcing.orbit.orbital_distance_m;
    snapshot.incident_solar_flux_W_m2 = forcing.incident_solar_flux_W_m2;
    snapshot.sun_direction_inertial_unit = forcing.orbit.sun_direction_inertial_unit;
    snapshot.sun_direction_body_unit = forcing.orbit.sun_direction_body_unit;
    snapshot.top_of_atmosphere_insolation_W_m2.reserve(
        forcing.top_of_atmosphere_insolation_W_m2.size());
    for (const float value : forcing.top_of_atmosphere_insolation_W_m2.values()) {
        snapshot.top_of_atmosphere_insolation_W_m2.push_back(value);
    }
    const std::size_t cells = state.mesh().cell_count();
    const auto copy_float = [cells](const auto& source, std::vector<float>& destination) {
        if (source.size() != cells) {
            return;
        }
        destination.reserve(cells);
        for (const auto value : source.values()) {
            destination.push_back(static_cast<float>(value));
        }
    };
    copy_float(forcing.surface_temperature_K, snapshot.surface_temperature_K);
    copy_float(state.slow().land_snow_water_equivalent_kg_m2,
               snapshot.land_snow_water_equivalent_kg_m2);
    copy_float(state.slow().sea_ice_mass_kg_m2, snapshot.sea_ice_mass_kg_m2);
    copy_float(forcing.precipitation_kg_m2_s, snapshot.precipitation_kg_m2_s);

    if (const auto& circulation = state.circulation();
        circulation.available() && circulation.eastward_wind_m_s.cell_count() == cells) {
        copy_float(circulation.sea_level_pressure_Pa, snapshot.sea_level_pressure_Pa);
        const auto east = circulation.eastward_wind_m_s.layer(0);
        const auto north = circulation.northward_wind_m_s.layer(0);
        snapshot.surface_eastward_wind_m_s.assign(east.begin(), east.end());
        snapshot.surface_northward_wind_m_s.assign(north.begin(), north.end());
    }

    const auto& climatology = state.climatology();
    std::uint64_t samples = 0U;
    for (const std::uint32_t count : climatology.samples) {
        samples += count;
    }
    if (samples != 0U && climatology.surface_temperature_mean_K.layer_count() == 12U &&
        climatology.surface_temperature_variance_K2.layer_count() == 12U) {
        snapshot.climatology_surface_temperature_mean_K.resize(cells);
        snapshot.climatology_surface_temperature_variance_K2.resize(cells);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            double mean = 0.0;
            for (std::size_t month = 0; month < climatology.samples.size(); ++month) {
                mean += static_cast<double>(climatology.samples[month]) *
                        climatology.surface_temperature_mean_K.layer(month)[cell];
            }
            mean /= static_cast<double>(samples);
            double variance = 0.0;
            for (std::size_t month = 0; month < climatology.samples.size(); ++month) {
                const double count = static_cast<double>(climatology.samples[month]);
                const double monthly_mean = climatology.surface_temperature_mean_K.layer(month)[cell];
                const double delta = monthly_mean - mean;
                variance += count * (climatology.surface_temperature_variance_K2.layer(month)[cell] +
                                     delta * delta);
            }
            snapshot.climatology_surface_temperature_mean_K[cell] = static_cast<float>(mean);
            snapshot.climatology_surface_temperature_variance_K2[cell] = static_cast<float>(
                variance / static_cast<double>(samples));
        }
    }
    return snapshot;
}

}  // namespace planetsim
