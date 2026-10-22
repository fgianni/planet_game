// INV-04 diagnostic experiment, not the scheduler or a V8 acceptance run.
// Start each case from the same ten-year monthly climate. Hold each month's
// insolation and circulation fixed, but divide its surface/column/transport
// solve into equal pieces. This changes integration only in this executable;
// no production cadence or parameter is changed. Report the following year.
#include "sim/planet/dynamics/climate_circulation.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>

int main() {
    using namespace planetsim;
    constexpr std::size_t workers = 1;
    Scenario scenario;
    scenario.subdivision = 3;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 10;
    PlanetRun initial(scenario, workers);
    const auto& planet = initial.parameters();
    const auto& surface = initial.surface_parameters();
    const auto& fractions = initial.fractions();
    const auto& mesh = initial.state().mesh();
    double area = 0;
    for (const auto& cell : mesh.cells()) area += cell.area_m2;

    for (const int pieces : {1, 2, 6, 30}) {
        PlanetState state(initial.state().mesh_handle());
        state.slow() = initial.state().slow();
        state.forcing() = initial.state().forcing();
        state.circulation() = initial.state().circulation();
        ClimateCirculation circulation(mesh, state.slow(), planet, surface, fractions);
        double temperature_K_s = 0, absorbed_J = 0, emitted_J = 0, precipitation_kg = 0;
        double cover_m2_s = 0, high_cover_m2_s = 0, seconds = 0;
        double worst_energy = 0, worst_water = 0;
        std::size_t fallbacks = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int month = 0; month < 12; ++month) {
            const auto substep = climate_substep(month, planet);
            update_substep_mean_insolation(state, planet, substep, workers);
            circulation.step(state, planet, surface,
                             state.forcing().substep_mean_insolation_W_m2,
                             state.circulation(), workers);
            if (circulation.diagnostics().last_solved) {
                circulation.write_balanced_pressure(state, workers);
            }
            const double dt_s = simulation_time_s(substep.length_ticks()) / pieces;
            for (int part = 0; part < pieces; ++part) {
                const auto step = step_surface_energy(
                    state, planet, surface, fractions,
                    state.forcing().substep_mean_insolation_W_m2, dt_s, workers,
                    &circulation.transport());
                const double energy = step.closure_residual_J() / step.closure_gate_J();
                const double water = step.water_cycle_residual_kg() / step.water_cycle_gate_kg();
                if (step.unconverged_columns != 0 || !std::isfinite(energy) ||
                    !std::isfinite(water) || energy > 1 || water > 1) {
                    std::cerr << "failed column or budget: pieces=" << pieces
                              << " month=" << month << " part=" << part
                              << " unconverged=" << step.unconverged_columns
                              << " energy=" << energy << " water=" << water << '\n';
                    return 1;
                }
                worst_energy = std::max(worst_energy, energy);
                worst_water = std::max(worst_water, water);
                if (step.transport_fell_back) ++fallbacks;
                temperature_K_s += step.mean_surface_temperature_K * dt_s;
                absorbed_J += step.absorbed_W * dt_s;
                emitted_J += step.emitted_W * dt_s;
                precipitation_kg += step.precipitation_kg;
                cover_m2_s += step.cloud_cover_m2 * dt_s;
                high_cover_m2_s += step.high_cloud_cover_m2 * dt_s;
                seconds += dt_s;
            }
        }
        std::cout << std::setprecision(9) << "pieces_per_month=" << pieces
                  << " mean_K=" << temperature_K_s / seconds
                  << " absorbed_W_m2=" << absorbed_J / (area * seconds)
                  << " olr_W_m2=" << emitted_J / (area * seconds)
                  << " precipitation_m_yr=" << precipitation_kg / area / 1000 / seconds *
                         365.25 * 86'400
                  << " cover=" << cover_m2_s / (area * seconds)
                  << " high_cover=" << high_cover_m2_s / (area * seconds)
                  << " energy_gate_ratio=" << worst_energy
                  << " water_gate_ratio=" << worst_water
                  << " fallbacks=" << fallbacks
                  << " failed_circulation_months=" << circulation.diagnostics().failed_months
                  << " wall_s=" << std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - start).count()
                  << std::endl;
        if (circulation.diagnostics().failed_months != 0) return 2;
    }
}
