#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {

using planetsim::PlanetPreset;
using planetsim::SimulationMode;

constexpr std::uint64_t test_seed = 20'260'929U;
constexpr std::uint32_t test_level = 4U;
constexpr int spin_up_years = 60;

// A generated planet with its surface fractions and equilibrium temperatures.
struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface;
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh, PlanetPreset preset,
           planetsim::SurfaceEnergyParameters surface_parameters)
        : mesh(std::move(shared_mesh)), surface(surface_parameters), state(mesh) {
        if (preset == PlanetPreset::dead_rock) {
            parameters.axial_tilt_rad = 0.0;   // experiment A (specification §13.1)
        }
        static_cast<void>(planetsim::generate_terrain(
            state, test_seed, planetsim::geology_parameters_for(preset), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_surface_temperatures(*mesh, state.slow(), parameters, surface, 4U);
        planetsim::initialise_cryosphere(*mesh, state.slow());
    }
};

[[nodiscard]] std::shared_ptr<const planetsim::PlanetMesh> mesh_at(std::uint32_t level) {
    return std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, 6'371'000.0));
}

template <typename T>
[[nodiscard]] bool bit_identical(const planetsim::Field2D<T>& first,
                                 const planetsim::Field2D<T>& second) {
    return first.size() == second.size() &&
           std::memcmp(first.values().data(), second.values().data(),
                       first.size() * sizeof(T)) == 0;
}

[[nodiscard]] bool same_temperatures(const planetsim::SlowState& first,
                                     const planetsim::SlowState& second) {
    return bit_identical(first.land_surface_temperature_K, second.land_surface_temperature_K) &&
           bit_identical(first.land_ground_temperature_K, second.land_ground_temperature_K) &&
           bit_identical(first.ocean_mixed_layer_temperature_K,
                         second.ocean_mixed_layer_temperature_K) &&
           bit_identical(first.ocean_deep_temperature_K, second.ocean_deep_temperature_K);
}

// The V2 gate of the column test (ADR-0007 §9), summed over tiles: 1e-9 of
// the flux scale plus the rounding floor of the stored energy.
[[nodiscard]] double closure_ratio(const planetsim::SurfaceEnergyDiagnostics& step) {
    return step.closure_residual_J() / step.closure_gate_J();
}

// The cell whose centre is nearest the given latitude, at longitude near 0.
[[nodiscard]] std::size_t cell_near(const planetsim::PlanetMesh& mesh, double latitude_rad) {
    const planetsim::Vec3d target{std::cos(latitude_rad), 0.0, std::sin(latitude_rad)};
    std::size_t best = 0;
    double best_dot = -2.0;
    for (const auto& cell : mesh.cells()) {
        const double d = planetsim::dot(cell.center_unit, target);
        if (d > best_dot) {
            best_dot = d;
            best = cell.id.to_index();
        }
    }
    return best;
}

// V2 globally on climate and reference steps; V8 over worker counts.
void check_closure_and_workers(planetsim::test::Context& test,
                               const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    const auto surface = planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    std::vector<std::unique_ptr<Planet>> planets;
    double worst_closure = 0.0;
    double worst_newton = 0.0;
    for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
        auto planet = std::make_unique<Planet>(mesh, PlanetPreset::earth_like, surface);
        for (std::int64_t index = 0; index < 3; ++index) {
            const auto substep = planetsim::climate_substep(index, planet->parameters);
            planetsim::update_substep_mean_insolation(planet->state, planet->parameters, substep,
                                                      workers);
            const auto step = planetsim::step_surface_energy(
                planet->state, planet->parameters, surface, planet->fractions,
                planet->state.forcing().substep_mean_insolation_W_m2,
                planetsim::simulation_time_s(substep.length_ticks()), workers);
            worst_closure = std::max(worst_closure, closure_ratio(step));
            worst_newton = std::max(worst_newton, step.max_newton_residual_W_m2);
        }
        const auto begin = planetsim::climate_substep(3, planet->parameters).begin_tick;
        for (std::int64_t step_index = 0; step_index < 6; ++step_index) {
            planetsim::update_solar_forcing(planet->state, planet->parameters,
                                            begin + 10 * step_index + 5, workers);
            const auto step = planetsim::step_surface_energy(
                planet->state, planet->parameters, surface, planet->fractions,
                planet->state.forcing().top_of_atmosphere_insolation_W_m2, 600.0, workers);
            worst_closure = std::max(worst_closure, closure_ratio(step));
            worst_newton = std::max(worst_newton, step.max_newton_residual_W_m2);
        }
        planets.push_back(std::move(planet));
    }
    std::cout << "closure worst_ratio=" << worst_closure << " worst_newton_W_m2=" << worst_newton
              << '\n';
    PLANETSIM_EXPECT(test, worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, worst_newton <= 1e-6);
    bool identical = true;
    for (const auto& planet : planets) {
        identical = identical && same_temperatures(planets.front()->state.slow(),
                                                   planet->state.slow());
    }
    PLANETSIM_EXPECT(test, identical);
}

