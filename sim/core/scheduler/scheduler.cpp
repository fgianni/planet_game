#include "sim/core/scheduler/scheduler.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

void require_implemented(SimulationMode mode) {
    if (mode == SimulationMode::weather_window) {
        throw std::logic_error(
            "weather windows are not implemented; ADR-0001 §8 schedules them for M10-M12");
    }
}

// The first multiple of step strictly after tick (tick >= 0, step > 0).
[[nodiscard]] SimulationTick next_multiple(SimulationTick tick, SimulationTick step) {
    const SimulationTick base = tick - tick % step;
    if (base > std::numeric_limits<SimulationTick>::max() - step) {
        throw std::overflow_error("reference step passes the end of the tick range");
    }
    return base + step;
}

}  // namespace

Scheduler::Scheduler(SimulationClock& clock, OrbitalCalendar calendar,
                     SimulationMode initial_mode, SchedulerConfig config)
    : clock_(clock), calendar_(calendar), config_(config), active_mode_(initial_mode) {
    calendar_.validate();
    if (config_.reference_step_ticks <= 0) {
        throw std::invalid_argument("reference step must be at least one tick");
    }
    require_implemented(initial_mode);
    mode_log_.push_back({clock_.tick(), initial_mode});
}

void Scheduler::register_process(ProcessRegistration registration, ProcessFunction function) {
    if (registration.name.empty()) {
        throw std::invalid_argument("a scheduled process needs a name");
    }
    if (!function) {
        throw std::invalid_argument("scheduled process '" + registration.name +
                                    "' has no function");
    }
    require_implemented(registration.mode);
    if (registration.cadence_ticks < 0) {
        throw std::invalid_argument("process cadence must not be negative");
    }
    if (registration.cadence_ticks != 0) {
        if (registration.mode != SimulationMode::reference) {
            throw std::invalid_argument(
                "only reference-mode processes take a cadence; climate processes run every "
                "sub-step");
        }
        if (registration.cadence_ticks % config_.reference_step_ticks != 0) {
            throw std::invalid_argument(
                "process cadence must be a multiple of the reference step");
        }
    }
    processes_.push_back({std::move(registration), std::move(function)});
}

void Scheduler::request_mode(SimulationMode mode) {
    require_implemented(mode);
    pending_mode_ = mode;
}

SimulationMode Scheduler::mode() const noexcept {
    return pending_mode_.value_or(active_mode_);
}

StepContext Scheduler::next_step() const {
    StepContext context;
    context.step_index = step_count_;
    context.mode = mode();
    context.begin_tick = clock_.tick();
    if (context.mode == SimulationMode::climate) {
        context.substep = climate_substep_containing(calendar_, context.begin_tick);
        context.end_tick = context.substep->end_tick;
    } else {
        context.end_tick = next_multiple(context.begin_tick, config_.reference_step_ticks);
    }
    return context;
}

StepContext Scheduler::step() {
    const StepContext context = next_step();
    // Consume the request this step honours before the processes run, so a
    // request made by a process during the step applies to the next one.
    const std::optional<SimulationMode> consumed = std::exchange(pending_mode_, std::nullopt);
    try {
        for (const Process& process : processes_) {
            if (process.registration.mode != context.mode) {
                continue;
            }
            const SimulationTick cadence = process.registration.cadence_ticks;
            if (cadence != 0 && context.begin_tick % cadence != 0) {
                continue;
            }
            process.function(context);
        }
        clock_.advance_ticks(context.length_ticks());
    } catch (...) {
        if (!pending_mode_) {
            pending_mode_ = consumed;
        }
        throw;
    }

    if (context.mode != active_mode_) {
        active_mode_ = context.mode;
        mode_log_.push_back({context.begin_tick, context.mode});
    }
    ++step_count_;
    return context;
}

std::uint64_t Scheduler::run_until(SimulationTick target_tick) {
    std::uint64_t executed = 0;
    while (next_step().end_tick <= target_tick) {
        step();
        ++executed;
    }
    return executed;
}

}  // namespace planetsim
