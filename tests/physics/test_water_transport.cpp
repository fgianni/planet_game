#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>

// The water cycle in coupled climate months (ADR-0021 §4.5, V2, V3, V6, V7
// recorded; task M7-04): the circulation's fluxes move the humidity before
// the columns evaporate and rain out.
namespace {

struct YearRecord {
    bool every_month_transported = true;
    double worst_closure = 0.0;
    double worst_cycle = 0.0;
    double worst_transport_change = 0.0;   // |Δ vapour| / vapour of the transport
    double worst_clipped = 0.0;
    double worst_residual = 0.0;
    int most_iterations = 0;
    double worst_consistency_W_m2 = 0.0;   // the coupled heat transport's
    std::size_t unconverged_columns = 0;
    double evaporation_kg = 0.0;
    double precipitation_kg = 0.0;
    double latent_J = 0.0;
    double duration_s = 0.0;
    std::array<double, 18> band_precipitation_kg{};   // 10° bands from 90° S
    std::array<double, 18> band_area_m2{};
};

[[nodiscard]] YearRecord run_year(planetsim::PlanetRun& run, std::int64_t first_month) {
    YearRecord record;
    const auto& mesh = run.state().mesh();
    for (std::int64_t month = first_month; month < first_month + 12; ++month) {
        run.run_until(planetsim::climate_substep(month, run.parameters()).end_tick);
        const auto& step = run.last_step();
        const auto& moved = step.humidity_transport;
        record.every_month_transported = record.every_month_transported && moved.transported;
        record.worst_closure =
            std::max(record.worst_closure, step.closure_residual_J() / step.closure_gate_J());
        record.worst_cycle =
            std::max(record.worst_cycle, step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
        record.worst_transport_change = std::max(
            record.worst_transport_change, std::abs(moved.vapour_change_kg) / moved.vapour_kg);
        record.worst_clipped = std::max(record.worst_clipped, moved.clipped_kg / moved.vapour_kg);
        record.worst_residual = std::max(record.worst_residual, moved.relative_residual);
        record.most_iterations = std::max(record.most_iterations, moved.iterations);
        record.worst_consistency_W_m2 =
            std::max(record.worst_consistency_W_m2, step.transport_consistency_W_m2);
        record.unconverged_columns += step.unconverged_columns;
        record.evaporation_kg += step.water_evaporation_kg + step.bucket_evaporation_kg +
                                 step.snow_sublimation_kg + step.ice_sublimation_kg;
        record.precipitation_kg += step.precipitation_kg;
        record.latent_J += step.evaporation_latent_J;
        record.duration_s += step.duration_s;
        const auto& precipitation = run.state().forcing().precipitation_kg_m2_s;
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            const double latitude =
                planetsim::latitude_rad(mesh.cells()[cell].center_unit) * 180.0 / std::numbers::pi;
            const auto band = static_cast<std::size_t>(
                std::clamp(std::floor((latitude + 90.0) / 10.0), 0.0, 17.0));
            const double area = mesh.cells()[cell].area_m2;
            record.band_precipitation_kg[band] +=
                area * static_cast<double>(precipitation[cell]) * step.duration_s;
            record.band_area_m2[band] += area * step.duration_s;
        }
    }
    return record;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    scenario.water_cycle = true;
    using planetsim::ClimateCirculationUse;
    // The manifest records the water cycle only when it is on.
    {
        const planetsim::Scenario off;
        PLANETSIM_EXPECT(test, off.entries().size() == off.base_entries().size());
        PLANETSIM_EXPECT(test, !planetsim::Scenario::from_entries(off.entries()).water_cycle);
        PLANETSIM_EXPECT(test, planetsim::Scenario::from_entries(scenario.entries()).water_cycle);
    }

    planetsim::PlanetRun run(scenario, 4U, ClimateCirculationUse::coupled);
    planetsim::PlanetRun serial(scenario, 1U, ClimateCirculationUse::coupled);
    // The first year spins the water up from 60% humidity; the second is
    // recorded.
    static_cast<void>(run_year(run, 0));
    static_cast<void>(run_year(serial, 0));
    const auto year = run_year(run, 12);
    static_cast<void>(run_year(serial, 12));

    double area = 0.0;
    for (const auto& cell : run.state().mesh().cells()) {
        area += cell.area_m2;
    }
    const double seconds = year.duration_s;
    const double precipitation_m_yr =
        year.precipitation_kg / area / 1000.0 / seconds * 365.25 * 86'400.0;
    std::cout << "water transport: closure " << year.worst_closure << " cycle " << year.worst_cycle
              << " transport change " << year.worst_transport_change << " clipped "
              << year.worst_clipped << " residual " << year.worst_residual << " iterations "
              << year.most_iterations << " heat consistency " << year.worst_consistency_W_m2
              << " W/m2, unconverged columns " << year.unconverged_columns << "\n  latent " << year.latent_J / seconds / area
              << " W/m2, precipitation " << precipitation_m_yr << " m/yr, P / E "
              << year.precipitation_kg / year.evaporation_kg << ", mean T "
              << run.last_step().mean_surface_temperature_K << " K\n  zonal precipitation (m/yr):";
    std::array<double, 18> zonal{};
    for (std::size_t band = 0; band < 18U; ++band) {
        zonal[band] = year.band_precipitation_kg[band] / year.band_area_m2[band] / 1000.0 *
                      365.25 * 86'400.0;
        std::cout << ' ' << std::fixed << std::setprecision(2) << zonal[band];
    }
    std::cout << std::defaultfloat << '\n';

    // V3, V2 with the transport, which moves the water and conserves it (V6).
    PLANETSIM_EXPECT(test, year.every_month_transported);
    PLANETSIM_EXPECT(test, year.worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, year.worst_cycle <= 1.0);
    PLANETSIM_EXPECT(test, year.worst_transport_change <= 1e-13);
    PLANETSIM_EXPECT(test, year.worst_clipped <= 1e-13);
    PLANETSIM_EXPECT(test, year.worst_residual <= 1e-10);
    // The water's kinks (rain, dew, the snow's melt-out) leave the coupled
    // heat transport and the columns converging.
    PLANETSIM_EXPECT(test, year.worst_consistency_W_m2 <= planetsim::coupled_newton_tolerance_W_m2);
    PLANETSIM_EXPECT(test, year.unconverged_columns == 0U);
    // Over a spun-up year the atmosphere's water barely changes: P ≈ E.
    PLANETSIM_EXPECT(test, std::abs(year.precipitation_kg / year.evaporation_kg - 1.0) < 0.03);
    // V7 is recorded, not gated, before the refit: the global rate.
    PLANETSIM_EXPECT(test, precipitation_m_yr > 0.3 && precipitation_m_yr < 3.0);
    // V11: 1 and 4 workers agree.
    PLANETSIM_EXPECT(test, run.state_hash() == serial.state_hash());
    return test.result();
}
