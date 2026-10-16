#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <thread>

// Reference mode with moist convection and without τ_c (ADR-0023 §4.2, V7;
// task M8-02) at L3. The energy and water budgets close every step and
// convection rains. V7 (ADR-0023 §11.3: winds below 100 m/s; after the
// first year, each cell's daily-mean latent heating below 100 mm/day of
// rain) is gated only on the measurement run,
// `test_reference_moist_convection <years>` (all cores, ten years in about
// 45 minutes). The climatological mean it also prints belongs to V9's
// calibration. CTest runs 30 days and reports the V7 quantities.
// PLANETSIM_SPIN_UP_YEARS sets climate years before tick 0;
// PLANETSIM_M7_CONVECTION runs M7's convection and τ_c for comparison;
// PLANETSIM_TRACE prints each day's highest wind.
int main(int argc, char** argv) {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    scenario.initial_mode = planetsim::SimulationMode::reference;
    scenario.water_cycle = std::getenv("PLANETSIM_DRY") == nullptr;
    if (const char* spin = std::getenv("PLANETSIM_SPIN_UP_YEARS")) scenario.spin_up_years = std::stoi(spin);
    scenario.moist_convection = *scenario.water_cycle && std::getenv("PLANETSIM_M7_CONVECTION") == nullptr;
    const double years = argc > 1 ? std::stod(argv[1]) : 30.0 / 365.25;
    const std::size_t workers =
        argc > 1 ? std::max<std::size_t>(1U, std::thread::hardware_concurrency()) : 4U;
    planetsim::PlanetRun run(scenario, workers);
    std::vector<double> daily_latent;
    const std::size_t cells = run.state().mesh().cell_count();
    std::vector<double> day_sum(cells, 0.0);    // J/m² of the current day
    std::vector<double> run_sum(cells, 0.0);    // J/m² of the run
    std::vector<double> settled_sum(cells, 0.0);   // J/m² after the first year
    double settled_s = 0.0;
    double day_seconds = 0.0;
    double max_daily_mean = 0.0;
    double max_settled_daily_mean = 0.0;   // days after the first year
    std::size_t current_day = 0;
    double year_wind = 0.0;
    double year_latent = 0.0;
    double year_precipitation_kg = 0.0;
    double year_area_s = 0.0;

    double worst_closure = 0.0;
    double worst_cycle = 0.0;
    double max_wind = 0.0;
    double max_latent = 0.0;
    double precipitation_kg = 0.0;
    double convective_kg = 0.0;
    double duration_s = 0.0;
    const auto end = static_cast<planetsim::SimulationTick>(years * 365.25 * 1'440.0);
    const auto& mesh = run.state().mesh();
    while (run.tick() < end) {
        const planetsim::SimulationTick before = run.tick();
        try {
            run.run_until(before + 10);
        } catch (const std::exception& error) {
            std::cout << "failed at day " << static_cast<double>(before) / 1'440.0 << ": "
                      << error.what() << '\n';
            PLANETSIM_EXPECT(test, false);
            return test.result();
        }
        const char* trace = std::getenv("PLANETSIM_TRACE");
        if (trace != nullptr && before % (*trace == 'h' ? 60 : 1'440) == 0)
 std::cout << "day " << static_cast<double>(before) / 1'440.0 << " wind " << run.dynamics()->last().max_wind_m_s
                      << " m/s\n";
        if (run.tick() == before) {
            PLANETSIM_EXPECT(test, false);
            return test.result();
        }
        const auto& step = run.last_step();
        worst_closure = std::max(worst_closure, step.closure_residual_J() / step.closure_gate_J());
        worst_cycle =
            std::max(worst_cycle, step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
        max_wind = std::max(max_wind, run.dynamics()->last().max_wind_m_s);
        year_wind = std::max(year_wind, run.dynamics()->last().max_wind_m_s);
        year_precipitation_kg += step.precipitation_kg;
        {
            double area_m2 = 0.0;
            for (const auto& cell : run.state().mesh().cells()) area_m2 += cell.area_m2;
            year_area_s += area_m2 * step.duration_s;
        }
        const auto& forcing = run.state().forcing();
        const auto day = static_cast<std::size_t>((run.tick() - 1) / 1'440);
        if (day != current_day) {
            for (std::size_t i = 0; i < cells; ++i) {
                max_daily_mean = std::max(max_daily_mean, day_sum[i] / day_seconds);
                if (current_day >= 365U)
                    max_settled_daily_mean = std::max(max_settled_daily_mean, day_sum[i] / day_seconds);
                year_latent = std::max(year_latent, day_sum[i] / day_seconds);
                day_sum[i] = 0.0;
            }
            if (day % 365U == 0U && argc > 1) {
                std::cout << "year " << day / 365U << ": max wind " << year_wind
                          << " m/s, max daily latent " << year_latent << " W/m2, precipitation "
                          << year_precipitation_kg / year_area_s * 365.25 * 86'400.0 / 1000.0
                          << " m/yr" << std::endl;
                year_wind = year_latent = year_precipitation_kg = year_area_s = 0.0;
            }
            day_seconds = 0.0;
            current_day = day;
        }
        day_seconds += step.duration_s;
        if (day >= 365U) settled_s += step.duration_s;
        if (daily_latent.size() <= day) daily_latent.resize(day + 1U, 0.0);
        for (const auto& cell : mesh.cells()) {
            const std::size_t i = cell.id.to_index();
            const double latent =
                planetsim::latent_heat_vaporisation_J_kg * forcing.precipitation_kg_m2_s[i];
            max_latent = std::max(max_latent, latent);
            day_sum[i] += latent * step.duration_s;
            run_sum[i] += latent * step.duration_s;
            if (day >= 365U) settled_sum[i] += latent * step.duration_s;
            daily_latent[day] = std::max(daily_latent[day], latent);
            convective_kg += cell.area_m2 * forcing.convective_precipitation_kg_m2_s[i] *
                             step.duration_s;
        }
        precipitation_kg += step.precipitation_kg;
        duration_s += step.duration_s;
    }
    double max_run_mean = 0.0;
    for (std::size_t i = 0; i < cells; ++i) max_run_mean = std::max(max_run_mean, run_sum[i] / duration_s);
    double max_settled_mean = 0.0;
    for (std::size_t i = 0; i < cells && settled_s > 0.0; ++i)
        max_settled_mean = std::max(max_settled_mean, settled_sum[i] / settled_s);
    if (settled_s > 0.0)
        std::cout << "latent heating per cell after the first year: daily-mean max "
                  << max_settled_daily_mean << ", climatological max " << max_settled_mean
                  << " W/m2\n";
    std::cout << "latent heating per cell: step max " << max_latent << ", daily-mean max "
              << max_daily_mean << ", run-mean max " << max_run_mean << " W/m2\n";
    double area = 0.0;
    for (const auto& cell : mesh.cells()) area += cell.area_m2;
    const double to_m_yr = 1.0 / area / 1000.0 / duration_s * 365.25 * 86'400.0;
    std::cout << "reference moist convection, " << years << " years: closure " << worst_closure
              << " cycle " << worst_cycle << " | max wind " << max_wind << " m/s, max latent "
              << max_latent << " W/m2 | precipitation " << precipitation_kg * to_m_yr
              << " m/yr, convective " << convective_kg / precipitation_kg << '\n';
    for (std::size_t day = 0; day < daily_latent.size(); day += std::max<std::size_t>(1U, daily_latent.size() / 30U))
        std::cout << "  day " << day << " max latent " << daily_latent[day] << " W/m2\n";
    PLANETSIM_EXPECT(test, worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, worst_cycle <= 1.0);
    if (argc > 1) {
        PLANETSIM_EXPECT(test, max_wind < 100.0);
        // V7's latent heating (ADR-0023 §11.3): no cell rains more than a
        // tropical-cyclone day, 100 mm/day, as a daily mean after the first
        // year's start-up. τ_c's runaway was 1.3–2.9 kW/m² sustained.
        const double cyclone_day_W_m2 =
            planetsim::latent_heat_vaporisation_J_kg * 100.0 / 86'400.0;
        PLANETSIM_EXPECT(test, max_settled_daily_mean < cyclone_day_W_m2);
    }
    PLANETSIM_EXPECT(test, scenario.moist_convection == false || convective_kg > 0.0);
    return test.result();
}
