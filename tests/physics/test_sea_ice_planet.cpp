#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

namespace {

using planetsim::PlanetPreset;

constexpr std::uint64_t test_seed = 20'260'930U;
constexpr int spin_up_years = 15;

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface =
        planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    explicit Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh)
        : mesh(std::move(shared_mesh)), state(mesh) {
        static_cast<void>(planetsim::generate_terrain(
            state, test_seed, planetsim::geology_parameters_for(PlanetPreset::earth_like), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_surface_temperatures(*mesh, state.slow(), parameters, surface, 4U);
        planetsim::initialise_cryosphere(*mesh, state.slow());
    }

    planetsim::SurfaceEnergyDiagnostics climate_step(std::int64_t index, std::size_t workers) {
        const auto substep = planetsim::climate_substep(index, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep, workers);
        return planetsim::step_surface_energy(
            state, parameters, surface, fractions, state.forcing().substep_mean_insolation_W_m2,
            planetsim::simulation_time_s(substep.length_ticks()), workers);
    }

    planetsim::SurfaceEnergyDiagnostics reference_step(planetsim::SimulationTick begin,
                                                       std::size_t workers) {
        planetsim::update_solar_forcing(state, parameters, begin + 5, workers);
        return planetsim::step_surface_energy(state, parameters, surface, fractions,
                                              state.forcing().top_of_atmosphere_insolation_W_m2,
                                              600.0, workers);
    }
};

template <typename T>
[[nodiscard]] bool bit_identical(const planetsim::Field2D<T>& first,
                                 const planetsim::Field2D<T>& second) {
    return first.size() == second.size() &&
           std::memcmp(first.values().data(), second.values().data(),
                       first.size() * sizeof(T)) == 0;
}

[[nodiscard]] bool invariants_hold(const Planet& planet) {
    const auto& slow = planet.state.slow();
    for (std::size_t cell = 0; cell < planet.mesh->cell_count(); ++cell) {
        const double ice = slow.sea_ice_mass_kg_m2[cell];
        const double mixed = slow.ocean_mixed_layer_temperature_K[cell];
        if (!(ice >= 0.0) || mixed < planetsim::seawater_freezing_point_K ||
            (ice > 0.0 && mixed != planetsim::seawater_freezing_point_K) ||
            slow.ocean_deep_temperature_K[cell] < planetsim::seawater_freezing_point_K - 1e-9) {
            return false;
        }
    }
    return true;
}

// ADR-0008 V1, V2 and V4 with sea ice on the Earth-like planet (transport
// on): three years of climate steps from the initial state, then a day of
// reference steps; ice forms at high latitudes only and has a seasonal
// cycle.
void check_budgets_and_ice(planetsim::test::Context& test,
                           const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet planet(mesh);
    double worst_energy = 0.0;
    double worst_water = 0.0;
    double north_min = 1e300;
    double north_max = 0.0;
    double south_min = 1e300;
    double south_max = 0.0;
    for (std::int64_t index = 0; index < 3 * planetsim::climate_substeps_per_year; ++index) {
        const auto step = planet.climate_step(index, 4U);
        worst_energy = std::max(worst_energy, step.closure_residual_J() / step.closure_gate_J());
        worst_water = std::max(worst_water, step.water_residual_kg() / step.water_gate_kg());
        if (index >= 2 * planetsim::climate_substeps_per_year) {
            north_min = std::min(north_min, step.ice_area_north_m2);
            north_max = std::max(north_max, step.ice_area_north_m2);
            south_min = std::min(south_min, step.ice_area_south_m2);
            south_max = std::max(south_max, step.ice_area_south_m2);
        }
    }
    const auto begin =
        planetsim::climate_substep(3 * planetsim::climate_substeps_per_year, planet.parameters)
            .begin_tick;
    for (std::int64_t step_index = 0; step_index < 144; ++step_index) {
        const auto step = planet.reference_step(begin + 10 * step_index, 4U);
        worst_energy = std::max(worst_energy, step.closure_residual_J() / step.closure_gate_J());
        worst_water = std::max(worst_water, step.water_residual_kg() / step.water_gate_kg());
    }
    double lowest_ice_latitude_deg = 90.0;
    const auto& slow = planet.state.slow();
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        if (slow.sea_ice_mass_kg_m2[cell] > 0.0) {
            lowest_ice_latitude_deg = std::min(
                lowest_ice_latitude_deg,
                std::abs(planetsim::latitude_rad(mesh->cells()[cell].center_unit)) * 180.0 /
                    std::numbers::pi);
        }
    }
    std::cout << "sea_ice worst_energy=" << worst_energy << " worst_water=" << worst_water
              << " north_area_km2 " << north_min / 1e6 << ".." << north_max / 1e6
              << " south_area_km2 " << south_min / 1e6 << ".." << south_max / 1e6
              << " lowest_ice_latitude_deg=" << lowest_ice_latitude_deg << '\n';
    PLANETSIM_EXPECT(test, worst_energy <= 1.0);
    PLANETSIM_EXPECT(test, worst_water <= 1.0);
    PLANETSIM_EXPECT(test, invariants_hold(planet));
    PLANETSIM_EXPECT(test, north_max > 0.0 && south_max > 0.0);
    PLANETSIM_EXPECT(test, north_max > north_min && south_max > south_min);
    PLANETSIM_EXPECT(test, lowest_ice_latitude_deg > 30.0);
}

