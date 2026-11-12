// INV-05: reference mode's spin-up trajectory at one mesh level, month by
// month from the initial state, to compare levels. Not a gate.
//
//   reference_trajectory_probe LEVEL MONTHS [WORKERS] [W_C] [ORDER] [DAMPING_H] [TAU_C_H] [BETA] [WIND]
//
// W_C, when positive, sets the clouds' in-cloud water path (kg/m², ADR-0023
// §13.3; the preset's 0.05) for INV-05's overcast test; ORDER the vapour's
// horizontal advection (1 upwind, the preset; 2 limited second-order);
// DAMPING_H, when positive, the dynamics' grid-scale damping time (hours;
// the preset's 8); TAU_C_H, when positive, the resolved condensation's
// relaxation time (hours; the preset's 12; unused with moist convection,
// INV-05 §15); BETA, when positive, the sub-grid condensation width
// (AtmosphereParameters::cloud_subgrid_width, M8-07; the preset's 0); WIND 0
// evaporates with the gustiness alone (the preset's 1: with the resolved
// wind too).
//
// Each month prints: the largest wind of any step (the dynamics' own
// maximum), the top layer's zonal-mean eastward wind at its strongest in
// each hemisphere (5° bands, sampled hourly) and where, the P2
// equator-to-pole difference, the polar caps' (poleward of 60°) and the
// tropics' (30° S–30° N) mean surface temperatures, sea-ice cover and the
// global mean. A run that stops (the dynamics' wind bound) prints why.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

using namespace planetsim;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto level = static_cast<std::uint32_t>(std::atoi(argv[1]));
    const int months = std::atoi(argv[2]);
    const std::size_t workers = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 16U;
    Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 0;
    scenario.initial_mode = SimulationMode::reference;
    const double w_c = argc > 4 ? std::atof(argv[4]) : -1.0;
    const int order = argc > 5 ? std::atoi(argv[5]) : 1;
    const double damping_h = argc > 6 ? std::atof(argv[6]) : 0.0;
    const double tau_c_h = argc > 7 ? std::atof(argv[7]) : 0.0;
    const double beta = argc > 8 ? std::atof(argv[8]) : 0.0;
    const bool resolved_wind = argc > 9 ? std::atoi(argv[9]) != 0 : true;
    PlanetRun run(scenario, workers, ClimateCirculationUse::coupled, [=](SurfaceEnergyParameters& p) {
        if (w_c > 0.0) p.atmosphere.cloud_in_cloud_water_path_kg_m2 = w_c;
        p.vapour_advection_order = order;
        if (damping_h > 0.0) p.hyperviscosity_damping_s = damping_h * 3600.0;
        if (tau_c_h > 0.0) p.condensation_time_s = tau_c_h * 3600.0;
        if (beta > 0.0) p.atmosphere.cloud_subgrid_width = beta;
        p.evaporation_resolved_wind = resolved_wind;
    });
    const auto& mesh = run.state().mesh();
    const std::size_t cells = mesh.cell_count();
    std::vector<double> latitude(cells);
    std::vector<std::size_t> band(cells);
    std::vector<double> band_area(36, 0.0);
    double area = 0.0;
    for (std::size_t c = 0; c < cells; ++c) {
        latitude[c] = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        band[c] = static_cast<std::size_t>(std::clamp(std::floor((latitude[c] + 90.0) / 5.0), 0.0, 35.0));
        band_area[band[c]] += mesh.cells()[c].area_m2;
        area += mesh.cells()[c].area_m2;
    }
    const std::size_t top = 2;
    std::cout << std::fixed << std::setprecision(2);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        double max_wind = 0.0, seconds = 0.0, p2 = 0.0, global = 0.0, polar = 0.0, tropics = 0.0, ice = 0.0;
        double polar_w = 0.0, tropics_w = 0.0;
        std::vector<double> u_band(36, 0.0);
        int samples = 0, steps = 0;
        try {
            while (run.tick() < end) {
                // Whole 10-minute steps: a calendar month does not end on one,
                // and a target short of the next step would not advance the
                // run (the month then ends up to 9 minutes late).
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                ++steps;
                if (run.dynamics() != nullptr) max_wind = std::max(max_wind, run.dynamics()->last().max_wind_m_s);
                const auto& step = run.last_step();
                seconds += step.duration_s;
                p2 += step.p2_surface_temperature_K * step.duration_s;
                global += step.mean_surface_temperature_K * step.duration_s;
                ice += (step.ice_cover_north_m2 + step.ice_cover_south_m2) * step.duration_s;
                const auto& z = step.zonal_mean_surface_temperature_K;   // 10° bands from 90° S
                for (int b = 0; b < 18; ++b) {
                    const double lat = -85.0 + 10.0 * b;
                    const double w = std::cos(lat * std::numbers::pi / 180.0) * step.duration_s;
                    if (std::abs(lat) > 60.0) { polar += z[b] * w; polar_w += w; }
                    if (std::abs(lat) < 30.0) { tropics += z[b] * w; tropics_w += w; }
                }
                if (steps % 6 == 0) {   // hourly
                    const auto& u = run.state().circulation().eastward_wind_m_s;
                    if (u.layer_count() > top) {
                        const auto layer = u.layer(top);   // once: the accessor is not free
                        for (std::size_t c = 0; c < cells; ++c) {
                            u_band[band[c]] += mesh.cells()[c].area_m2 * layer[c];
                        }
                        ++samples;
                    }
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        double south = -1e9, north = -1e9, south_lat = 0.0, north_lat = 0.0;
        for (int b = 0; b < 36; ++b) {
            const double lat = -87.5 + 5.0 * b;
            const double u = samples > 0 ? u_band[b] / (band_area[b] * samples) : 0.0;
            if (lat < 0.0 && u > south) { south = u; south_lat = lat; }
            if (lat > 0.0 && u > north) { north = u; north_lat = lat; }
        }
        std::cout << "month " << std::setw(3) << month + 1 << ": max wind " << std::setw(6) << max_wind
                  << " m/s | jets S " << std::setw(5) << south << " @" << std::setw(6) << south_lat
                  << ", N " << std::setw(5) << north << " @" << std::setw(5) << north_lat
                  << " | P2 " << std::setw(5) << -1.5 * p2 / seconds << " K | polar " << polar / polar_w
                  << " tropics " << tropics / tropics_w << " global " << global / seconds
                  << " K | ice " << ice / (area * seconds) << std::endl;
    }
    return 0;
}
