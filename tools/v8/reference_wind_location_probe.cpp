// INV-05: where reference mode's strongest winds are. Every hour, the
// edge and layer with the largest normal wind; per month, how those hourly
// maxima split between high or steep terrain and heavy rain, and by layer
// and latitude. Separates orographic flow from grid-point storms. Not a gate.
//
//   reference_wind_location_probe LEVEL MONTHS [WORKERS]
//
// Terrain: each cell's median sub-cell elevation (hypsometry quantile 5 of
// 9), sea level where below it. Rain: resolved plus convective, mm/day, the
// larger of the edge's two cells.
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
    std::vector<double> height(cells), latitude(cells);
    const auto median = run.state().slow().hypsometry_m.layer(4);
    for (std::size_t c = 0; c < cells; ++c) {
        height[c] = std::max(0.0, static_cast<double>(median[c]));
        latitude[c] = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
    }
    const auto edges = mesh.edges();
    std::cout << std::fixed << std::setprecision(1);
    for (int month = 0; month < months; ++month) {
        const SimulationTick end = climate_substep_containing(run.tick(), run.parameters()).end_tick;
        int samples = 0, high = 0, steep = 0, rainy = 0, tropical = 0, polar = 0;
        // The same shares over all edges at the same hours: the baseline.
        double all_high = 0.0, all_steep = 0.0, all_rainy = 0.0, all_tropical = 0.0, all_polar = 0.0;
        int by_layer[3] = {};
        double largest = 0.0, largest_lat = 0.0, largest_height = 0.0, largest_rain = 0.0;
        int largest_layer = 0;
        int steps = 0;
        try {
            while (run.tick() < end) {
                const SimulationTick before = run.tick();
                run.run_until(run.tick() + 10);
                if (run.tick() == before) throw std::runtime_error("the run did not advance");
                if (++steps % 6 != 0) continue;   // hourly
                const auto* fast = run.state().fast_state();
                if (fast == nullptr) continue;
                const auto& u = fast->atmosphere_edge_normal_wind_m_s;
                double best = 0.0;
                std::size_t best_edge = 0, best_layer = 0;
                for (std::size_t k = 0; k < u.layer_count() && k < 3; ++k) {
                    const auto layer = u.layer(k);
                    for (std::size_t e = 0; e < layer.size(); ++e) {
                        if (std::abs(layer[e]) > best) { best = std::abs(layer[e]); best_edge = e; best_layer = k; }
                    }
                }
                const std::size_t a = edges[best_edge].first_cell.to_index();
                const std::size_t b = edges[best_edge].second_cell.to_index();
                const auto& forcing = run.state().forcing();
                const double rain = 86400.0 * std::max(forcing.precipitation_kg_m2_s[a], forcing.precipitation_kg_m2_s[b]);
                const double h = std::max(height[a], height[b]);
                const double step_m = std::abs(height[a] - height[b]);
                const double lat = 0.5 * (latitude[a] + latitude[b]);
                double n_high = 0, n_steep = 0, n_rainy = 0, n_tropical = 0, n_polar = 0;
                for (const auto& edge : edges) {
                    const std::size_t i = edge.first_cell.to_index(), j = edge.second_cell.to_index();
                    const double l = 0.5 * (latitude[i] + latitude[j]);
                    n_high += std::max(height[i], height[j]) >= 1000.0;
                    n_steep += std::abs(height[i] - height[j]) >= 500.0;
                    n_rainy += 86400.0 * std::max(forcing.precipitation_kg_m2_s[i], forcing.precipitation_kg_m2_s[j]) >= 20.0;
                    n_tropical += std::abs(l) < 30.0;
                    n_polar += std::abs(l) >= 60.0;
                }
                const double count = static_cast<double>(edges.size());
                all_high += n_high / count; all_steep += n_steep / count; all_rainy += n_rainy / count;
                all_tropical += n_tropical / count; all_polar += n_polar / count;
                ++samples;
                high += h >= 1000.0;
                steep += step_m >= 500.0;
                rainy += rain >= 20.0;
                tropical += std::abs(lat) < 30.0;
                polar += std::abs(lat) >= 60.0;
                ++by_layer[best_layer];
                if (best > largest) {
                    largest = best; largest_lat = lat; largest_height = h; largest_rain = rain;
                    largest_layer = static_cast<int>(best_layer);
                }
            }
        } catch (const std::exception& error) {
            std::cout << "month " << month + 1 << ": STOPPED: " << error.what() << std::endl;
            return 1;
        }
        const auto share = [&](int n) { return samples > 0 ? 100.0 * n / samples : 0.0; };
        const auto base = [&](double n) { return samples > 0 ? 100.0 * n / samples : 0.0; };
        std::cout << "month " << std::setw(3) << month + 1 << ": strongest " << std::setw(6) << largest
                  << " m/s at " << std::setw(6) << largest_lat << "°, layer " << largest_layer << ", terrain "
                  << std::setw(5) << largest_height << " m, rain " << std::setw(5) << largest_rain
                  << " mm/d | hourly maxima (all edges): terrain≥1 km " << share(high) << "% (" << base(all_high)
                  << "), step≥500 m " << share(steep) << "% (" << base(all_steep) << "), rain≥20 mm/d "
                  << share(rainy) << "% (" << base(all_rainy) << "), tropics " << share(tropical) << "% ("
                  << base(all_tropical) << "), polar " << share(polar) << "% (" << base(all_polar) << "), layers " << share(by_layer[0]) << "/" << share(by_layer[1]) << "/"
                  << share(by_layer[2]) << "%" << std::endl;
    }
    return 0;
}
