#pragma once

#include <cstdint>

namespace planetsim {

using SimulationTick = std::int64_t;
inline constexpr SimulationTick simulation_seconds_per_tick = 60;

[[nodiscard]] constexpr double simulation_time_s(SimulationTick tick) noexcept {
    return static_cast<double>(tick) * static_cast<double>(simulation_seconds_per_tick);
}

class SimulationClock {
  public:
    [[nodiscard]] SimulationTick tick() const noexcept { return tick_; }
    [[nodiscard]] double time_s() const noexcept { return simulation_time_s(tick_); }

    void advance_ticks(SimulationTick ticks);
    void set_tick(SimulationTick tick);
    void reset() noexcept;

  private:
    SimulationTick tick_ = 0;
};

}  // namespace planetsim
