#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/land_snow.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/surface/surface_materials.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using planetsim::PlanetPreset;

constexpr std::uint64_t test_seed = 20'260'930U;
constexpr double test_coefficient_W_m2_K = 0.5;

[[nodiscard]] std::shared_ptr<const planetsim::PlanetMesh> mesh_at(std::uint32_t level) {
    return std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, 6'371'000.0));
}

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface;
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh, PlanetPreset preset,
           double coefficient)
        : mesh(std::move(shared_mesh)),
          surface(planetsim::surface_energy_parameters_for(preset)),
          state(mesh) {
        surface.transport_coefficient_W_m2_K = coefficient;
        static_cast<void>(planetsim::generate_terrain(
            state, test_seed, planetsim::geology_parameters_for(preset), 4U));
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

[[nodiscard]] bool same_temperatures(const planetsim::SlowState& first,
                                     const planetsim::SlowState& second) {
    return bit_identical(first.land_surface_temperature_K, second.land_surface_temperature_K) &&
           bit_identical(first.land_ground_temperature_K, second.land_ground_temperature_K) &&
           bit_identical(first.ocean_mixed_layer_temperature_K,
                         second.ocean_mixed_layer_temperature_K) &&
           bit_identical(first.ocean_deep_temperature_K, second.ocean_deep_temperature_K);
}

// V1, V2, V3 and V7 on the Earth-like planet, a year of climate steps and a
// day of reference steps.
void check_budgets(planetsim::test::Context& test,
                   const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet planet(mesh, PlanetPreset::earth_like, test_coefficient_W_m2_K);
    double worst_conservation = 0.0;
    double worst_closure = 0.0;
    double worst_consistency = 0.0;
    double largest_dissipation = -1e300;
    int most_newton = 0;
    const auto record = [&](const planetsim::SurfaceEnergyDiagnostics& step) {
        worst_conservation =
            std::max(worst_conservation,
                     std::abs(step.transport_cell_sum_W) / step.transport_absolute_W);
        worst_closure = std::max(worst_closure, step.closure_residual_J() / step.closure_gate_J());
        worst_consistency = std::max(worst_consistency, step.transport_consistency_W_m2);
        largest_dissipation = std::max(largest_dissipation, step.transport_dissipation_W_K);
        most_newton = std::max(most_newton, step.transport_newton_iterations);
    };
    for (std::int64_t index = 0; index < planetsim::climate_substeps_per_year; ++index) {
        record(planet.climate_step(index, 4U));
    }
    const auto begin =
        planetsim::climate_substep(planetsim::climate_substeps_per_year, planet.parameters)
            .begin_tick;
    for (std::int64_t step = 0; step < 144; ++step) {
        record(planet.reference_step(begin + 10 * step, 4U));
    }
    std::cout << "budgets conservation=" << worst_conservation << " closure=" << worst_closure
              << " consistency_W_m2=" << worst_consistency
              << " dissipation_W_K=" << largest_dissipation << " newton=" << most_newton << '\n';
    PLANETSIM_EXPECT(test, worst_conservation <= 1e-13);
    PLANETSIM_EXPECT(test, worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, largest_dissipation <= 0.0);
    PLANETSIM_EXPECT(test, worst_consistency <= 1e-6);
}

// V4: the steady response of an ocean planet (g = 0) to a small P2 anomaly
// of uniform insolation, with and without transport, against the linear
// energy-balance result λ / (λ + 6 D), λ = 4 ε σ T₀³ (North, 1975).
void check_p2_response(planetsim::test::Context& test) {
    constexpr double mean_insolation = 340.0;
    constexpr double anomaly = 0.02;
    double coarse_error = 0.0;
    double error = 0.0;
    for (const std::uint32_t level : {3U, 4U, 5U}) {
        const auto mesh = mesh_at(level);
        const auto amplitude = [&](double coefficient) {
            Planet planet(mesh, PlanetPreset::aqua_planet, coefficient);
            planetsim::Field2D<float> insolation(mesh->cell_count(), 0.0F);
            std::vector<double> p2(mesh->cell_count());
            for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
                const double mu =
                    std::sin(planetsim::latitude_rad(mesh->cells()[cell].center_unit));
                p2[cell] = 0.5 * (3.0 * mu * mu - 1.0);
                insolation[cell] =
                    static_cast<float>(mean_insolation * (1.0 + anomaly * p2[cell]));
            }
            // One effectively infinite step is the steady state.
            static_cast<void>(planetsim::step_surface_energy(
                planet.state, planet.parameters, planet.surface, planet.fractions, insolation,
                1.0e20, 4U));
            double projection = 0.0;
            double norm = 0.0;
            for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
                const double area = mesh->cells()[cell].area_m2;
                projection +=
                    area * planet.state.slow().ocean_mixed_layer_temperature_K[cell] * p2[cell];
                norm += area * p2[cell] * p2[cell];
            }
            return projection / norm;
        };
        const auto ocean = planetsim::column_properties(
            planetsim::SurfaceMaterial::ocean, planetsim::PlanetParameters::earth_development());
        const double t0 =
            planetsim::column_equilibrium_temperature_K(ocean, mean_insolation, 0.0);
        const double lambda =
            4.0 * ocean.emissivity * planetsim::stefan_boltzmann_W_m2_K4 * t0 * t0 * t0;
        const double expected = lambda / (lambda + 6.0 * test_coefficient_W_m2_K);
        const double ratio = amplitude(test_coefficient_W_m2_K) / amplitude(0.0);
        error = std::abs(ratio - expected) / expected;
        if (level == 3U) {
            coarse_error = error;
        }
        std::cout << "p2 level=" << level << " ratio=" << ratio << " expected=" << expected
                  << " relative_error=" << error << '\n';
    }
    PLANETSIM_EXPECT(test, error <= 0.02);
    PLANETSIM_EXPECT(test, error < coarse_error);
}

