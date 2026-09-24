#include "sim/core/scheduler/simulation_clock.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace planetsim {

void SimulationClock::advance(double timestep_s) {
    if (!(timestep_s > 0.0) || !std::isfinite(timestep_s)) {
        throw std::invalid_argument("simulation timestep must be finite and positive");
    }
    if (step_count_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("simulation step counter overflow");
    }
    const double next_time_s = time_s_ + timestep_s;
    if (!std::isfinite(next_time_s)) {
        throw std::overflow_error("simulation time overflow");
    }
    time_s_ = next_time_s;
    ++step_count_;
}

void SimulationClock::reset() noexcept {
    time_s_ = 0.0;
    step_count_ = 0;
}

}  // namespace planetsim
