#include "sim/core/serialization/state_snapshot.hpp"

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_state.hpp"

namespace planetsim {

StateSnapshot make_state_snapshot(const PlanetState& state, const SimulationClock& clock) {
    StateSnapshot snapshot;
    snapshot.simulation_time_s = clock.time_s();
    snapshot.step_count = clock.step_count();
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
    for (const double value : forcing.top_of_atmosphere_insolation_W_m2.values()) {
        snapshot.top_of_atmosphere_insolation_W_m2.push_back(static_cast<float>(value));
    }
    return snapshot;
}

}  // namespace planetsim
