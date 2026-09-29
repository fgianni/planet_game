#include "sim/planet/orbit/climate_calendar.hpp"

namespace planetsim {

OrbitalCalendar make_orbital_calendar(const PlanetParameters& parameters) {
    OrbitalCalendar calendar{parameters.orbital_period_s, parameters.initial_orbital_phase_rad};
    calendar.validate();
    return calendar;
}

ClimateSubstep climate_substep(std::int64_t index, const PlanetParameters& parameters) {
    return climate_substep(make_orbital_calendar(parameters), index);
}

ClimateSubstep climate_substep_containing(SimulationTick tick,
                                          const PlanetParameters& parameters) {
    return climate_substep_containing(make_orbital_calendar(parameters), tick);
}

}  // namespace planetsim
