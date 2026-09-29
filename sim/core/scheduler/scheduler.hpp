#pragma once

#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/scheduler/simulation_mode.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace planetsim {

struct SchedulerConfig {
    // Reference-mode step (ADR-0001 §4.2: the ten-minute atmospheric step).
    SimulationTick reference_step_ticks = 10;
};

// One step as the processes see it. Steps never cross a boundary of their
// mode's grid: climate steps end at the next sub-step boundary (ADR-0006
// §4.1) and reference steps at the next multiple of reference_step_ticks, so
// a step that starts off the grid is a partial step up to it.
struct StepContext {
    std::uint64_t step_index = 0;
    SimulationMode mode = SimulationMode::climate;
    SimulationTick begin_tick = 0;
    SimulationTick end_tick = 0;
    // The sub-step being advanced, in climate mode; begin_tick lies inside it
    // and equals substep->begin_tick except for a partial first step.
    std::optional<ClimateSubstep> substep;

    [[nodiscard]] SimulationTick length_ticks() const noexcept { return end_tick - begin_tick; }
};

struct ModeChange {
    SimulationTick tick = 0;
    SimulationMode mode = SimulationMode::climate;
};

using ProcessFunction = std::function<void(const StepContext&)>;

struct ProcessRegistration {
    std::string name;
    SimulationMode mode = SimulationMode::climate;
    // Reference mode only: 0 runs on every step; otherwise a positive
    // multiple of reference_step_ticks, and the process runs on steps whose
    // begin tick is a multiple of it. Climate processes run on every
    // sub-step and must leave this 0.
    SimulationTick cadence_ticks = 0;
};

// Serial, deterministic dispatcher (ADR-0001 §4.4, ADR-0006 §4.4). The step
// sequence is a function of the clock, the calendar, the configuration and
// the requested modes only: never of wall clock, pacing or thread count.
// Processes use the deterministic block executor internally when they need
// parallelism.
class Scheduler {
  public:
    Scheduler(SimulationClock& clock, OrbitalCalendar calendar,
              SimulationMode initial_mode = SimulationMode::climate,
              SchedulerConfig config = {});

    // Processes run in registration order, only in their mode.
    void register_process(ProcessRegistration registration, ProcessFunction function);

    // Takes effect at the start of the next step; never splits a step. The
    // scheduler never changes mode on its own: callers decide, from state
    // (specification §8). Weather windows throw std::logic_error until they
    // exist (ADR-0001 §8, M10-M12).
    void request_mode(SimulationMode mode);

    // The mode the next step will use.
    [[nodiscard]] SimulationMode mode() const noexcept;

    // The step that step() would execute next, without executing it.
    [[nodiscard]] StepContext next_step() const;

    // Runs the registered processes for the next step, then advances the
    // clock to its end. If a process throws, the clock, the mode and the step
    // count are left unchanged.
    StepContext step();

    // Executes whole steps while the next step would end at or before
    // target_tick; never splits a step to land on it. Returns the number of
    // steps executed. Running to A and then to B executes the same steps as
    // running straight to B.
    std::uint64_t run_until(SimulationTick target_tick);

    [[nodiscard]] std::uint64_t step_count() const noexcept { return step_count_; }

    // The initial mode and every effective change, with the tick at which the
    // first step in that mode began; the seed of the ADR-0003 run manifest.
    [[nodiscard]] std::span<const ModeChange> mode_log() const noexcept { return mode_log_; }

  private:
    struct Process {
        ProcessRegistration registration;
        ProcessFunction function;
    };

    SimulationClock& clock_;
    OrbitalCalendar calendar_;
    SchedulerConfig config_;
    SimulationMode active_mode_;
    std::optional<SimulationMode> pending_mode_;
    std::vector<Process> processes_;
    std::vector<ModeChange> mode_log_;
    std::uint64_t step_count_ = 0;
};

}  // namespace planetsim
