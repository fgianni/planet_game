#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using planetsim::ProcessRegistration;
using planetsim::Scheduler;
using planetsim::SimulationClock;
using planetsim::SimulationMode;
using planetsim::SimulationTick;
using planetsim::StepContext;

using StepRecord = std::tuple<std::uint64_t, SimulationMode, SimulationTick, SimulationTick>;

[[nodiscard]] planetsim::OrbitalCalendar calendar() {
    return planetsim::make_orbital_calendar(planetsim::PlanetParameters::earth_development());
}

[[nodiscard]] StepRecord record(const StepContext& context) {
    return {context.step_index, context.mode, context.begin_tick, context.end_tick};
}

// Task C6: climate steps are calendar sub-steps; reference steps are ten
// ticks; processes run in registration order, only in their mode, at their
// cadence.
void check_steps_and_dispatch(planetsim::test::Context& test) {
    SimulationClock clock;
    Scheduler scheduler(clock, calendar());
    std::vector<std::string> calls;
    scheduler.register_process({"climate_a", SimulationMode::climate, 0},
                               [&](const StepContext&) { calls.push_back("climate_a"); });
    scheduler.register_process({"reference_every", SimulationMode::reference, 0},
                               [&](const StepContext&) { calls.push_back("reference_every"); });
    scheduler.register_process({"climate_b", SimulationMode::climate, 0},
                               [&](const StepContext&) { calls.push_back("climate_b"); });
    scheduler.register_process({"reference_hourly", SimulationMode::reference, 60},
                               [&](const StepContext&) { calls.push_back("reference_hourly"); });

    bool substeps_match = true;
    for (std::int64_t index = 0; index < 24; ++index) {
        const StepContext context = scheduler.step();
        const auto expected = planetsim::climate_substep(calendar(), index);
        substeps_match = substeps_match && context.mode == SimulationMode::climate &&
                         context.substep.has_value() && context.substep->index == index &&
                         context.begin_tick == expected.begin_tick &&
                         context.end_tick == expected.end_tick;
    }
    PLANETSIM_EXPECT(test, substeps_match);
    PLANETSIM_EXPECT(test, calls.size() == 48U);
    PLANETSIM_EXPECT(test, calls.size() >= 2U && calls[0] == "climate_a" && calls[1] == "climate_b");
    PLANETSIM_EXPECT(test, clock.tick() == planetsim::climate_substep_begin_tick(calendar(), 24));

    // Switch to reference mode at a sub-step boundary that is not a multiple
    // of ten: the first reference step is partial, up to the grid.
    calls.clear();
    scheduler.request_mode(SimulationMode::reference);
    const SimulationTick switch_tick = clock.tick();
    const StepContext first = scheduler.step();
    PLANETSIM_EXPECT(test, first.mode == SimulationMode::reference && !first.substep.has_value());
    PLANETSIM_EXPECT(test, first.begin_tick == switch_tick);
    PLANETSIM_EXPECT(test, first.end_tick % 10 == 0 && first.length_ticks() <= 10);
    bool ten_tick_steps = true;
    std::size_t hourly = 0;
    std::size_t every = 0;
    calls.clear();
    for (int index = 0; index < 60; ++index) {
        const StepContext context = scheduler.step();
        ten_tick_steps = ten_tick_steps && context.length_ticks() == 10 &&
                         context.begin_tick % 10 == 0;
    }
    for (const auto& call : calls) {
        hourly += call == "reference_hourly" ? 1U : 0U;
        every += call == "reference_every" ? 1U : 0U;
    }
    PLANETSIM_EXPECT(test, ten_tick_steps);
    PLANETSIM_EXPECT(test, every == 60U);
    PLANETSIM_EXPECT(test, hourly == 10U);   // 600 ticks, one call per 60
}

