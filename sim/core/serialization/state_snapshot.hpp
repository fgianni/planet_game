#pragma once

#include "sim/core/fields/field_registry.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>
#include <vector>

namespace planetsim {

class PlanetState;
inline constexpr std::uint32_t state_snapshot_schema_version = 5;

struct StateSnapshot {
    std::uint32_t schema_version = state_snapshot_schema_version;
    SimulationTick simulation_tick = 0;
    double simulation_time_s = 0.0;
    double rotation_angle_rad = 0.0;
    double orbital_phase_rad = 0.0;
    double eccentric_anomaly_rad = 0.0;
    double true_anomaly_rad = 0.0;
    double solar_longitude_rad = 0.0;
    double solar_declination_rad = 0.0;
    double orbital_distance_m = 0.0;
    double incident_solar_flux_W_m2 = 0.0;
    Vec3d sun_direction_inertial_unit{1.0, 0.0, 0.0};
    Vec3d sun_direction_body_unit{1.0, 0.0, 0.0};
    FieldId top_of_atmosphere_insolation_field_id = FieldId::top_of_atmosphere_insolation_W_m2;
    std::vector<float> top_of_atmosphere_insolation_W_m2;
    // Schema 3 presentation fields. Empty means the source field has not
    // been produced; these are read-only copies, never PSNAP fields.
    std::vector<float> surface_temperature_K;
    std::vector<float> land_snow_water_equivalent_kg_m2;
    std::vector<float> sea_ice_mass_kg_m2;
    // Sample-weighted annual aggregates of the twelve monthly climatology
    // layers. Both vectors are empty until climatology has samples.
    std::vector<float> climatology_surface_temperature_mean_K;
    std::vector<float> climatology_surface_temperature_variance_K2;
    // Schema 4 (ADR-0011 §8): the climate circulation's sea-level pressure
    // and bottom-layer wind. Empty until the circulation has solved a month.
    std::vector<float> sea_level_pressure_Pa;
    std::vector<float> surface_eastward_wind_m_s;
    std::vector<float> surface_northward_wind_m_s;
    // Schema 5: the last completed step's model precipitation. This remains
    // a read-only presentation copy; the registered field owns its meaning.
    std::vector<float> precipitation_kg_m2_s;
};

[[nodiscard]] StateSnapshot make_state_snapshot(const PlanetState& state,
                                                const SimulationClock& clock);

}  // namespace planetsim