// V5: at ten times the test coefficient, a random grid-scale perturbation of
// an aqua planet under uniform insolation never grows, on monthly and
// ten-minute steps.
void check_stability(planetsim::test::Context& test) {
    const auto mesh = mesh_at(4U);
    bool never_grows = true;
    for (const double dt : {2.63e6, 600.0}) {
        Planet planet(mesh, PlanetPreset::aqua_planet, 10.0 * test_coefficient_W_m2_K);
        const planetsim::Field2D<float> insolation(mesh->cell_count(), 340.0F);
        auto& mixed = planet.state.slow().ocean_mixed_layer_temperature_K;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            mixed[cell] = 280.0 + static_cast<double>((cell * 2'654'435'761U) % 1'000U) / 100.0;
            planet.state.slow().ocean_deep_temperature_K[cell] = 280.0;
        }
        const auto spread = [&]() {
            const auto [low, high] =
                std::minmax_element(mixed.values().begin(), mixed.values().end());
            return *high - *low;
        };
        double previous = spread();
        for (int step = 0; step < 24; ++step) {
            static_cast<void>(planetsim::step_surface_energy(planet.state, planet.parameters,
                                                             planet.surface, planet.fractions,
                                                             insolation, dt, 4U));
            const double current = spread();
            never_grows = never_grows && current <= previous * (1.0 + 1e-12);
            previous = current;
        }
        std::cout << "stability dt=" << dt << " final_spread_K=" << previous << '\n';
    }
    PLANETSIM_EXPECT(test, never_grows);
}

// V6: without transport and without shared air, the mesh step is the
// independent per-tile ADR-0007/0008 step, bit for bit.
void check_off_is_unchanged(planetsim::test::Context& test,
                            const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    Planet planet(mesh, PlanetPreset::earth_like, 0.0);
    planet.surface.air_exchange_W_m2_K = 0.0;
    auto expected = planet.state.slow();
    const auto substep = planetsim::climate_substep(0, planet.parameters);
    planetsim::update_substep_mean_insolation(planet.state, planet.parameters, substep, 4U);
    const double dt = planetsim::simulation_time_s(substep.length_ticks());
    const auto& insolation = planet.state.forcing().substep_mean_insolation_W_m2;
    const auto land = planetsim::column_properties(planet.surface.land_material, planet.parameters);
    const auto ocean =
        planetsim::column_properties(planetsim::SurfaceMaterial::ocean, planet.parameters);
    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
        const auto land_step = planetsim::step_land_tile(
            land,
            {expected.land_surface_temperature_K[cell], expected.land_ground_temperature_K[cell]},
            0.0, insolation[cell], 0.0, planet.surface.grey_emissivity, dt);
        const auto ocean_step = planetsim::step_column(
            ocean,
            {expected.ocean_mixed_layer_temperature_K[cell],
             expected.ocean_deep_temperature_K[cell]},
            insolation[cell], planet.surface.grey_emissivity, dt);
        expected.land_surface_temperature_K[cell] =
            static_cast<float>(land_step.column.state.surface_K);
        expected.land_ground_temperature_K[cell] =
            static_cast<float>(land_step.column.state.lower_K);
        expected.ocean_mixed_layer_temperature_K[cell] = ocean_step.state.surface_K;
        expected.ocean_deep_temperature_K[cell] = ocean_step.state.lower_K;
    }
    static_cast<void>(planetsim::step_surface_energy(planet.state, planet.parameters,
                                                     planet.surface, planet.fractions, insolation,
                                                     dt, 4U));
    PLANETSIM_EXPECT(test, same_temperatures(planet.state.slow(), expected));
}

// V8: 1, 2, 8 and 16 workers give the same state with transport on.
void check_workers(planetsim::test::Context& test,
                   const std::shared_ptr<const planetsim::PlanetMesh>& mesh) {
    std::vector<std::unique_ptr<Planet>> planets;
    for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
        auto planet = std::make_unique<Planet>(mesh, PlanetPreset::earth_like,
                                               test_coefficient_W_m2_K);
        for (std::int64_t index = 0; index < 4; ++index) {
            static_cast<void>(planet->climate_step(index, workers));
        }
        const auto begin = planetsim::climate_substep(4, planet->parameters).begin_tick;
        for (std::int64_t step = 0; step < 6; ++step) {
            static_cast<void>(planet->reference_step(begin + 10 * step, workers));
        }
        planets.push_back(std::move(planet));
    }
    bool identical = true;
    for (const auto& planet : planets) {
        identical = identical &&
                    same_temperatures(planets.front()->state.slow(), planet->state.slow());
    }
    PLANETSIM_EXPECT(test, identical);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = mesh_at(4U);
    check_budgets(test, mesh);
    check_p2_response(test);
    check_stability(test);
    check_off_is_unchanged(test, mesh);
    check_workers(test, mesh);
    return test.result();
}
