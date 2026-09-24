#pragma once

#include <cstdint>

namespace planetsim {

class SimulationClock {
public:
    [[nodiscard]] double time_s() const noexcept { return time_s_; }
    [[nodiscard]] std::uint64_t step_count() const noexcept { return step_count_; }

    void advance(double timestep_s);
    void reset() noexcept;

private:
    double time_s_ = 0.0;
    std::uint64_t step_count_ = 0;
};

}  // namespace planetsim
