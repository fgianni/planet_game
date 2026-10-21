#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// ADR-0026 V1, V3, V5: the preset carries water upward, conserves water
// and energy, and remains deterministic through the coupled transport.
// With arguments <level> <years> <workers>, measures one preset run for
// the L4 calibration and L5/L6 performance/transport records.
namespace {

struct Result {
    std::uint64_t hash;
    double middle_vapour_kg_m2;
};

Result measure(planetsim::test::Context& test, std::size_t workers, bool moistening,
               std::uint32_t level = 3U, int years = 1) {
    const auto start = std::chrono::steady_clock::now();
    planetsim::Scenario scenario;
    scenario.subdivision = level;
    planetsim::PlanetRun run(
        scenario, workers, planetsim::ClimateCirculationUse::coupled,
        [=](planetsim::SurfaceEnergyParameters& p) {
            // The on case exercises the preset without overriding it.
            if (!moistening) p.atmosphere.convective_moistening = false;
        });
    PLANETSIM_EXPECT(test, run.surface_parameters().atmosphere.convective_moistening == moistening);
    double energy = 0.0;
    double water = 0.0;
    double consistency = 0.0;
    int capped_months = 0;
    double seconds = 0.0;
    double temperature_K_s = 0.0;
    double cover_m2_s = 0.0;
    double shortwave_J = 0.0;
    double longwave_J = 0.0;
    std::vector<double> latent_J_m2(run.state().mesh().cell_count(), 0.0);
    int fallback_months = 0;   // ADR-0024 §9
    for (int month = 0; month < years * 12; ++month) {
        run.run_until(planetsim::climate_substep_containing(run.tick(), run.parameters()).end_tick);
        const auto& step = run.last_step();
        energy = std::max(energy, step.closure_residual_J() / step.closure_gate_J());
        water = std::max(water, step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
        PLANETSIM_EXPECT(test, step.unconverged_columns == 0U);
        if (step.unconverged_columns != 0U) {
            std::cout << "unconverged_columns month=" << month << " count=" << step.unconverged_columns
                      << " max_correction_K=" << step.max_column_correction_K
                      << " fell_back=" << (step.transport_fell_back ? 1 : 0)
                      << " consistency_W_m2=" << step.transport_consistency_W_m2 << std::endl;
        }
        if (step.transport_fell_back) {
            ++fallback_months;
            std::cout << "transport_fallback month=" << month
                      << " consistency_W_m2=" << step.transport_consistency_W_m2 << std::endl;
        }
        consistency = std::max(consistency, step.transport_consistency_W_m2);
        if (step.transport_newton_iterations >= 30) {
            ++capped_months;
            std::cout << "transport_iteration_cap month=" << month
                      << " consistency_W_m2=" << step.transport_consistency_W_m2 << std::endl;
        }
        if (month >= (years - 1) * 12) {
            seconds += step.duration_s;
            temperature_K_s += step.mean_surface_temperature_K * step.duration_s;
            cover_m2_s += step.cloud_cover_m2 * step.duration_s;
            shortwave_J += step.cloud_shortwave_effect_W * step.duration_s;
            longwave_J += step.cloud_longwave_effect_W * step.duration_s;
            const auto& precipitation = run.state().forcing().precipitation_kg_m2_s;
            for (std::size_t cell = 0; cell < latent_J_m2.size(); ++cell) {
                latent_J_m2[cell] += planetsim::latent_heat_vaporisation_J_kg *
                                     precipitation[cell] * step.duration_s;
            }
        }
        if (years > 1 && (month + 1) % (12 * 50) == 0) {
            std::cout << "year=" << (month + 1) / 12
                      << " surface_K=" << step.mean_surface_temperature_K
                      << " transport_consistency_W_m2=" << step.transport_consistency_W_m2
                      << std::endl;
        }
    }
    const auto& slow = run.state().slow();
    const auto& mesh = run.state().mesh();
    const double gravity = planetsim::surface_gravity_m_s2(run.parameters());
    double mass = 0.0;
    double area = 0.0;
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        const double a = mesh.cells()[cell].area_m2;
        mass += a * slow.atmosphere_specific_humidity_kg_kg.layer(1U)[cell] *
                slow.atmosphere_surface_pressure_Pa[cell] / (3.0 * gravity);
        area += a;
    }
    PLANETSIM_EXPECT(test, std::isfinite(energy) && energy <= 1.0);
    PLANETSIM_EXPECT(test, std::isfinite(water) && water <= 1.0);
    const double wall_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double mean = temperature_K_s / seconds;
    const double cover = cover_m2_s / (area * seconds);
    const double sw = shortwave_J / (area * seconds);
    const double lw = longwave_J / (area * seconds);
    std::cout << "moistening=" << moistening << " workers=" << workers
              << " level=" << level << " years=" << years
              << " middle_vapour_kg_m2=" << mass / area
              << " energy_gate_ratio=" << energy << " water_gate_ratio=" << water
              << " worst_transport_consistency_W_m2=" << consistency
              << " capped_months=" << capped_months << " fallback_months=" << fallback_months
              << " failed_circulation_months=" << run.circulation()->diagnostics().failed_months
              << " mean_K=" << mean << " cover=" << cover
              << " cre_sw_W_m2=" << sw << " cre_lw_W_m2=" << lw
              << " max_climatological_latent_W_m2="
              << *std::max_element(latent_J_m2.begin(), latent_J_m2.end()) / seconds
              << " wall_s=" << wall_s << " years_per_minute=" << 60.0 * years / wall_s << '\n';
    if (level == 4U && years == 150 && moistening) {
        PLANETSIM_EXPECT(test, std::abs(mean - 288.0) <= 0.05);
        PLANETSIM_EXPECT(test, cover >= 0.5 && cover <= 0.7);
        PLANETSIM_EXPECT(test, std::abs(sw + 47.0) <= 0.3 * 47.0);
        PLANETSIM_EXPECT(test, std::abs(lw - 26.0) <= 0.3 * 26.0);
    }
    return {run.state_hash(), mass / area};
}

}  // namespace

int main(int argc, char** argv) {
    planetsim::test::Context test;
    if (argc == 4) {
        const int level = std::stoi(argv[1]);
        const int years = std::stoi(argv[2]);
        const int workers = std::stoi(argv[3]);
        if (level < 3 || level > 6 || years < 1 || years > 10'000 || workers < 1) return 2;
        static_cast<void>(measure(test, static_cast<std::size_t>(workers), true,
                                   static_cast<std::uint32_t>(level), years));
        return test.result();
    }
    if (argc != 1) return 2;
    const auto off = measure(test, 1U, false);
    const auto serial = measure(test, 1U, true);
    const auto parallel = measure(test, 4U, true);
    PLANETSIM_EXPECT(test, serial.hash == parallel.hash);
    PLANETSIM_EXPECT(test, serial.middle_vapour_kg_m2 > off.middle_vapour_kg_m2);
    return test.result();
}
