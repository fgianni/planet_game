// ADR-0027: the first months of an L6 cold start in climate mode (seed 1,
// three layers, 16 workers), each month's unconverged columns, their
// largest correction, and whether the transport fell back. Arguments:
// <heat_group_mixing> [<τ_d> <κ_v>] (the preset's when omitted);
// PROBE_LEVEL and PROBE_MONTHS (default 2) change the level and months.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include <cstdlib>
#include <iostream>
using namespace planetsim;
int main(int argc, char** argv) {
  const double mixing = argc > 1 ? std::atof(argv[1]) : 1.0;
  const double dry = argc > 3 ? std::atof(argv[2]) : 0.0;
  const double vapour = argc > 3 ? std::atof(argv[3]) : 0.0;
  Scenario sc; sc.subdivision = 6;
  if (const char* v = std::getenv("PROBE_LEVEL")) sc.subdivision = static_cast<std::uint32_t>(std::atoi(v));
  int months = 2;
  if (const char* v = std::getenv("PROBE_MONTHS")) months = std::atoi(v);
  PlanetRun run(sc, 16, ClimateCirculationUse::coupled, [&](SurfaceEnergyParameters& s) {
    s.heat_group_mixing = mixing;
    if (dry > 0.0) s.atmosphere.dry_optical_depth = dry;
    if (vapour > 0.0) s.atmosphere.vapour_absorption_m2_kg = vapour;
  });
  for (int m = 0; m < months; ++m) {
    run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
    const auto& d = run.last_step();
    std::cout << "mixing " << mixing << " month " << m << " unconverged " << d.unconverged_columns
              << " max_correction_K " << d.max_column_correction_K << " max_iterations "
              << d.max_column_iterations << " fell_back " << d.transport_fell_back
              << " consistency_W_m2 " << d.transport_consistency_W_m2 << " newton "
              << d.transport_newton_iterations << "\n";
  }
}
