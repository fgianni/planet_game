#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

// ADR-0024 §9.6: the Earth-like preset's L6 first month from the initial
// state, whose coupled transport diverges and whose diffusive fallback,
// restarted from the step's start, does not converge either. The month
// moves no heat; its columns converge, energy and water close, and the
// month is the same for 4 and 16 workers.
namespace {

struct Record {
    std::uint64_t hash = 0;
    bool fell_back = false;
    bool no_heat = false;
    double largest_transport_W_m2 = 0.0;
    double closure = 0.0;
    double water = 0.0;
    double column_residual_W_m2 = 0.0;
};

Record first_month(std::size_t workers) {
    planetsim::Scenario scenario;
    scenario.subdivision = 6;
    planetsim::PlanetRun planet(scenario, workers, planetsim::ClimateCirculationUse::coupled);
    planet.run_until(
        planetsim::climate_substep_containing(planet.tick(), planet.parameters()).end_tick);
    const auto& step = planet.last_step();
    Record record;
    record.hash = planet.state_hash();
    record.fell_back = step.transport_fell_back;
    record.no_heat = step.transport_fallback_unconverged;
    for (const double h : step.cell_transport_W_m2) {
        record.largest_transport_W_m2 = std::max(record.largest_transport_W_m2, std::abs(h));
    }
    record.closure = step.closure_residual_J() / step.closure_gate_J();
    record.water = step.water_cycle_residual_kg() / step.water_cycle_gate_kg();
    record.column_residual_W_m2 = step.max_column_residual_W_m2;
    std::cout << "workers " << workers << ": fell back " << record.fell_back << ", no heat "
              << record.no_heat << ", closure " << record.closure << ", water " << record.water
              << ", column residual " << record.column_residual_W_m2 << '\n';
    return record;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const Record parallel = first_month(16U);
    const Record fewer = first_month(4U);
    PLANETSIM_EXPECT(test, parallel.fell_back && parallel.no_heat);
    PLANETSIM_EXPECT(test, parallel.largest_transport_W_m2 == 0.0);
    PLANETSIM_EXPECT(test, parallel.closure <= 1.0);
    PLANETSIM_EXPECT(test, parallel.water <= 1.0);
    PLANETSIM_EXPECT(test, parallel.column_residual_W_m2 <= 1e-9);
    PLANETSIM_EXPECT(test, parallel.hash == fewer.hash);
    return test.result();
}
