#include "sim/planet/dynamics/climate_circulation.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

// The equatorial waves' exchange (ADR-0028 V2; INV-04): the diffusivity
// D_w = c L_eq exp(−y²/(2 L_eq²)) with L_eq = √(c/β), and the Earth-like
// planet in climate mode, L3, three months from the initial state, with
// c = 50 m/s (the preset) and c = 0: both close energy and water, they
// differ, and the preset's run is the same for 1 and 4 workers. The cold
// start's hot first month may take ADR-0024 §9's fallback (the preset's
// does, at 246 W/m²); the months after it are coupled.
namespace {

struct Record {
    std::uint64_t hash = 0;
    double worst_imbalance = 0.0;
    double worst_closure = 0.0;
    double worst_water = 0.0;
    int coupled_months = 0;
};

Record run(std::size_t workers, double wave_speed_m_s) {
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    planetsim::PlanetRun planet(scenario, workers, planetsim::ClimateCirculationUse::coupled,
                                [=](planetsim::SurfaceEnergyParameters& p) {
                                    p.equatorial_wave_speed_m_s = wave_speed_m_s;
                                });
    Record record;
    for (int month = 0; month < 3; ++month) {
        planet.run_until(
            planetsim::climate_substep_containing(planet.tick(), planet.parameters()).end_tick);
        const auto& step = planet.last_step();
        record.coupled_months += step.transport_coupled && !step.transport_fell_back ? 1 : 0;
        record.worst_imbalance = std::max(record.worst_imbalance,
                                          std::abs(step.transport_W) / step.transport_absolute_W);
        record.worst_closure =
            std::max(record.worst_closure, step.closure_residual_J() / step.closure_gate_J());
        record.worst_water = std::max(record.worst_water,
                                      step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
    }
    record.hash = planet.state_hash();
    std::cout << "c " << wave_speed_m_s << " workers " << workers << ": imbalance "
              << record.worst_imbalance << ", closure " << record.worst_closure << ", water "
              << record.worst_water << ", coupled months " << record.coupled_months << '\n';
    return record;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    // The diffusivity: Earth's Ω and a, c = 50 m/s.
    constexpr double omega = 7.292e-5;
    constexpr double radius = 6.371e6;
    const double l_eq = std::sqrt(50.0 / (2.0 * omega / radius));
    const double peak = planetsim::equatorial_wave_diffusivity_m2_s(50.0, omega, radius, 0.0);
    PLANETSIM_EXPECT(test, std::abs(peak - 50.0 * l_eq) <= 1e-9 * peak);
    PLANETSIM_EXPECT(test, std::abs(l_eq - 1.478e6) < 1e3);
    // At one deformation radius from the equator, e^(−1/2) of the peak,
    // the same north and south.
    const double at_l_eq_deg = l_eq / radius * 180.0 / 3.14159265358979323846;
    const double north =
        planetsim::equatorial_wave_diffusivity_m2_s(50.0, omega, radius, at_l_eq_deg);
    const double south =
        planetsim::equatorial_wave_diffusivity_m2_s(50.0, omega, radius, -at_l_eq_deg);
    PLANETSIM_EXPECT(test, std::abs(north / peak - std::exp(-0.5)) <= 1e-12);
    PLANETSIM_EXPECT(test, north == south);
    // Off: none.
    PLANETSIM_EXPECT(test, planetsim::equatorial_wave_diffusivity_m2_s(0.0, omega, radius, 0.0) == 0.0);

    const Record off = run(4U, 0.0);
    const Record on = run(4U, 50.0);
    const Record serial = run(1U, 50.0);
    for (const Record* r : {&off, &on}) {
        PLANETSIM_EXPECT(test, r->coupled_months >= 2);
        PLANETSIM_EXPECT(test, r->worst_imbalance <= 1e-9);
        PLANETSIM_EXPECT(test, r->worst_closure <= 1.0);
        PLANETSIM_EXPECT(test, r->worst_water <= 1.0);
    }
    PLANETSIM_EXPECT(test, on.hash != off.hash);
    // V5: the same for any worker count.
    PLANETSIM_EXPECT(test, on.hash == serial.hash);
    return test.result();
}
