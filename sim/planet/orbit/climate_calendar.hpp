#pragma once

#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/planet/planet_parameters.hpp"

#include <cstdint>

namespace planetsim {

// The ADR-0006 §4.4 calendar for a planet: the orbital period and the mean
// anomaly at tick 0 come from its parameters.
[[nodiscard]] OrbitalCalendar make_orbital_calendar(const PlanetParameters& parameters);

[[nodiscard]] ClimateSubstep climate_substep(std::int64_t index,
                                             const PlanetParameters& parameters);

[[nodiscard]] ClimateSubstep climate_substep_containing(SimulationTick tick,
                                                        const PlanetParameters& parameters);

}  // namespace planetsim
