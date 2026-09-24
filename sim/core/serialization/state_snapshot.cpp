#include "sim/core/serialization/state_snapshot.hpp"

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_state.hpp"

namespace planetsim {

StateSnapshot make_state_snapshot(const PlanetState& state, const SimulationClock& clock) {
    StateSnapshot snapshot;
    snapshot.simulation_time_s = clock.time_s();
    snapshot.step_count = clock.step_count();
    snapshot.surface_scalar.reserve(state.surface().debug_scalar.size());
    for (const double value : state.surface().debug_scalar.values()) {
        snapshot.surface_scalar.push_back(static_cast<float>(value));
    }
    return snapshot;
}

}  // namespace planetsim