// V8 through the scheduler: two climate sub-steps, then a reference hour,
// in one run_until or in uneven chunks.
void check_scheduler_chunking(planetsim::test::Context& test,
                              const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    const auto surface = planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    const auto run = [&](const std::vector<planetsim::SimulationTick>& targets,
                         std::size_t workers) {
        auto planet = std::make_unique<Planet>(mesh, PlanetPreset::earth_like, surface);
        planetsim::SimulationClock clock;
        planetsim::Scheduler scheduler(clock, planetsim::make_orbital_calendar(planet->parameters));
        planetsim::SurfaceEnergyDiagnostics last;
        planetsim::register_surface_energy(scheduler, planet->state, planet->parameters, surface,
                                           planet->fractions, workers, &last);
        const auto switch_tick = planetsim::climate_substep(2, planet->parameters).begin_tick;
        for (const auto target : targets) {
            scheduler.run_until(std::min(target, switch_tick));
            if (clock.tick() >= switch_tick) {
                scheduler.request_mode(SimulationMode::reference);
                scheduler.run_until(target);
            }
        }
        return planet;
    };
    const auto end = planetsim::climate_substep(2, planetsim::PlanetParameters::earth_development())
                         .begin_tick +
                     60;
    const auto single = run({end}, 1U);
    const auto chunked = run({17'000, 60'000, 87'000, end - 25, end}, 8U);
    PLANETSIM_EXPECT(test, same_temperatures(single->state.slow(), chunked->state.slow()));
}

// V5: the seasonal (climate mode) and diurnal (reference mode) range of the
// land and ocean tiles of the same cells under the same forcing, g = 0.
void check_thermal_inertia(planetsim::test::Context& test,
                           const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    planetsim::SurfaceEnergyParameters surface;
    surface.land_material = planetsim::SurfaceMaterial::dry_soil;
    surface.grey_emissivity = 0.0;
    Planet planet(mesh, PlanetPreset::earth_like, surface);
    static_cast<void>(planetsim::spin_up_surface_energy(planet.state, planet.parameters, surface,
                                                        planet.fractions, spin_up_years, 4U));
    const double pi = 3.14159265358979323846;
    // 30° N: with g = 0 the ocean freezes further poleward (ADR-0008), and a
    // mixed layer under ice is held at T_f with no seasonal range.
    const std::size_t midlatitude = cell_near(*mesh, 30.0 * pi / 180.0);
    const std::size_t equator = cell_near(*mesh, 0.0);

    auto& slow = planet.state.slow();
    double land_min = 1e9;
    double land_max = 0.0;
    double ocean_min = 1e9;
    double ocean_max = 0.0;
    for (std::int64_t month = 0; month < 12; ++month) {
        const auto substep = planetsim::climate_substep(
            spin_up_years * planetsim::climate_substeps_per_year + month, planet.parameters);
        planetsim::update_substep_mean_insolation(planet.state, planet.parameters, substep, 4U);
        static_cast<void>(planetsim::step_surface_energy(
            planet.state, planet.parameters, surface, planet.fractions,
            planet.state.forcing().substep_mean_insolation_W_m2,
            planetsim::simulation_time_s(substep.length_ticks()), 4U));
        land_min = std::min<double>(land_min, slow.land_surface_temperature_K[midlatitude]);
        land_max = std::max<double>(land_max, slow.land_surface_temperature_K[midlatitude]);
        ocean_min = std::min<double>(ocean_min, slow.ocean_mixed_layer_temperature_K[midlatitude]);
        ocean_max = std::max<double>(ocean_max, slow.ocean_mixed_layer_temperature_K[midlatitude]);
    }
    const double seasonal_land = land_max - land_min;
    const double seasonal_ocean = ocean_max - ocean_min;

    // One day of ten-minute steps after a warm-up day, at the equator.
    const auto begin = planetsim::climate_substep(
                           (spin_up_years + 1) * planetsim::climate_substeps_per_year,
                           planet.parameters)
                           .begin_tick;
    double day_land_min = 1e9;
    double day_land_max = 0.0;
    double day_ocean_min = 1e9;
    double day_ocean_max = 0.0;
    for (std::int64_t step = 0; step < 2 * 144; ++step) {
        planetsim::update_solar_forcing(planet.state, planet.parameters, begin + 10 * step + 5,
                                        4U);
        static_cast<void>(planetsim::step_surface_energy(
            planet.state, planet.parameters, surface, planet.fractions,
            planet.state.forcing().top_of_atmosphere_insolation_W_m2, 600.0, 4U));
        if (step >= 144) {
            day_land_min = std::min<double>(day_land_min, slow.land_surface_temperature_K[equator]);
            day_land_max = std::max<double>(day_land_max, slow.land_surface_temperature_K[equator]);
            day_ocean_min =
                std::min<double>(day_ocean_min, slow.ocean_mixed_layer_temperature_K[equator]);
            day_ocean_max =
                std::max<double>(day_ocean_max, slow.ocean_mixed_layer_temperature_K[equator]);
        }
    }
    const double diurnal_land = day_land_max - day_land_min;
    const double diurnal_ocean = day_ocean_max - day_ocean_min;
    std::cout << "inertia seasonal_30N land_K=" << seasonal_land << " ocean_K=" << seasonal_ocean
              << " ratio=" << seasonal_land / seasonal_ocean << " diurnal_equator land_K="
              << diurnal_land << " ocean_K=" << diurnal_ocean
              << " ratio=" << diurnal_land / diurnal_ocean << '\n';
    PLANETSIM_EXPECT(test, seasonal_land > 2.0 * seasonal_ocean);
    PLANETSIM_EXPECT(test, diurnal_land > 10.0 * diurnal_ocean);
}

// V6 and V7: the presets after spin-up.
void check_experiments(planetsim::test::Context& test,
                       const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    // Experiment A: dead rock, g = 0, no axial tilt.
    {
        const auto surface = planetsim::surface_energy_parameters_for(PlanetPreset::dead_rock);
        Planet planet(mesh, PlanetPreset::dead_rock, surface);
        const auto year = planetsim::spin_up_surface_energy(
            planet.state, planet.parameters, surface, planet.fractions, spin_up_years, 4U);
        const std::size_t equator = cell_near(*mesh, 0.0);
        const auto begin = planetsim::climate_substep(
                               spin_up_years * planetsim::climate_substeps_per_year,
                               planet.parameters)
                               .begin_tick;
        double day_min = 1e9;
        double day_max = 0.0;
        for (std::int64_t step = 0; step < 2 * 144; ++step) {
            planetsim::update_solar_forcing(planet.state, planet.parameters,
                                            begin + 10 * step + 5, 4U);
            static_cast<void>(planetsim::step_surface_energy(
                planet.state, planet.parameters, surface, planet.fractions,
                planet.state.forcing().top_of_atmosphere_insolation_W_m2, 600.0, 4U));
            if (step >= 144) {
                const double t = planet.state.slow().land_surface_temperature_K[equator];
                day_min = std::min(day_min, t);
                day_max = std::max(day_max, t);
            }
        }
        std::cout << "dead_rock imbalance=" << year.relative_imbalance()
                  << " mean_K=" << year.mean_surface_temperature_K
                  << " equator_day_min_K=" << day_min << " max_K=" << day_max
                  << " range_K=" << day_max - day_min << '\n';
        PLANETSIM_EXPECT(test, std::abs(year.relative_imbalance()) <= 1e-3);
        PLANETSIM_EXPECT(test, day_max - day_min > 20.0);
    }
    // Experiment B: aqua planet.
    {
        const auto surface = planetsim::surface_energy_parameters_for(PlanetPreset::aqua_planet);
        Planet planet(mesh, PlanetPreset::aqua_planet, surface);
        const auto year = planetsim::spin_up_surface_energy(
            planet.state, planet.parameters, surface, planet.fractions, spin_up_years, 4U);
        std::vector<double> minimum(mesh->cell_count(), 1e9);
        std::vector<double> maximum(mesh->cell_count(), 0.0);
        for (std::int64_t month = 0; month < 12; ++month) {
            const auto substep = planetsim::climate_substep(
                spin_up_years * planetsim::climate_substeps_per_year + month, planet.parameters);
            planetsim::update_substep_mean_insolation(planet.state, planet.parameters, substep,
                                                      4U);
            static_cast<void>(planetsim::step_surface_energy(
                planet.state, planet.parameters, surface, planet.fractions,
                planet.state.forcing().substep_mean_insolation_W_m2,
                planetsim::simulation_time_s(substep.length_ticks()), 4U));
            for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
                const double t = planet.state.slow().ocean_mixed_layer_temperature_K[cell];
                minimum[cell] = std::min(minimum[cell], t);
                maximum[cell] = std::max(maximum[cell], t);
            }
        }
        double largest_range = 0.0;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            largest_range = std::max(largest_range, maximum[cell] - minimum[cell]);
        }
        std::cout << "aqua_planet imbalance=" << year.relative_imbalance()
                  << " storage_rate=" << year.relative_storage_rate()
                  << " ice_kg=" << year.ice_kg << " mean_K=" << year.mean_surface_temperature_K
                  << " largest_seasonal_range_K=" << largest_range << '\n';
        // Sea ice keeps thickening on this planet without transport or greenhouse
        // (ADR-0008 §9), so the balance that settles is the columns' storage.
        PLANETSIM_EXPECT(test, std::abs(year.relative_storage_rate()) <= 1e-3);
        PLANETSIM_EXPECT(test, largest_range < 20.0);
    }
}

