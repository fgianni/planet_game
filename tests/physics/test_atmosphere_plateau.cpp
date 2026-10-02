#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

namespace {

constexpr double dome_radius_rad = 0.6;
constexpr double coast_m = 500.0;
constexpr double summit_m = 4'000.0;
constexpr int spin_up_years = 30;

// ADR-0010 V6: an aqua planet with one equatorial dome, 500 m at its coast
// rising to 4,000 m, under the Earth-like surface and atmosphere. Over the
// dome's cells within 20° of the equator, the annual-mean land surface
// temperature falls with height at 4–9.8 K/km: the column over high ground
// holds less mass and less optical depth, and the transport of θ_c brings
// it air reduced along Γ_c. No lapse rate is imposed on the surface.
void check_plateau(planetsim::test::Context& test) {
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(4U, 6'371'000.0));
    const auto parameters = planetsim::PlanetParameters::earth_development();
    const auto surface =
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);
    planetsim::PlanetState state(mesh);
    const std::size_t cells = mesh->cell_count();
    std::vector<double> height(cells, -4'000.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        // Angular distance from (0° N, 0° E), the +x axis.
        const double distance =
            std::acos(std::clamp(mesh->cells()[cell].center_unit.x, -1.0, 1.0));
        if (distance < dome_radius_rad) {
            height[cell] = coast_m + (summit_m - coast_m) * (1.0 - distance / dome_radius_rad);
        }
        for (std::size_t layer = 0; layer < planetsim::hypsometry_layer_count; ++layer) {
            state.slow().hypsometry_m.layer(layer)[cell] = static_cast<float>(height[cell]);
        }
    }
    state.slow().sea_level_m = 0.0;
    const auto fractions = planetsim::compute_surface_fractions(
        *mesh, state.slow().hypsometry_m, state.slow().sea_level_m, 4U);
    planetsim::initialise_climate(*mesh, state.slow(), parameters, surface, 4U);
    static_cast<void>(planetsim::spin_up_surface_energy(state, parameters, surface, fractions,
                                                        spin_up_years, 4U));

    // A year of annual means.
    std::vector<double> mean_K(cells, 0.0);
    double total_s = 0.0;
    for (std::int64_t month = 0; month < planetsim::climate_substeps_per_year; ++month) {
        const auto substep = planetsim::climate_substep(month, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep, 4U);
        const double dt = planetsim::simulation_time_s(substep.length_ticks());
        static_cast<void>(planetsim::step_surface_energy(
            state, parameters, surface, fractions, state.forcing().substep_mean_insolation_W_m2,
            dt, 4U));
        for (std::size_t cell = 0; cell < cells; ++cell) {
            mean_K[cell] += dt * state.slow().land_surface_temperature_K[cell];
        }
        total_s += dt;
    }
    // Least-squares slope of temperature on height.
    double n = 0.0;
    double sz = 0.0;
    double st = 0.0;
    double szz = 0.0;
    double szt = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const double latitude = planetsim::latitude_rad(mesh->cells()[cell].center_unit);
        if (fractions.land_fraction[cell] < 0.99F ||
            std::abs(latitude) > 20.0 * std::numbers::pi / 180.0) {
            continue;
        }
        const double z_km = height[cell] / 1'000.0;
        const double t = mean_K[cell] / total_s;
        n += 1.0;
        sz += z_km;
        st += t;
        szz += z_km * z_km;
        szt += z_km * t;
    }
    const double slope = (n * szt - sz * st) / (n * szz - sz * sz);
    std::cout << "plateau cells=" << n << " lapse_K_per_km=" << -slope
              << " mean_K=" << st / n << '\n';
    PLANETSIM_EXPECT(test, n >= 10.0);
    PLANETSIM_EXPECT(test, -slope >= 4.0 && -slope <= 9.8);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_plateau(test);
    return test.result();
}
