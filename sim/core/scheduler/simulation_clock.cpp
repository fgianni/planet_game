#include "sim/core/scheduler/simulation_clock.hpp"

#include <limits>
#include <stdexcept>

namespace planetsim {

void SimulationClock::advance_ticks(SimulationTick ticks) {
    if (ticks <= 0) {
        throw std::invalid_argument("simulation tick increment must be positive");
    }
    if (tick_ > std::numeric_limits<SimulationTick>::max() - ticks) {
        throw std::overflow_error("simulation clock tick overflow");
    }
    tick_ += ticks;
}

void SimulationClock::set_tick(SimulationTick tick) {
    if (tick < 0) {
        throw std::invalid_argument("simulation tick must be non-negative");
    }
    tick_ = tick;
}

void SimulationClock::reset() noexcept { tick_ = 0; }

}  // namespace planetsim
