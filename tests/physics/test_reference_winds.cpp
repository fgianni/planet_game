#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <cmath>

// ADR-0011 §4.3, §4.9 (task M6-03 step B): the winds in reference mode on
// the Earth-like planet, and the fast state released on the return to
// climate mode. (Switching from climate into reference mode is covered by
// test_run_replay.)
int main() {
    planetsim::test::Context test;

    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    scenario.initial_mode = planetsim::SimulationMode::reference;
    planetsim::PlanetRun run(scenario, 2U);
    PLANETSIM_EXPECT(test, run.dynamics() != nullptr);
    PLANETSIM_EXPECT(test, !run.state().has_fast_state());   // opened by the first step

    const auto& mesh = run.state().mesh();
    const auto mass = [&] {
        double sum = 0.0;
        for (const auto& cell : mesh.cells()) {
            sum += cell.area_m2 * run.state().slow().atmosphere_surface_pressure_Pa[cell.id];
        }
        return sum;
    };
    const double initial_mass = mass();

    // Reference mode for two days: the fast state opens, winds spin up
    // from rest, mass is exact and the dynamics' energy change stays small.
    const planetsim::SimulationTick start = run.tick();
    double energy_change = 0.0;
    while (run.tick() < start + 2 * 1'440) {
        const planetsim::SimulationTick before = run.tick();
        run.run_until(before + 10);
        if (run.tick() == before) {
            PLANETSIM_EXPECT(test, false);   // a reference step must advance
            return test.result();
        }
        energy_change += run.dynamics()->last().energy_change_J;
    }
    PLANETSIM_EXPECT(test, run.state().has_fast_state());
    const auto& winds = run.state().fast_state()->atmosphere_edge_normal_wind_m_s;
    PLANETSIM_EXPECT(test, winds.layer_count() == run.state().slow().atmosphere_layer_count());
    PLANETSIM_EXPECT(test, winds.cell_count() == mesh.edge_count());
    const auto& last = run.dynamics()->last();
    PLANETSIM_EXPECT(test, last.max_wind_m_s > 5.0 && last.max_wind_m_s < 150.0);
    PLANETSIM_EXPECT(test, std::isfinite(last.kinetic_energy_J) && last.kinetic_energy_J > 0.0);
    PLANETSIM_EXPECT(test, std::abs(mass() - initial_mass) <= 1.0e-13 * initial_mass);
    const double total = run.dynamics()
                             ->model()
                             .diagnose(run.dynamics()->model_state(run.state()))
                             .energy_J();
    // Starting from rest against the climate state's temperature gradients
    // is a violent geostrophic adjustment, and RK3's energy error during it
    // is large but converges at third order (task M6-03: 7.1e-5, 1.2e-5,
    // 1.6e-6, 2.1e-7 of the total over two days at Δt = 600, 300, 150 and
    // 75 s, L3). M6-04's balanced start removes the shock; the yearly drift
    // is V6's, measured by planet_cli reference.
    PLANETSIM_EXPECT(test, std::abs(energy_change) <= 2.0e-4 * total);
    for (std::size_t k = 0; k < run.state().slow().atmosphere_layer_count(); ++k) {
        for (const double t : run.state().slow().atmosphere_temperature_K.layer(k)) {
            PLANETSIM_EXPECT(test, t > 150.0 && t < 350.0);
        }
    }

    // Back to climate mode: the next climate step releases the fast state.
    run.submit({run.tick(), "test", "set_mode", "climate"});
    run.run_until(run.tick() + 100'000);
    PLANETSIM_EXPECT(test, !run.state().has_fast_state());
    PLANETSIM_EXPECT(test, std::abs(mass() - initial_mass) <= 1.0e-13 * initial_mass);

    return test.result();
}
