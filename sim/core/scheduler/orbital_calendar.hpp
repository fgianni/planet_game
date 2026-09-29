#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>

namespace planetsim {

// The climate-mode sub-step calendar of ADR-0006 §4.1. Sub-step k begins at
// the first tick at or after the orbit reaches mean anomaly 2πk/12 from
// periapsis:
//
//   t_k = P · (k/12 − M0/(2π))      begin_tick(k) = ceil(t_k / 60 s)
//
// Boundaries are a pure function of k and the orbit, so they never drift and
// mean the same orbital interval in every scenario. Plain numbers keep this
// domain-agnostic; sim/planet/orbit/climate_calendar.hpp builds one from
// PlanetParameters.
struct OrbitalCalendar {
    double orbital_period_s = 0.0;           // positive, finite; at least 12 ticks
    double initial_orbital_phase_rad = 0.0;  // mean anomaly at tick 0, finite

    // Throws std::invalid_argument for a period that is non-finite or shorter
    // than one tick per sub-step, or a non-finite phase.
    void validate() const;
};

inline constexpr std::int64_t climate_substeps_per_year = 12;

struct ClimateSubstep {
    std::int64_t index = 0;          // k
    SimulationTick begin_tick = 0;   // inclusive
    SimulationTick end_tick = 0;     // exclusive, = begin_tick(k + 1)
    int month = 0;                   // non-negative k mod 12, in 0..11 (also for k < 0)

    [[nodiscard]] SimulationTick length_ticks() const noexcept { return end_tick - begin_tick; }
};

// First tick of sub-step `index`. Throws std::overflow_error if the tick is
// outside the SimulationTick range.
[[nodiscard]] SimulationTick climate_substep_begin_tick(const OrbitalCalendar& calendar,
                                                        std::int64_t index);

[[nodiscard]] ClimateSubstep climate_substep(const OrbitalCalendar& calendar, std::int64_t index);

// The sub-step with begin_tick <= tick < end_tick; consistent with
// climate_substep at every tick, including boundaries and negative ticks.
[[nodiscard]] ClimateSubstep climate_substep_containing(const OrbitalCalendar& calendar,
                                                        SimulationTick tick);

}  // namespace planetsim
