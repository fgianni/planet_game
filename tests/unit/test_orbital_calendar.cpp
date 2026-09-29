#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {

using planetsim::ClimateSubstep;
using planetsim::OrbitalCalendar;
using planetsim::SimulationTick;

[[nodiscard]] OrbitalCalendar default_calendar() {
    return planetsim::make_orbital_calendar(planetsim::PlanetParameters::earth_development());
}

// ADR-0006 V1 (task C1): increasing boundaries of 43,829 or 43,830 ticks.
void check_lengths(planetsim::test::Context& test, const OrbitalCalendar& calendar) {
    bool increasing = true;
    bool lengths_ok = true;
    SimulationTick previous = planetsim::climate_substep_begin_tick(calendar, -100'000);
    for (std::int64_t index = -99'999; index <= 100'000; ++index) {
        const SimulationTick begin = planetsim::climate_substep_begin_tick(calendar, index);
        const SimulationTick length = begin - previous;
        increasing = increasing && length > 0;
        lengths_ok = lengths_ok && (length == 43'829 || length == 43'830);
        previous = begin;
    }
    PLANETSIM_EXPECT(test, increasing);
    PLANETSIM_EXPECT(test, lengths_ok);
}

// ADR-0006 V2 (task C2): no drift over 10,000 orbital years.
void check_no_drift(planetsim::test::Context& test, const OrbitalCalendar& calendar) {
    const double ticks_per_year =
        calendar.orbital_period_s / static_cast<double>(planetsim::simulation_seconds_per_tick);
    const SimulationTick origin = planetsim::climate_substep_begin_tick(calendar, 0);
    double worst = 0.0;
    for (std::int64_t year = 0; year <= 10'000; ++year) {
        const SimulationTick begin = planetsim::climate_substep_begin_tick(
            calendar, year * planetsim::climate_substeps_per_year);
        worst = std::max(worst, std::abs(static_cast<double>(begin - origin) -
                                         static_cast<double>(year) * ticks_per_year));
    }
    PLANETSIM_EXPECT(test, worst <= 1.0);
}

// ADR-0006 V5 (task C3): the containing sub-step agrees with the indexed one
// at every tick of a range, including every boundary.
void check_containing(planetsim::test::Context& test, const OrbitalCalendar& calendar,
                      std::int64_t first_index, std::int64_t last_index) {
    const SimulationTick first = planetsim::climate_substep_begin_tick(calendar, first_index);
    const SimulationTick last = planetsim::climate_substep_begin_tick(calendar, last_index);
    bool consistent = true;
    std::int64_t expected_index = first_index;
    for (SimulationTick tick = first; tick < last; ++tick) {
        if (tick == planetsim::climate_substep_begin_tick(calendar, expected_index + 1)) {
            ++expected_index;
        }
        const ClimateSubstep containing = planetsim::climate_substep_containing(calendar, tick);
        const ClimateSubstep indexed = planetsim::climate_substep(calendar, expected_index);
        consistent = consistent && containing.index == expected_index &&
                     containing.begin_tick == indexed.begin_tick &&
                     containing.end_tick == indexed.end_tick &&
                     containing.month == indexed.month && containing.begin_tick <= tick &&
                     tick < containing.end_tick;
    }
    PLANETSIM_EXPECT(test, consistent);
    PLANETSIM_EXPECT(test, expected_index == last_index - 1);
}

// Task C5: the default epoch and a shifted initial phase.
void check_epoch_and_phase(planetsim::test::Context& test, const OrbitalCalendar& calendar) {
    PLANETSIM_EXPECT(test, planetsim::climate_substep_begin_tick(calendar, 0) == 0);
    PLANETSIM_EXPECT(test, planetsim::climate_substep(calendar, 0).month == 0);
    PLANETSIM_EXPECT(test, planetsim::climate_substep(calendar, 13).month == 1);
    PLANETSIM_EXPECT(test, planetsim::climate_substep(calendar, -1).month == 11);
    PLANETSIM_EXPECT(test, planetsim::climate_substep(calendar, -13).month == 11);

    // Starting one sub-step further along the orbit shifts every boundary by
    // exactly one sub-step: the calendar is anchored to the orbit, not to tick 0.
    OrbitalCalendar shifted = calendar;
    shifted.initial_orbital_phase_rad = 2.0 * std::numbers::pi_v<double> / 12.0;
    bool shifted_ok = true;
    for (std::int64_t index = -120; index <= 120; ++index) {
        shifted_ok = shifted_ok && planetsim::climate_substep_begin_tick(shifted, index) ==
                                       planetsim::climate_substep_begin_tick(calendar, index - 1);
    }
    PLANETSIM_EXPECT(test, shifted_ok);
    PLANETSIM_EXPECT(test, planetsim::climate_substep_begin_tick(shifted, 1) == 0);
}

// Task C9 (calendar part) and the planet-side overloads.
void check_validation_and_overloads(planetsim::test::Context& test) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double period : {0.0, -1.0, 719.0, nan, infinity}) {
        const OrbitalCalendar invalid{period, 0.0};
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, invalid.validate());
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::climate_substep(invalid, 0));
    }
    const OrbitalCalendar bad_phase{31'556'925.216, nan};
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::climate_substep_containing(bad_phase, 0));
    const OrbitalCalendar shortest{720.0, 0.0};
    PLANETSIM_EXPECT(test, planetsim::climate_substep(shortest, 5).length_ticks() == 1);

    const auto parameters = planetsim::PlanetParameters::earth_development();
    const OrbitalCalendar calendar = default_calendar();
    const ClimateSubstep from_planet = planetsim::climate_substep(7, parameters);
    const ClimateSubstep from_core = planetsim::climate_substep(calendar, 7);
    PLANETSIM_EXPECT(test, from_planet.begin_tick == from_core.begin_tick &&
                               from_planet.end_tick == from_core.end_tick &&
                               from_planet.month == from_core.month);
    const ClimateSubstep containing_planet =
        planetsim::climate_substep_containing(300'000, parameters);
    PLANETSIM_EXPECT(test, containing_planet.index ==
                               planetsim::climate_substep_containing(calendar, 300'000).index);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const OrbitalCalendar calendar = default_calendar();
    check_lengths(test, calendar);
    check_no_drift(test, calendar);
    check_containing(test, calendar, 0, 12);    // year 0
    check_containing(test, calendar, -6, 6);    // the year around tick 0
    check_epoch_and_phase(test, calendar);
    check_validation_and_overloads(test);
    return test.result();
}