// ADR-0008 V8 with sea ice: 1, 2, 8 and 16 workers give the same state.
void check_workers(planetsim::test::Context& test,
                   const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    std::vector<std::unique_ptr<Planet>> planets;
    for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
        auto planet = std::make_unique<Planet>(mesh);
        for (std::int64_t index = 0; index < 18; ++index) {
            static_cast<void>(planet->climate_step(index, workers));
        }
        planets.push_back(std::move(planet));
    }
    bool identical = true;
    for (const auto& planet : planets) {
        const auto& first = planets.front()->state.slow();
        const auto& slow = planet->state.slow();
        identical = identical &&
                    bit_identical(first.sea_ice_mass_kg_m2, slow.sea_ice_mass_kg_m2) &&
                    bit_identical(first.ocean_mixed_layer_temperature_K,
                                  slow.ocean_mixed_layer_temperature_K) &&
                    bit_identical(first.ocean_deep_temperature_K, slow.ocean_deep_temperature_K);
    }
    PLANETSIM_EXPECT(test, identical);
}

// ADR-0008 V5 for sea ice: the same spun-up state with and without 1 m of
// extra ice on every ocean tile, under the same forcing. The ice lowers the
// absorbed shortwave and the global mean temperature a year later.
void check_albedo_feedback_sign(planetsim::test::Context& test,
                                const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet open(mesh);
    static_cast<void>(planetsim::spin_up_surface_energy(open.state, open.parameters, open.surface,
                                                        open.fractions, spin_up_years, 4U));
    Planet icy(mesh);
    icy.state.slow() = open.state.slow();
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        icy.state.slow().sea_ice_mass_kg_m2[cell] += planetsim::sea_ice_density_kg_m3;
        icy.state.slow().ocean_mixed_layer_temperature_K[cell] =
            planetsim::seawater_freezing_point_K;
    }
    const std::int64_t first = spin_up_years * planetsim::climate_substeps_per_year;
    const auto open_first = open.climate_step(first, 4U);
    const auto icy_first = icy.climate_step(first, 4U);
    planetsim::SurfaceEnergyDiagnostics open_last;
    planetsim::SurfaceEnergyDiagnostics icy_last;
    for (std::int64_t index = first + 1; index < first + planetsim::climate_substeps_per_year;
         ++index) {
        open_last = open.climate_step(index, 4U);
        icy_last = icy.climate_step(index, 4U);
    }
    std::cout << "albedo_sign absorbed_open_W=" << open_first.absorbed_W
              << " absorbed_icy_W=" << icy_first.absorbed_W
              << " mean_open_K=" << open_last.mean_surface_temperature_K
              << " mean_icy_K=" << icy_last.mean_surface_temperature_K << '\n';
    PLANETSIM_EXPECT(test, icy_first.absorbed_W < open_first.absorbed_W);
    PLANETSIM_EXPECT(test,
                     icy_last.mean_surface_temperature_K < open_last.mean_surface_temperature_K);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(4U, 6'371'000.0));
    check_budgets_and_ice(test, mesh);
    check_workers(test, mesh);
    check_albedo_feedback_sign(test, mesh);
    return test.result();
}
