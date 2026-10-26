#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/dynamics/climate_circulation.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// ADR-0026 V1, V3, V5: the preset carries water upward, conserves water
// and energy, and remains deterministic through the coupled transport.
// With arguments <level> <years> <workers>, measures one preset run for
// the L4 calibration and L5/L6 performance/transport records.
// --snapshot-restart-l6 checks ADR-0024 §9 V5 on the refitted L6 preset.
namespace {

// The column-convergence gate (ADR-0027 §10): a column fails above this
// residual, ten times the columns' 1e-10 W/m² tolerance. A column that
// ends just above the tolerance stops at its evaluation's rounding (L6's
// first month on the 2026-10-24 refit: four at 1.01–1.09 times it).
constexpr double column_gate_W_m2 = 1e-9;

struct Result {
    std::uint64_t hash;
    double middle_vapour_kg_m2;
};

Result measure(planetsim::test::Context& test, std::size_t workers, bool moistening,
               std::uint32_t level = 3U, int years = 1,
               double dry_depth = 0.0, double vapour_absorption = 0.0) {
    const auto start = std::chrono::steady_clock::now();
    planetsim::Scenario scenario;
    scenario.subdivision = level;
    planetsim::PlanetRun run(
        scenario, workers, planetsim::ClimateCirculationUse::coupled,
        [=](planetsim::SurfaceEnergyParameters& p) {
            // The on case exercises the preset without overriding it.
            if (!moistening) p.atmosphere.convective_moistening = false;
            // Optional fit probes use this PlanetRun path so calibration
            // includes the same humidity dynamics as the long acceptance run.
            if (dry_depth > 0.0) p.atmosphere.dry_optical_depth = dry_depth;
            if (vapour_absorption > 0.0) {
                p.atmosphere.vapour_absorption_m2_kg = vapour_absorption;
            }
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
    int no_heat_months = 0;    // §9.6: the fallback failed too
    for (int month = 0; month < years * 12; ++month) {
        run.run_until(planetsim::climate_substep_containing(run.tick(), run.parameters()).end_tick);
        const auto& step = run.last_step();
        energy = std::max(energy, step.closure_residual_J() / step.closure_gate_J());
        water = std::max(water, step.water_cycle_residual_kg() / step.water_cycle_gate_kg());
        PLANETSIM_EXPECT(test, step.max_column_residual_W_m2 <= column_gate_W_m2);
        if (step.unconverged_columns != 0U) {
            std::cout << "unconverged_columns month=" << month << " count=" << step.unconverged_columns
                      << " max_residual_W_m2=" << step.max_column_residual_W_m2
                      << " max_correction_K=" << step.max_column_correction_K
                      << " fell_back=" << (step.transport_fell_back ? 1 : 0)
                      << " consistency_W_m2=" << step.transport_consistency_W_m2 << std::endl;
        }
        if (step.transport_fell_back) {
            ++fallback_months;
            no_heat_months += step.transport_fallback_unconverged ? 1 : 0;
            std::cout << "transport_fallback month=" << month
                      << " consistency_W_m2=" << step.transport_consistency_W_m2
                      << " no_heat=" << (step.transport_fallback_unconverged ? 1 : 0) << std::endl;
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
              << " no_heat_months=" << no_heat_months
              << " failed_circulation_months=" << run.circulation()->diagnostics().failed_months
              << " mean_K=" << mean << " cover=" << cover
              << " cre_sw_W_m2=" << sw << " cre_lw_W_m2=" << lw
              << " max_climatological_latent_W_m2="
              << *std::max_element(latent_J_m2.begin(), latent_J_m2.end()) / seconds
              << " wall_s=" << wall_s << " years_per_minute=" << 60.0 * years / wall_s
              << " state_hash=" << run.state_hash() << '\n';
    if (level == 4U && years == 150 && moistening) {
        PLANETSIM_EXPECT(test, std::abs(mean - 288.0) <= 0.05);
        PLANETSIM_EXPECT(test, cover >= 0.5 && cover <= 0.7);
        PLANETSIM_EXPECT(test, std::abs(sw + 47.0) <= 0.3 * 47.0);
        PLANETSIM_EXPECT(test, std::abs(lw - 26.0) <= 0.3 * 26.0);
    }
    return {run.state_hash(), mass / area};
}

void snapshot_restart_v5(planetsim::test::Context& test, std::uint32_t level = 4U) {
    namespace fs = std::filesystem;
    using namespace planetsim;

    Scenario scenario;
    scenario.subdivision = level;
    const auto start = std::chrono::steady_clock::now();
    constexpr double prior_dry_depth = 1.1114445287695311;
    constexpr double prior_vapour_absorption = 0.005557222476679688;
    const auto fit = [=](SurfaceEnergyParameters& parameters) {
        // Keep the established L4 case; L6 exercises the current preset.
        if (level == 4U) {
            parameters.atmosphere.dry_optical_depth = prior_dry_depth;
            parameters.atmosphere.vapour_absorption_m2_kg = prior_vapour_absorption;
            // Its month-2 fallback is without ADR-0027's mixing, which keeps
            // no state across months (its shift starts at zero each month).
            parameters.heat_group_mixing = 0.0;
        }
    };
    PlanetRun uninterrupted(scenario, 1U, ClimateCirculationUse::coupled, fit);
    double energy = 0.0;
    double water = 0.0;
    const auto check_step = [&](const SurfaceEnergyDiagnostics& step) {
        const double e = step.closure_residual_J() / step.closure_gate_J();
        const double w = step.water_cycle_residual_kg() / step.water_cycle_gate_kg();
        PLANETSIM_EXPECT(test, std::isfinite(e) && e <= 1.0);
        PLANETSIM_EXPECT(test, std::isfinite(w) && w <= 1.0);
        PLANETSIM_EXPECT(test, step.max_column_residual_W_m2 <= column_gate_W_m2);
        energy = std::max(energy, e);
        water = std::max(water, w);
    };
    // Restart from evolved state, not merely a serialization of the initial
    // condition. L4 keeps its month-2 case; L6 searches months 1..11 so a
    // different build may take a different nonlinear startup path (ADR-0003).
    const int first_month = level == 4U ? 2 : 1;
    const int search_end = level == 4U ? 3 : 12;
    const auto calendar = make_orbital_calendar(uninterrupted.parameters());
    for (int month = 0; month < first_month; ++month) {
        uninterrupted.run_until(climate_substep(calendar, month + 1).begin_tick);
        check_step(uninterrupted.last_step());
    }
    const fs::path snapshot_path =
        fs::temp_directory_path() /
        ("planet_game_inv03_restart_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".psnap");
    struct RemoveSnapshot {
        fs::path path;
        ~RemoveSnapshot() {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    } remove_snapshot{snapshot_path};
    SimulationTick restart_tick = 0;
    std::uint64_t restart_hash = 0;
    int fallback_month = first_month;
    bool found_fallback = false;
    for (; fallback_month < search_end; ++fallback_month) {
        restart_tick = uninterrupted.tick();
        restart_hash = uninterrupted.state_hash();
        write_snapshot(snapshot_path, uninterrupted.state(), restart_tick);
        uninterrupted.run_until(climate_substep(calendar, fallback_month + 1).begin_tick);
        check_step(uninterrupted.last_step());
        if (uninterrupted.last_step().transport_fell_back) {
            found_fallback = true;
            break;
        }
    }
    PLANETSIM_EXPECT(test, found_fallback);
    if (!found_fallback) return;
    PLANETSIM_EXPECT(test, restart_tick > 0);
    std::cout << "snapshot_restart level=" << level << " fallback_month=" << fallback_month
              << " restart_tick=" << restart_tick << " restart_hash=" << restart_hash
              << std::endl;

    struct Checkpoint {
        SimulationTick tick;
        std::uint64_t hash;
        bool fell_back;
    };
    std::vector<Checkpoint> checkpoints;
    // Include the fallback itself and three subsequent months; compare every
    // boundary to catch a transient divergence even if a later state matches.
    for (int offset = 0; offset < 4; ++offset) {
        if (offset != 0) {
            uninterrupted.run_until(
                climate_substep(calendar, fallback_month + offset + 1).begin_tick);
            check_step(uninterrupted.last_step());
        }
        checkpoints.push_back({uninterrupted.tick(), uninterrupted.state_hash(),
                               uninterrupted.last_step().transport_fell_back});
    }
    PLANETSIM_EXPECT(test, uninterrupted.circulation()->diagnostics().failed_months == 0U);

    // Rebuild the deterministic terrain/runtime, then replace its slow state
    // with the PSNAP payload and resume the climate scheduler at its tick.
    auto parameters = PlanetParameters::earth_development();
    parameters.mesh_subdivision = scenario.subdivision;
    const auto mesh = std::make_shared<const PlanetMesh>(
        make_icosphere(scenario.subdivision, parameters.radius_m));
    PlanetState restored(mesh);
    constexpr std::size_t restart_workers = 4U;
    static_cast<void>(generate_terrain(restored, scenario.seed,
                                       geology_parameters_for(scenario.preset), restart_workers));
    SurfaceEnergyParameters surface = surface_energy_parameters_for(scenario.preset);
    surface.atmosphere.layer_count = scenario.resolved_atmosphere_layers();
    surface.water_cycle = scenario.resolved_water_cycle();
    surface.atmosphere.moist_convection = scenario.resolved_moist_convection();
    fit(surface);
    initialise_climate(*mesh, restored.slow(), parameters, surface, restart_workers);
    const SnapshotManifest manifest = read_snapshot(snapshot_path, restored);
    PLANETSIM_EXPECT(test, manifest.tick == restart_tick);
    PLANETSIM_EXPECT(test, slow_state_hash(restored) == restart_hash);

    const auto fractions = compute_surface_fractions(*mesh, restored.slow().hypsometry_m,
                                                     restored.slow().sea_level_m, restart_workers);
    SimulationClock clock;
    clock.set_tick(manifest.tick);
    ClimateCirculation circulation(*mesh, restored.slow(), parameters, surface, fractions);
    Scheduler scheduler(clock, make_orbital_calendar(parameters), SimulationMode::climate);
    register_climate_circulation(scheduler, restored, parameters, surface, circulation,
                                 restart_workers, true);
    SurfaceEnergyDiagnostics last;
    register_surface_energy(scheduler, restored, parameters, surface, fractions, restart_workers,
                            &last, false, &circulation.transport());

    for (const auto& checkpoint : checkpoints) {
        scheduler.run_until(checkpoint.tick);
        check_step(last);
        const auto restored_hash = slow_state_hash(restored);
        PLANETSIM_EXPECT(test, clock.tick() == checkpoint.tick);
        PLANETSIM_EXPECT(test, last.transport_fell_back == checkpoint.fell_back);
        PLANETSIM_EXPECT(test, restored_hash == checkpoint.hash);
        std::cout << "snapshot_restart_checkpoint level=" << level
                  << " tick=" << checkpoint.tick
                  << " fallback=" << last.transport_fell_back
                  << " uninterrupted_hash=" << checkpoint.hash
                  << " restored_hash=" << restored_hash << std::endl;
    }
    PLANETSIM_EXPECT(test, circulation.diagnostics().failed_months == 0U);
    std::cout << "snapshot_restart level=" << level
              << " uninterrupted_workers=1 restart_workers=" << restart_workers
              << " compared_months=" << checkpoints.size()
              << " energy_gate_ratio=" << energy << " water_gate_ratio=" << water
              << " wall_s="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    planetsim::test::Context test;
    if (argc == 2 && std::string_view(argv[1]) == "--snapshot-restart-l6") {
        snapshot_restart_v5(test, 6U);
        return test.result();
    }
    if (argc == 4 || argc == 6) {
        const int level = std::stoi(argv[1]);
        const int years = std::stoi(argv[2]);
        const int workers = std::stoi(argv[3]);
        if (level < 3 || level > 6 || years < 1 || years > 10'000 || workers < 1) return 2;
        const double dry_depth = argc == 6 ? std::stod(argv[4]) : 0.0;
        const double vapour_absorption = argc == 6 ? std::stod(argv[5]) : 0.0;
        if ((argc == 6 && (!(dry_depth > 0.0) || !(vapour_absorption > 0.0))) ||
            (argc == 6 && !std::isfinite(dry_depth + vapour_absorption))) {
            return 2;
        }
        static_cast<void>(measure(test, static_cast<std::size_t>(workers), true,
                                   static_cast<std::uint32_t>(level), years, dry_depth,
                                   vapour_absorption));
        return test.result();
    }
    if (argc != 1) return 2;
    const auto off = measure(test, 1U, false);
    const auto serial = measure(test, 1U, true);
    const auto parallel = measure(test, 4U, true);
    PLANETSIM_EXPECT(test, serial.hash == parallel.hash);
    PLANETSIM_EXPECT(test, serial.middle_vapour_kg_m2 > off.middle_vapour_kg_m2);
    snapshot_restart_v5(test);
    return test.result();
}