// ADR-0007 §10: the ocean tile keeps the solver's double result, so thirty
// days of ten-minute reference steps on the mesh equal the same column run
// entirely in double. A float32 mixed layer drifted 0.01-0.04 K here.
void check_reference_ocean_precision(planetsim::test::Context& test,
                                     const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    // The column alone: no horizontal transport and no shared air
    // (ADR-0009), which would couple the columns this test compares.
    auto surface = planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    surface.transport_coefficient_W_m2_K = 0.0;
    surface.air_exchange_W_m2_K = 0.0;
    Planet planet(mesh, PlanetPreset::earth_like, surface);
    const std::size_t cells = mesh->cell_count();
    // Constant forcing, 3 W/m2 of absorbed shortwave above the initial
    // equilibrium's, so every ocean column warms slowly (~1e-5 K per step).
    const auto ocean = planetsim::column_properties(planetsim::SurfaceMaterial::ocean,
                                                    planet.parameters);
    planetsim::Field2D<float> insolation(cells, 0.0F);
    std::vector<planetsim::ColumnState> columns(cells);
    std::vector<double> initial_mixed_K(cells, 0.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const double T = planet.state.slow().ocean_mixed_layer_temperature_K[cell];
        const double emitted = (1.0 - 0.5 * planet.surface.grey_emissivity) * ocean.emissivity *
                               planetsim::stefan_boltzmann_W_m2_K4 * T * T * T * T;
        insolation[cell] = static_cast<float>((emitted + 3.0) / (1.0 - ocean.albedo));
        columns[cell] = {T, planet.state.slow().ocean_deep_temperature_K[cell]};
        initial_mixed_K[cell] = T;
    }
    const double dt_s = planetsim::simulation_time_s(10);
    const int steps = 30 * 24 * 6;
    for (int step = 0; step < steps; ++step) {
        static_cast<void>(planetsim::step_surface_energy(planet.state, planet.parameters,
                                                         planet.surface, planet.fractions,
                                                         insolation, dt_s, 4U));
        for (std::size_t cell = 0; cell < cells; ++cell) {
            columns[cell] = planetsim::step_column(ocean, columns[cell], insolation[cell],
                                                   planet.surface.grey_emissivity, dt_s)
                                .state;
        }
    }
    // Columns that start below the seawater freezing point freeze (ADR-0008)
    // and leave the column comparison.
    bool identical = true;
    double largest_warming_K = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (!(initial_mixed_K[cell] > planetsim::seawater_freezing_point_K)) {
            continue;
        }
        const double stored = planet.state.slow().ocean_mixed_layer_temperature_K[cell];
        identical = identical && std::bit_cast<std::uint64_t>(stored) ==
                                     std::bit_cast<std::uint64_t>(columns[cell].surface_K);
        largest_warming_K = std::max(
            largest_warming_K,
            stored - static_cast<double>(planet.state.slow().ocean_deep_temperature_K[cell]));
    }
    std::cout << "reference_ocean_30d mixed_minus_deep_max_K=" << largest_warming_K << '\n';
    PLANETSIM_EXPECT(test, identical);
    PLANETSIM_EXPECT(test, largest_warming_K > 0.0);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = mesh_at(test_level);
    check_closure_and_workers(test, mesh);
    check_scheduler_chunking(test, mesh);
    check_thermal_inertia(test, mesh);
    check_experiments(test, mesh);
    check_reference_ocean_precision(test, mesh_at(2U));
    return test.result();
}
