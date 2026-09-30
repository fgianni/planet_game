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
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using planetsim::PlanetPreset;

constexpr std::uint64_t test_seed = 20'260'930U;
constexpr std::uint32_t test_level = 4U;
constexpr int spin_up_years = 30;
// About 950 mm of water a year everywhere (ADR-0008 §3.3 B: prescribed).
constexpr float snowfall_rate_kg_m2_s = 3e-5F;

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

    void set_precipitation(float rate) {
        auto& field = state.forcing().prescribed_precipitation_kg_m2_s;
        for (std::size_t cell = 0; cell < field.size(); ++cell) {
            field[cell] = rate;
        }
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

// ADR-0008 V1, V2 and V4 on the planet with prescribed snowfall, over three
// years of climate steps and a day of reference steps. Without horizontal
// heat transport, snow-covered land cannot warm back to the melting point
// (ADR-0008 §9 finding): snow accumulates year on year and melts only at
// the edges, so the test requires accumulation and some melt, not a
// seasonal cycle.
void check_budgets_and_seasons(planetsim::test::Context& test,
                               const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet planet(mesh);
    planet.set_precipitation(snowfall_rate_kg_m2_s);
    double worst_energy = 0.0;
    double worst_water = 0.0;
    double total_melt = 0.0;
    double year_two_snow = 0.0;
    double year_three_snow = 0.0;
    for (std::int64_t index = 0; index < 3 * planetsim::climate_substeps_per_year; ++index) {
        const auto step = planet.climate_step(index, 4U);
        worst_energy = std::max(worst_energy, step.closure_residual_J() / step.closure_gate_J());
        worst_water = std::max(worst_water, step.water_residual_kg() / step.water_gate_kg());
        total_melt += step.melt_kg;
        if (index == 2 * planetsim::climate_substeps_per_year - 1) {
            year_two_snow = step.snow_kg;
        }
        year_three_snow = step.snow_kg;
    }
    const auto begin =
        planetsim::climate_substep(3 * planetsim::climate_substeps_per_year, planet.parameters)
            .begin_tick;
    for (std::int64_t step_index = 0; step_index < 144; ++step_index) {
        const auto step = planet.reference_step(begin + 10 * step_index, 4U);
        worst_energy = std::max(worst_energy, step.closure_residual_J() / step.closure_gate_J());
        worst_water = std::max(worst_water, step.water_residual_kg() / step.water_gate_kg());
    }
    bool invariant = true;
    const auto& slow = planet.state.slow();
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        const double snow = slow.land_snow_water_equivalent_kg_m2[cell];
        invariant = invariant && snow >= 0.0 &&
                    (snow == 0.0 ||
                     slow.land_surface_temperature_K[cell] <= planetsim::melting_point_K);
    }
    std::cout << "snow worst_energy_ratio=" << worst_energy << " worst_water_ratio=" << worst_water
              << " snow_end_year2_kg=" << year_two_snow
              << " snow_end_year3_kg=" << year_three_snow << " melt_kg=" << total_melt << '\n';
    PLANETSIM_EXPECT(test, worst_energy <= 1.0);
    PLANETSIM_EXPECT(test, worst_water <= 1.0);
    PLANETSIM_EXPECT(test, invariant);
    PLANETSIM_EXPECT(test, total_melt > 0.0);
    PLANETSIM_EXPECT(test, year_three_snow > year_two_snow);
}

// ADR-0008 V8 with snow: 1, 2, 8 and 16 workers give the same state.
void check_workers(planetsim::test::Context& test,
                   const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    std::vector<std::unique_ptr<Planet>> planets;
    for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
        auto planet = std::make_unique<Planet>(mesh);
        planet->set_precipitation(snowfall_rate_kg_m2_s);
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
                    bit_identical(first.land_snow_water_equivalent_kg_m2,
                                  slow.land_snow_water_equivalent_kg_m2) &&
                    bit_identical(first.land_surface_temperature_K,
                                  slow.land_surface_temperature_K) &&
                    bit_identical(first.land_ground_temperature_K, slow.land_ground_temperature_K);
    }
    PLANETSIM_EXPECT(test, identical);
}

// ADR-0008 V5 for snow: the same state with and without snow on the land
// tiles, under the same forcing. Snow lowers the absorbed shortwave and,
// cell by cell, the new land surface temperature.
void check_albedo_feedback_sign(planetsim::test::Context& test,
                                const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet bare(mesh);
    static_cast<void>(planetsim::spin_up_surface_energy(bare.state, bare.parameters, bare.surface,
                                                        bare.fractions, spin_up_years, 4U));
    Planet snowy(mesh);
    snowy.state.slow() = bare.state.slow();
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        snowy.state.slow().land_snow_water_equivalent_kg_m2[cell] = 50.0;
    }
    const std::int64_t month = spin_up_years * planetsim::climate_substeps_per_year + 6;
    const auto bare_step = bare.climate_step(month, 4U);
    const auto snowy_step = snowy.climate_step(month, 4U);
    bool colder = true;
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        colder = colder && snowy.state.slow().land_surface_temperature_K[cell] <=
                               bare.state.slow().land_surface_temperature_K[cell];
    }
    std::cout << "albedo_sign absorbed_bare_W=" << bare_step.absorbed_W
              << " absorbed_snowy_W=" << snowy_step.absorbed_W
              << " land_mean_bare_K=" << bare_step.land_mean_surface_temperature_K
              << " land_mean_snowy_K=" << snowy_step.land_mean_surface_temperature_K << '\n';
    PLANETSIM_EXPECT(test, snowy_step.absorbed_W < bare_step.absorbed_W);
    PLANETSIM_EXPECT(test, colder);
    PLANETSIM_EXPECT(test, snowy_step.land_mean_surface_temperature_K <
                               bare_step.land_mean_surface_temperature_K);
}

// ADR-0008 V6: without precipitation the snow stays zero everywhere, however
// cold the land gets.
void check_no_precipitation(planetsim::test::Context& test,
                            const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet planet(mesh);
    double snow = 0.0;
    double latent = 0.0;
    for (std::int64_t index = 0; index < 2 * planetsim::climate_substeps_per_year; ++index) {
        const auto step = planet.climate_step(index, 4U);
        snow += step.snow_kg + step.snowfall_kg;
        latent += step.latent_heat_J;
    }
    PLANETSIM_EXPECT(test, snow == 0.0);
    PLANETSIM_EXPECT(test, latent == 0.0);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(test_level, 6'371'000.0));
    check_budgets_and_seasons(test, mesh);
    check_workers(test, mesh);
    check_albedo_feedback_sign(test, mesh);
    check_no_precipitation(test, mesh);
    return test.result();
}
