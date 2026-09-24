#pragma once

#include <cstdint>
#include <vector>

namespace planetsim {

class PlanetState;
class SimulationClock;

struct StateSnapshot {
    double simulation_time_s = 0.0;
    std::uint64_t step_count = 0;
    std::vector<float> surface_scalar;
};

[[nodiscard]] StateSnapshot make_state_snapshot(const PlanetState& state,
                                                const SimulationClock& clock);

}  // namespace planetsim
