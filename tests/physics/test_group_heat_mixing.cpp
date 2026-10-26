#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

// The heat mixed within the transport's groups (ADR-0027 V2; INV-04): the
// Earth-like planet in climate mode, L3, three coupled months from the
// initial state. Off, every cell of a group receives the group's source
// (ADR-0009 §12). On (the preset), the cells' sources differ by shifts that
// sum to zero, so the transport still sums to zero and energy and water
// close; the land and ocean air of a mixed group come within a few kelvin
// of each other; and the run is the same for 1 and 4 workers.
namespace {

struct Record {
    std::uint64_t hash = 0;
    double worst_imbalance = 0.0;   // |Σ A H| over Σ A |H|
    double worst_closure = 0.0;
    double worst_water = 0.0;
    double worst_group_spread_W_m2 = 0.0;   // max |H_c − H_c'| within a group
    double within_mixed_groups_K = 0.0;     // bottom air, land cells minus ocean cells
    int coupled_months = 0;
};

Record run(std::size_t workers, double mixing) {
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    planetsim::PlanetRun planet(scenario, workers, planetsim::ClimateCirculationUse::coupled,
                                [=](planetsim::SurfaceEnergyParameters& p) {
                                    p.heat_group_mixing = mixing;
                                    // Without ADR-0028's waves, whose cold
                                    // start falls back in month 0.
                                    p.equatorial_wave_speed_m_s = 0.0;
                                });
    const auto& mesh = planet.state().mesh();
    const auto& fractions = planet.fractions();
    const std::vector<std::size_t>* group_of_cell = nullptr;
    const auto& graph = planetsim::agglomerated_transport_graph(mesh, group_of_cell);
    const std::size_t groups = graph.size();
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
        std::vector<double> low(groups, INFINITY), high(groups, -INFINITY);
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            const std::size_t g = (*group_of_cell)[cell];
            low[g] = std::min(low[g], step.cell_transport_W_m2[cell]);
            high[g] = std::max(high[g], step.cell_transport_W_m2[cell]);
        }
        for (std::size_t g = 0; g < groups; ++g) {
            record.worst_group_spread_W_m2 =
                std::max(record.worst_group_spread_W_m2, high[g] - low[g]);
        }
    }
    // The last month's bottom air: land cells (land fraction above 0.5)
    // against ocean cells in the groups that have both, area-weighted.
    const auto air = planet.state().slow().atmosphere_temperature_K.layer(0);
    std::vector<double> land_area(groups, 0.0), land_air(groups, 0.0);
    std::vector<double> ocean_area(groups, 0.0), ocean_air(groups, 0.0);
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        const std::size_t g = (*group_of_cell)[cell];
        const double a = mesh.cells()[cell].area_m2;
        if (fractions.land_fraction[cell] > 0.5) {
            land_area[g] += a;
            land_air[g] += a * air[cell];
        } else {
            ocean_area[g] += a;
            ocean_air[g] += a * air[cell];
        }
    }
    double weight = 0.0;
    for (std::size_t g = 0; g < groups; ++g) {
        if (land_area[g] > 0.0 && ocean_area[g] > 0.0) {
            weight += land_area[g];
            record.within_mixed_groups_K +=
                land_area[g] * (land_air[g] / land_area[g] - ocean_air[g] / ocean_area[g]);
        }
    }
    record.within_mixed_groups_K /= weight;
    record.hash = planet.state_hash();
    std::cout << "mixing " << mixing << " workers " << workers << ": imbalance "
              << record.worst_imbalance << ", closure " << record.worst_closure << ", water "
              << record.worst_water << ", group spread " << record.worst_group_spread_W_m2
              << " W/m2, air within mixed groups " << record.within_mixed_groups_K
              << " K, coupled months " << record.coupled_months << '\n';
    return record;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const Record off = run(4U, 0.0);
    const Record on = run(4U, 1.0);
    const Record serial = run(1U, 1.0);
    // Off: one source per group, exactly.
    PLANETSIM_EXPECT(test, off.worst_group_spread_W_m2 == 0.0);
    // On: the shifts move heat within groups, and none out of them.
    PLANETSIM_EXPECT(test, on.worst_group_spread_W_m2 > 0.0);
    for (const Record* r : {&off, &on}) {
        PLANETSIM_EXPECT(test, r->coupled_months == 3);
        PLANETSIM_EXPECT(test, r->worst_imbalance <= 1e-9);
        PLANETSIM_EXPECT(test, r->worst_closure <= 1.0);
        PLANETSIM_EXPECT(test, r->worst_water <= 1.0);
    }
    // The land and ocean air of a mixed group: tens of kelvin apart off,
    // within a fifth of that on.
    PLANETSIM_EXPECT(test, std::abs(off.within_mixed_groups_K) > 5.0);
    PLANETSIM_EXPECT(test,
                     std::abs(on.within_mixed_groups_K) < 0.2 * std::abs(off.within_mixed_groups_K));
    // V5: the same for any worker count.
    PLANETSIM_EXPECT(test, on.hash == serial.hash);
    PLANETSIM_EXPECT(test, on.hash != off.hash);
    return test.result();
}