// Task C4 (ADR-0006 V7): a run started mid-sub-step shares every later
// boundary with a run started at tick 0; its first step is the remainder.
void check_mid_substep_start(planetsim::test::Context& test) {
    SimulationClock from_zero;
    Scheduler zero(from_zero, calendar());
    std::vector<SimulationTick> zero_ends;
    for (int index = 0; index < 30; ++index) {
        zero_ends.push_back(zero.step().end_tick);
    }

    SimulationClock mid_clock;
    mid_clock.set_tick(12'345);
    Scheduler mid(mid_clock, calendar());
    const StepContext first = mid.step();
    PLANETSIM_EXPECT(test, first.begin_tick == 12'345);
    PLANETSIM_EXPECT(test, first.substep.has_value() && first.substep->index == 0 &&
                               first.substep->begin_tick == 0);
    PLANETSIM_EXPECT(test, first.end_tick == zero_ends[0]);
    bool shared = true;
    for (std::size_t index = 1; index < zero_ends.size(); ++index) {
        shared = shared && mid.step().end_tick == zero_ends[index];
    }
    PLANETSIM_EXPECT(test, shared);
}

// Task C7: requested mode changes take effect at the next step, are logged,
// and a request made by a process during a step applies to the next one.
void check_mode_changes(planetsim::test::Context& test) {
    SimulationClock clock;
    Scheduler scheduler(clock, calendar());
    PLANETSIM_EXPECT(test, scheduler.mode_log().size() == 1U &&
                               scheduler.mode_log()[0].tick == 0 &&
                               scheduler.mode_log()[0].mode == SimulationMode::climate);
    scheduler.step();
    const SimulationTick boundary = clock.tick();
    scheduler.request_mode(SimulationMode::reference);
    PLANETSIM_EXPECT(test, scheduler.mode() == SimulationMode::reference);
    PLANETSIM_EXPECT(test, clock.tick() == boundary);   // no step was split or taken
    scheduler.step();
    PLANETSIM_EXPECT(test, scheduler.mode_log().size() == 2U &&
                               scheduler.mode_log()[1].tick == boundary &&
                               scheduler.mode_log()[1].mode == SimulationMode::reference);

    // A process requests climate mode while a reference step runs.
    bool requested = false;
    scheduler.register_process({"switch_back", SimulationMode::reference, 0},
                               [&](const StepContext&) {
                                   if (!requested) {
                                       scheduler.request_mode(SimulationMode::climate);
                                       requested = true;
                                   }
                               });
    const StepContext during = scheduler.step();
    PLANETSIM_EXPECT(test, during.mode == SimulationMode::reference);
    PLANETSIM_EXPECT(test, scheduler.mode() == SimulationMode::climate);
    const StepContext after = scheduler.step();
    PLANETSIM_EXPECT(test, after.mode == SimulationMode::climate);
    PLANETSIM_EXPECT(test, scheduler.mode_log().size() == 3U &&
                               scheduler.mode_log()[2].tick == during.end_tick);

    PLANETSIM_EXPECT_THROWS(test, std::logic_error,
                            scheduler.request_mode(SimulationMode::weather_window));
    SimulationClock other;
    PLANETSIM_EXPECT_THROWS(test, std::logic_error,
                            Scheduler(other, calendar(), SimulationMode::weather_window));
}

// A failing process leaves the clock, the mode and the step count unchanged,
// and the consumed mode request is restored.
void check_failed_step(planetsim::test::Context& test) {
    SimulationClock clock;
    Scheduler scheduler(clock, calendar());
    scheduler.register_process({"failing", SimulationMode::reference, 0},
                               [](const StepContext&) { throw std::runtime_error("fail"); });
    scheduler.request_mode(SimulationMode::reference);
    PLANETSIM_EXPECT_THROWS(test, std::runtime_error, scheduler.step());
    PLANETSIM_EXPECT(test, clock.tick() == 0);
    PLANETSIM_EXPECT(test, scheduler.step_count() == 0U);
    PLANETSIM_EXPECT(test, scheduler.mode() == SimulationMode::reference);
    PLANETSIM_EXPECT(test, scheduler.mode_log().size() == 1U);
}

// Task C8: pacing cannot change the tick sequence. A process switches mode
// from "state" (the step index) in both runs.
[[nodiscard]] std::vector<StepRecord> paced_run(const std::vector<SimulationTick>& targets,
                                                SimulationTick& final_tick) {
    SimulationClock clock;
    Scheduler scheduler(clock, calendar());
    std::vector<StepRecord> steps;
    const auto observe = [&](const StepContext& context) {
        steps.push_back(record(context));
        if (context.step_index == 5U) {
            scheduler.request_mode(SimulationMode::reference);
        }
        if (context.step_index == 400U) {
            scheduler.request_mode(SimulationMode::climate);
        }
    };
    scheduler.register_process({"observe_climate", SimulationMode::climate, 0}, observe);
    scheduler.register_process({"observe_reference", SimulationMode::reference, 0}, observe);
    for (const SimulationTick target : targets) {
        scheduler.run_until(target);
    }
    final_tick = clock.tick();
    return steps;
}

void check_pacing_independence(planetsim::test::Context& test) {
    const SimulationTick target = 2'000'000;   // about four orbital years, not on a boundary
    SimulationTick single_final = 0;
    SimulationTick chunked_final = 0;
    const auto single = paced_run({target}, single_final);
    const auto chunked = paced_run({1, 43'829, 150'000, 263'000, 263'001, 1'234'567, target},
                                   chunked_final);
    PLANETSIM_EXPECT(test, single.size() > 400U);
    PLANETSIM_EXPECT(test, single == chunked);
    PLANETSIM_EXPECT(test, single_final == chunked_final);
    PLANETSIM_EXPECT(test, single_final <= target);
    const auto modes_seen = [&](SimulationMode mode) {
        for (const auto& step : single) {
            if (std::get<1>(step) == mode) {
                return true;
            }
        }
        return false;
    };
    PLANETSIM_EXPECT(test, modes_seen(SimulationMode::reference) &&
                               modes_seen(SimulationMode::climate));
}

// Task C9 (scheduler part).
void check_validation(planetsim::test::Context& test) {
    SimulationClock clock;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            Scheduler(clock, calendar(), SimulationMode::climate, {0}));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            Scheduler(clock, {0.0, 0.0}, SimulationMode::climate));
    Scheduler scheduler(clock, calendar());
    const auto noop = [](const StepContext&) {};
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            scheduler.register_process({"", SimulationMode::climate, 0}, noop));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            scheduler.register_process({"null", SimulationMode::climate, 0},
                                                       planetsim::ProcessFunction{}));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            scheduler.register_process({"odd", SimulationMode::reference, 15},
                                                       noop));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            scheduler.register_process({"negative", SimulationMode::reference, -10},
                                                       noop));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            scheduler.register_process({"monthly", SimulationMode::climate, 60},
                                                       noop));
    PLANETSIM_EXPECT_THROWS(
        test, std::logic_error,
        scheduler.register_process({"window", SimulationMode::weather_window, 0}, noop));
    PLANETSIM_EXPECT(test, planetsim::simulation_mode_name(SimulationMode::climate) == "climate");
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_steps_and_dispatch(test);
    check_mid_substep_start(test);
    check_mode_changes(test);
    check_failed_step(test);
    check_pacing_independence(test);
    check_validation(test);
    return test.result();
}
