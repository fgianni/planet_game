// INV-05: the structure of reference mode's strongest winds. Every hour,
// the cell with the fastest top-layer wind (the circulation's cell winds);
// per month, averages of: its speed against its neighbours' mean (a spike
// or a smooth jet), its eastward share u/|V| (a zonal jet near +1), its rain
// and its vertical mass flux against its neighbours', and how the maxima
// split by latitude (|φ| in 10° bins from 50°). Not a gate.
//
//   reference_wind_structure_probe LEVEL MONTHS [WORKERS]
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
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
    PlanetRun run(scenario, workers);
    const auto& mesh = run.state().mesh();
    const std::size_t cells = mesh.cell_count();
    std::vector<double> latitude(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        latitude[c] = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
    }
    const std::size_t top = 2;
    std::cout << std::fixed << std::setprecision(2);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        int samples = 0, steps = 0;
        int bins[5] = {};   // |φ| < 50, 50–60, 60–70, 70–80, 80–90
        double spike = 0.0, zonal = 0.0, rain_ratio = 0.0, ascent_ratio = 0.0, speed = 0.0;
        int rain_n = 0, ascent_n = 0;
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                if (++steps % 6 != 0) continue;   // hourly
                const auto& circulation = run.state().circulation();
                if (circulation.eastward_wind_m_s.layer_count() <= top) continue;
                const auto u = circulation.eastward_wind_m_s.layer(top);
                const auto v = circulation.northward_wind_m_s.layer(top);
                // The vertical mass flux at the lowest interface, where the
                // circulation has one (reference mode may not fill it).
                const bool has_w = circulation.vertical_mass_flux_kg_m2_s.layer_count() > 0;
                const auto& rain = run.state().forcing().precipitation_kg_m2_s;
                std::size_t best = 0;
                double best_speed = -1.0;
                for (std::size_t c = 0; c < cells; ++c) {
                    const double s = std::hypot(u[c], v[c]);
                    if (s > best_speed) { best_speed = s; best = c; }
                }
                double ring_speed = 0.0, ring_rain = 0.0, ring_ascent = 0.0;
                const auto neighbours = mesh.cell_edges(CellId{static_cast<std::uint32_t>(best)});
                for (const auto& n : neighbours) {
                    const std::size_t j = n.neighbor.to_index();
                    ring_speed += std::hypot(u[j], v[j]);
                    ring_rain += rain[j];
                    if (has_w) ring_ascent += std::abs(circulation.vertical_mass_flux_kg_m2_s.layer(0)[j]);
                }
                const double count = static_cast<double>(neighbours.size());
                ring_speed /= count; ring_rain /= count; ring_ascent /= count;
                ++samples;
                speed += best_speed;
                spike += best_speed / std::max(ring_speed, 1e-9);
                zonal += u[best] / std::max(best_speed, 1e-9);
                if (ring_rain > 1e-9) { rain_ratio += rain[best] / ring_rain; ++rain_n; }
                if (has_w && ring_ascent > 1e-12) {
                    ascent_ratio += std::abs(circulation.vertical_mass_flux_kg_m2_s.layer(0)[best]) / ring_ascent;
                    ++ascent_n;
                }
                const double a = std::abs(latitude[best]);
                ++bins[a < 50.0 ? 0 : std::min(4, static_cast<int>((a - 40.0) / 10.0))];
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        const double n = std::max(samples, 1);
        std::cout << "month " << std::setw(3) << month + 1 << ": fastest " << std::setw(6) << speed / n
                  << " m/s, against neighbours ×" << spike / n << ", eastward share " << zonal / n
                  << ", rain ×" << (rain_n > 0 ? rain_ratio / rain_n : 0.0) << ", |ascent| ×"
                  << (ascent_n > 0 ? ascent_ratio / ascent_n : 0.0) << " | |φ| <50 " << 100.0 * bins[0] / n
                  << "%, 50–60 " << 100.0 * bins[1] / n << "%, 60–70 " << 100.0 * bins[2] / n << "%, 70–80 "
                  << 100.0 * bins[3] / n << "%, 80–90 " << 100.0 * bins[4] / n << "%" << std::endl;
    }
    return 0;
}
