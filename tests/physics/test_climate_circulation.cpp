#include "sim/planet/dynamics/climate_circulation.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

// The climate mode's circulation as a scheduler process (ADR-0011 §4.4;
// task M6-04 step D).
int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 3;
    const planetsim::SimulationTick year_end = [&] {
        const planetsim::PlanetRun probe(scenario, 1U, false);
        return planetsim::orbital_year_begin_tick(1, probe.parameters());
    }();

    planetsim::PlanetRun run(scenario, 4U);
    planetsim::PlanetRun serial(scenario, 1U);
    planetsim::PlanetRun without(scenario, 4U, false);
    planetsim::StateSnapshot last_frame;
    run.run_until(year_end, [&last_frame](const planetsim::StateSnapshot& frame) { last_frame = frame; });
    serial.run_until(year_end);
    without.run_until(year_end);

    // 1. Every month of the year is solved.
    const auto* circulation = run.circulation();
    PLANETSIM_EXPECT(test, circulation != nullptr);
    PLANETSIM_EXPECT(test, without.circulation() == nullptr);
    if (circulation == nullptr) {
        return test.result();
    }
    const auto& diagnostics = circulation->diagnostics();
    PLANETSIM_EXPECT(test, diagnostics.months == 12U);
    PLANETSIM_EXPECT(test, diagnostics.failed_months == 0U);
    PLANETSIM_EXPECT(test, diagnostics.last_solved);
    PLANETSIM_EXPECT(test, circulation->zonal().has_value());
    PLANETSIM_EXPECT(test, circulation->balance().has_value());
    if (!circulation->zonal() || !circulation->balance()) {
        return test.result();
    }

    // 2. Derived only: the slow state does not see it (until M6-05).
    PLANETSIM_EXPECT(test, run.state_hash() == without.state_hash());
    PLANETSIM_EXPECT(test, run.manifest().checkpoints.size() == without.manifest().checkpoints.size());
    for (std::size_t i = 0; i < run.manifest().checkpoints.size(); ++i) {
        PLANETSIM_EXPECT(test, run.manifest().checkpoints[i].state_hash ==
                                   without.manifest().checkpoints[i].state_hash);
    }

    // 3. Bit-identical for 1 and 4 workers.
    const auto& zonal = *circulation->zonal();
    const auto& balance = *circulation->balance();
    PLANETSIM_EXPECT(test, serial.circulation()->zonal()->eastward_m_s == zonal.eastward_m_s);
    PLANETSIM_EXPECT(test, serial.circulation()->balance()->surface_pressure_Pa ==
                               balance.surface_pressure_Pa);
    PLANETSIM_EXPECT(test, serial.circulation()->balance()->mass_flux_kg_m_s ==
                               balance.mass_flux_kg_m_s);

    // 4. A function of the slow state alone: a new circulation stepped on the
    // run's state gives the run's last month exactly (what a run continued
    // from a snapshot computes).
    {
        planetsim::ClimateCirculation fresh(run.state().mesh(), run.state().slow(),
                                            run.parameters(), run.surface_parameters(),
                                            run.fractions());
        planetsim::CirculationState outputs;
        fresh.step(run.state(), run.parameters(), run.surface_parameters(),
                   run.state().forcing().substep_mean_insolation_W_m2, outputs, 4U);
        PLANETSIM_EXPECT(test, fresh.diagnostics().last_solved);
        if (fresh.zonal() && fresh.balance()) {
            PLANETSIM_EXPECT(test, fresh.zonal()->eastward_m_s == zonal.eastward_m_s);
            PLANETSIM_EXPECT(test,
                             fresh.balance()->surface_pressure_Pa == balance.surface_pressure_Pa);
        }
    }

    // 5. The month's circulation is physical: V9's torque balance, mass
    // closed by the balance, westerlies aloft somewhere in mid-latitudes.
    PLANETSIM_EXPECT(test, std::abs(diagnostics.total_torque_N_m) <=
                               0.05 * diagnostics.gross_torque_N_m);
    // BiCGSTAB stops at 1e-8 here (ClimateCirculationParameters).
    PLANETSIM_EXPECT(test, diagnostics.relative_column_divergence < 1.0e-6);
    double max_top_wind = 0.0;
    const std::size_t top = zonal.layers - 1U;
    for (std::size_t j = 0; j < zonal.bands; ++j) {
        if (std::abs(zonal.latitude_deg[j]) > 25.0 && std::abs(zonal.latitude_deg[j]) < 65.0) {
            max_top_wind = std::max(max_top_wind, zonal.at_band(zonal.eastward_m_s, top, j));
        }
    }
    PLANETSIM_EXPECT(test, max_top_wind > 10.0);

    // 6. The cells' circulation (§4.4 step 5).
    {
        const auto& mesh = run.state().mesh();
        const auto& slow = run.state().slow();
        const auto& out = run.state().circulation();
        const auto& coarse = circulation->balance_model().coarse_mesh();
        const auto& group_of = circulation->balance_model().group_of_cell();
        const std::size_t cells = mesh.cell_count();
        const std::size_t layers = slow.atmosphere_layer_count();
        PLANETSIM_EXPECT(test, out.available());
        PLANETSIM_EXPECT(test, out.eastward_wind_m_s.layer_count() == layers);
        PLANETSIM_EXPECT(test, out.eastward_wind_m_s.cell_count() == cells);
        PLANETSIM_EXPECT(test, without.state().circulation().available() == false);

        // The atmosphere's mass is held.
        std::vector<double> mass(coarse.cell_count(), 0.0);
        std::vector<double> area(coarse.cell_count(), 0.0);
        double balanced_total = 0.0;
        double slow_total = 0.0;
        for (std::size_t i = 0; i < cells; ++i) {
            const double a = mesh.cells()[i].area_m2;
            mass[group_of[i]] += a * out.balanced_surface_pressure_Pa[i];
            area[group_of[i]] += a;
            balanced_total += a * out.balanced_surface_pressure_Pa[i];
            slow_total += a * slow.atmosphere_surface_pressure_Pa[i];
        }
        double worst = 0.0;
        for (std::size_t c = 0; c < coarse.cell_count(); ++c) {
            worst = std::max(worst, std::abs(mass[c] / (area[c] * balance.surface_pressure_Pa[c]) -
                                             1.0));
        }
        // The groups' masses follow the balance only loosely: it sees the
        // smoothed heights (see ClimateCirculation).
        std::cerr << "cells: worst group mass departure from the balance=" << worst << '\n';
        PLANETSIM_EXPECT_NEAR(test, balanced_total / slow_total, 1.0, 1.0e-12);

        // Bit-identical for 1 and 4 workers.
        const auto& serial_out = serial.state().circulation();
        PLANETSIM_EXPECT(test, std::equal(out.eastward_wind_m_s.layer(layers - 1U).begin(),
                                          out.eastward_wind_m_s.layer(layers - 1U).end(),
                                          serial_out.eastward_wind_m_s.layer(layers - 1U).begin()));
        bool same = true;
        for (std::size_t i = 0; i < cells; ++i) {
            same = same && out.sea_level_pressure_Pa[i] == serial_out.sea_level_pressure_Pa[i] &&
                   out.surface_stress_east_N_m2[i] == serial_out.surface_stress_east_N_m2[i] &&
                   out.balanced_surface_pressure_Pa[i] == serial_out.balanced_surface_pressure_Pa[i];
        }
        PLANETSIM_EXPECT(test, same);

        // The stress is along the bottom-layer wind; the sea-level pressure
        // is p_s where the surface is at sea level; values are physical.
        double max_cross = 0.0;
        double min_along = 0.0;
        double max_wind = 0.0;
        double sea_level_mismatch = 0.0;
        double low_slp = 1.0e9;
        double high_slp = 0.0;
        for (std::size_t i = 0; i < cells; ++i) {
            const double u = out.eastward_wind_m_s.layer(0)[i];
            const double v = out.northward_wind_m_s.layer(0)[i];
            const double tx = out.surface_stress_east_N_m2[i];
            const double ty = out.surface_stress_north_N_m2[i];
            max_cross = std::max(max_cross, std::abs(tx * v - ty * u) /
                                                std::max(1.0e-12, std::hypot(tx, ty) * std::hypot(u, v)));
            min_along = std::min(min_along, tx * u + ty * v);
            for (std::size_t k = 0; k < layers; ++k) {
                max_wind = std::max(
                    max_wind, std::hypot(static_cast<double>(out.eastward_wind_m_s.layer(k)[i]),
                                         static_cast<double>(out.northward_wind_m_s.layer(k)[i])));
            }
            if (run.fractions().land_fraction[i] == 0.0F) {
                sea_level_mismatch =
                    std::max(sea_level_mismatch, std::abs(out.sea_level_pressure_Pa[i] /
                                                              out.balanced_surface_pressure_Pa[i] -
                                                          1.0));
            }
            low_slp = std::min(low_slp, static_cast<double>(out.sea_level_pressure_Pa[i]));
            high_slp = std::max(high_slp, static_cast<double>(out.sea_level_pressure_Pa[i]));
        }
        std::cerr << "cells: max_wind=" << max_wind << " slp_hPa=" << low_slp / 100.0 << ".."
                  << high_slp / 100.0 << " stress_cross=" << max_cross
                  << " sea_level_mismatch=" << sea_level_mismatch << '\n';
        PLANETSIM_EXPECT(test, max_cross < 1.0e-6);   // float rounding, relative
        PLANETSIM_EXPECT(test, min_along >= 0.0);
        PLANETSIM_EXPECT(test, max_wind < 150.0);
        PLANETSIM_EXPECT(test, sea_level_mismatch < 1.0e-6);
        PLANETSIM_EXPECT(test, low_slp > 900.0e2 && high_slp < 1100.0e2);

        // The cells' top-layer wind, averaged by band, follows the zonal
        // solution's ū.
        std::vector<double> band_sum(zonal.bands, 0.0);
        std::vector<double> band_area(zonal.bands, 0.0);
        const auto band = planetsim::zonal_band_of_cells(mesh, zonal.bands);
        for (std::size_t i = 0; i < cells; ++i) {
            band_sum[band[i]] += mesh.cells()[i].area_m2 * out.eastward_wind_m_s.layer(top)[i];
            band_area[band[i]] += mesh.cells()[i].area_m2;
        }
        double worst_band = 0.0;
        for (std::size_t j = 0; j < zonal.bands; ++j) {
            if (band_area[j] > 0.0 && std::abs(zonal.latitude_deg[j]) < 75.0) {
                worst_band = std::max(worst_band, std::abs(band_sum[j] / band_area[j] -
                                                           zonal.at_band(zonal.eastward_m_s, top, j)));
            }
        }
        std::cerr << "cells: worst band-mean departure of the top wind=" << worst_band << '\n';
        PLANETSIM_EXPECT(test, worst_band < 5.0);
    }

    // 7. The circulation's climatology: one sample per month after a year,
    // so the last month's mean is that month's field.
    {
        const auto& climatology = run.state().climatology();
        const auto& out = run.state().circulation();
        bool counted = true;
        for (const auto count : climatology.circulation_samples) {
            counted = counted && count == 1U;
        }
        PLANETSIM_EXPECT(test, counted);
        const std::size_t last = 11U;
        const auto pressure = climatology.sea_level_pressure_mean_Pa.layer(last);
        const auto east = climatology.surface_eastward_wind_mean_m_s.layer(last);
        bool same = true;
        for (std::size_t i = 0; i < run.state().mesh().cell_count(); ++i) {
            same = same && pressure[i] == out.sea_level_pressure_Pa[i] &&
                   east[i] == out.eastward_wind_m_s.layer(0)[i];
        }
        PLANETSIM_EXPECT(test, same);
        bool absent = true;
        for (const auto count : without.state().climatology().circulation_samples) {
            absent = absent && count == 0U;
        }
        PLANETSIM_EXPECT(test, absent);
    }

    // 8. The presentation snapshot carries it (schema 4).
    {
        const auto& out = run.state().circulation();
        PLANETSIM_EXPECT(test, last_frame.schema_version == 4U);
        PLANETSIM_EXPECT(test, last_frame.sea_level_pressure_Pa.size() == run.state().mesh().cell_count());
        bool same = last_frame.sea_level_pressure_Pa.size() == run.state().mesh().cell_count();
        for (std::size_t i = 0; same && i < last_frame.sea_level_pressure_Pa.size(); ++i) {
            same = last_frame.sea_level_pressure_Pa[i] == out.sea_level_pressure_Pa[i] &&
                   last_frame.surface_eastward_wind_m_s[i] == out.eastward_wind_m_s.layer(0)[i] &&
                   last_frame.surface_northward_wind_m_s[i] == out.northward_wind_m_s.layer(0)[i];
        }
        PLANETSIM_EXPECT(test, same);
    }

    // 9. No atmosphere, or a mesh too coarse for the bands: no circulation.
    {
        planetsim::Scenario rock;
        rock.preset = planetsim::PlanetPreset::dead_rock;
        rock.subdivision = 2;
        const planetsim::PlanetRun dead(rock, 1U);
        PLANETSIM_EXPECT(test, dead.circulation() == nullptr);
        planetsim::Scenario coarse;
        coarse.subdivision = 2;
        const planetsim::PlanetRun low(coarse, 1U);
        PLANETSIM_EXPECT(test, low.circulation() == nullptr);
        PLANETSIM_EXPECT(test, planetsim::climate_circulation_resolves(run.state().mesh()));
    }

    // 10. Reference mode starts from the balanced circulation (ADR-0011
    // §4.3): p_s from the balanced sea-level pressure on the winds' orography,
    // the winds from the cells' balanced winds; mass held, and the
    // start is deterministic. Without the circulation it starts from rest.
    {
        const auto mass = [](const planetsim::PlanetRun& r) {
            double sum = 0.0;
            for (const auto& cell : r.state().mesh().cells()) {
                sum += cell.area_m2 * r.state().slow().atmosphere_surface_pressure_Pa[cell.id];
            }
            return sum;
        };
        const double initial_mass = mass(run);
        for (auto* r : {&run, &serial, &without}) {
            r->submit({year_end, "test", "set_mode", "reference"});
            r->run_until(year_end + 10);   // the first reference step
        }
        const auto first_ps = run.state().slow().atmosphere_surface_pressure_Pa;
        const auto first_rest_ps = without.state().slow().atmosphere_surface_pressure_Pa;
        for (auto* r : {&run, &serial, &without}) {
            r->run_until(year_end + 2 * 1'440);   // two days
        }
        double drift = 0.0;
        double drift_rest = 0.0;
        for (std::size_t i = 0; i < first_ps.size(); ++i) {
            drift += std::abs(run.state().slow().atmosphere_surface_pressure_Pa[i] - first_ps[i]);
            drift_rest +=
                std::abs(without.state().slow().atmosphere_surface_pressure_Pa[i] - first_rest_ps[i]);
        }
        drift /= static_cast<double>(first_ps.size());
        drift_rest /= static_cast<double>(first_ps.size());
        std::cerr << "two days after the first step: mean |Δp_s| " << drift
                  << " Pa from the balanced start, " << drift_rest << " Pa from rest\n";
        const auto& last = run.dynamics()->last();
        PLANETSIM_EXPECT(test, last.starts == 1U && last.started_from_balance);
        PLANETSIM_EXPECT(test, without.dynamics()->last().starts == 1U &&
                                   !without.dynamics()->last().started_from_balance);
        PLANETSIM_EXPECT(test, last.max_wind_m_s > 10.0 && last.max_wind_m_s < 100.0);
        // The balanced start sheds far less mass to the core's orography.
        PLANETSIM_EXPECT(test, drift < 0.5 * drift_rest);
        PLANETSIM_EXPECT_NEAR(test, mass(run) / initial_mass, 1.0, 1.0e-12);
        PLANETSIM_EXPECT(test, run.state_hash() == serial.state_hash());
        PLANETSIM_EXPECT(test, run.state_hash() != without.state_hash());
        std::cerr << "reference start: max wind after two days " << last.max_wind_m_s
                  << " m/s (from rest: " << without.dynamics()->last().max_wind_m_s << ")\n";
    }
    return test.result();
}
