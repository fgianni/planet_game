#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

// The water cycle in reference mode (ADR-0021 §4.5, V2, V3; task M7-06):
// the primitive-equation core advects the humidity, and every reference step's
// columns evaporate and rain out.
namespace {

struct Record {
    double worst_closure = 0.0;
    double worst_cycle = 0.0;
    double worst_advection = 0.0;   // |Δ vapour| / vapour of the advection
    double worst_clipped = 0.0;
    double evaporation_kg = 0.0;
    double precipitation_kg = 0.0;
    double duration_s = 0.0;
};

[[nodiscard]] double vapour_kg(const planetsim::PlanetRun& run) {
    const auto& state = run.state();
    const auto& slow = state.slow();
    const std::size_t n = slow.atmosphere_layer_count();
    const double g = planetsim::surface_gravity_m_s2(run.parameters());
    double sum = 0.0;
    for (const auto& cell : state.mesh().cells()) {
        const std::size_t i = cell.id.to_index();
        for (std::size_t k = 0; k < n; ++k) {
            sum += cell.area_m2 * slow.atmosphere_specific_humidity_kg_kg.layer(k)[i] *
                   slow.atmosphere_surface_pressure_Pa[i] / (g * static_cast<double>(n));
        }
    }
    return sum;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    scenario.initial_mode = planetsim::SimulationMode::reference;
    scenario.water_cycle = true;
    planetsim::PlanetRun run(scenario, 4U);
    planetsim::PlanetRun serial(scenario, 1U);

    // Ten days of reference steps.
    Record record;
    const planetsim::SimulationTick start = run.tick();
    while (run.tick() < start + 10 * 1'440) {
        const planetsim::SimulationTick before = run.tick();
        run.run_until(before + 10);
        if (run.tick() == before) {
            PLANETSIM_EXPECT(test, false);
            return test.result();
        }
        const auto& step = run.last_step();
        const auto& dynamics = run.dynamics()->last();
        record.worst_closure =
            std::max(record.worst_closure, step.closure_residual_J() / step.closure_gate_J());
        record.worst_cycle =
            std::max(record.worst_cycle, step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
        record.worst_advection = std::max(record.worst_advection,
                                          std::abs(dynamics.vapour_change_kg) / dynamics.vapour_kg);
        record.worst_clipped =
            std::max(record.worst_clipped, dynamics.clipped_kg / dynamics.vapour_kg);
        record.evaporation_kg += step.water_evaporation_kg + step.bucket_evaporation_kg +
                                 step.snow_sublimation_kg + step.ice_sublimation_kg;
        record.precipitation_kg += step.precipitation_kg;
        record.duration_s += step.duration_s;
    }
    serial.run_until(run.tick());

    double area = 0.0;
    for (const auto& cell : run.state().mesh().cells()) {
        area += cell.area_m2;
    }
    const double to_m_yr = 1.0 / area / 1000.0 / record.duration_s * 365.25 * 86'400.0;
    std::cout << "reference water: closure " << record.worst_closure << " cycle "
              << record.worst_cycle << " advection " << record.worst_advection << " clipped "
              << record.worst_clipped << " | evaporation " << record.evaporation_kg * to_m_yr
              << " m/yr, precipitation " << record.precipitation_kg * to_m_yr
              << " m/yr | vapour " << vapour_kg(run) / area << " kg/m2, max wind "
              << run.dynamics()->last().max_wind_m_s << " m/s\n";
    // V3 and V2 every reference step; the core's advection conserves the
    // water (V6) and leaves negative undershoots at rounding.
    PLANETSIM_EXPECT(test, record.worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, record.worst_cycle <= 1.0);
    PLANETSIM_EXPECT(test, record.worst_advection <= 1e-12);
    PLANETSIM_EXPECT(test, record.worst_clipped <= 1e-12);
    PLANETSIM_EXPECT(test, record.precipitation_kg > 0.0 && record.evaporation_kg > 0.0);
    // V11: 1 and 4 workers agree.
    PLANETSIM_EXPECT(test, run.state_hash() == serial.state_hash());
    return test.result();
}
