#include "sim/core/scheduler/orbital_calendar.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace planetsim {
namespace {

constexpr double seconds_per_tick = static_cast<double>(simulation_seconds_per_tick);
constexpr double substeps_per_year = static_cast<double>(climate_substeps_per_year);
constexpr double two_pi = 2.0 * std::numbers::pi_v<double>;

[[nodiscard]] int month_of(std::int64_t index) noexcept {
    const std::int64_t remainder = index % climate_substeps_per_year;
    return static_cast<int>(remainder < 0 ? remainder + climate_substeps_per_year : remainder);
}

}  // namespace

void OrbitalCalendar::validate() const {
    if (!std::isfinite(orbital_period_s) ||
        !(orbital_period_s >= substeps_per_year * seconds_per_tick)) {
        throw std::invalid_argument(
            "orbital period must be finite and at least one tick per climate sub-step");
    }
    if (!std::isfinite(initial_orbital_phase_rad)) {
        throw std::invalid_argument("initial orbital phase must be finite");
    }
}

SimulationTick climate_substep_begin_tick(const OrbitalCalendar& calendar, std::int64_t index) {
    calendar.validate();
    const double crossing_s =
        calendar.orbital_period_s * (static_cast<double>(index) / substeps_per_year -
                                     calendar.initial_orbital_phase_rad / two_pi);
    const double tick = std::ceil(crossing_s / seconds_per_tick);
    // 2^63 is exactly representable; every double strictly inside
    // (-2^63, 2^63) converts to int64 without overflow.
    constexpr double limit = 9'223'372'036'854'775'808.0;
    if (!std::isfinite(tick) || !(tick > -limit) || !(tick < limit)) {
        throw std::overflow_error("climate sub-step boundary is outside the tick range");
    }
    return static_cast<SimulationTick>(tick);
}

ClimateSubstep climate_substep(const OrbitalCalendar& calendar, std::int64_t index) {
    if (index == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("climate sub-step index is at the end of its range");
    }
    return {index, climate_substep_begin_tick(calendar, index),
            climate_substep_begin_tick(calendar, index + 1), month_of(index)};
}

ClimateSubstep climate_substep_containing(const OrbitalCalendar& calendar, SimulationTick tick) {
    calendar.validate();
    // First estimate from the inverse of the boundary formula, then correct
    // against the exact boundaries: a floating-point division alone could put
    // a tick that sits on a boundary into the wrong sub-step.
    const double phase_years = static_cast<double>(tick) * seconds_per_tick /
                                   calendar.orbital_period_s +
                               calendar.initial_orbital_phase_rad / two_pi;
    const double estimate = std::floor(phase_years * substeps_per_year);
    constexpr double index_limit = 4.0e18;
    if (!std::isfinite(estimate) || std::abs(estimate) > index_limit) {
        throw std::overflow_error("climate sub-step index is outside its range");
    }
    std::int64_t index = static_cast<std::int64_t>(estimate);
    while (climate_substep_begin_tick(calendar, index) > tick) {
        --index;
    }
    while (climate_substep_begin_tick(calendar, index + 1) <= tick) {
        ++index;
    }
    return climate_substep(calendar, index);
}

}  // namespace planetsim
