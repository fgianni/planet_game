#pragma once

#include "sim/core/math/vec3d.hpp"

#include <cstdint>
#include <vector>

namespace planetsim {

class PlanetState;
class SimulationClock;

inline constexpr std::uint32_t state_snapshot_schema_version = 1;

struct StateSnapshot {
    std::uint32_t schema_version = state_snapshot_schema_version;
    double simulation_time_s = 0.0;
    std::uint64_t step_count = 0;
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
    std::vector<float> top_of_atmosphere_insolation_W_m2;
};

[[nodiscard]] StateSnapshot make_state_snapshot(const PlanetState& state,
                                                const SimulationClock& clock);

}  // namespace planetsim
