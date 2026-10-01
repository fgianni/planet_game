#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using planetsim::AtmosphereParameters;

// ADR-0010 V1: the closed-form surface pressure against a numerical
// integration of dp/dz = −g p / (R_d T(z)) with T(z) = T₀ − Γ_c z from sea
// level, for a range of heights and temperatures.
void check_hydrostatic_surface_pressure(planetsim::test::Context& test) {
    const AtmosphereParameters parameters;
    const double gravity =
        planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
    PLANETSIM_EXPECT_NEAR(test, gravity, 9.8203, 1e-4);
    double worst = 0.0;
    for (const double height_m : {-400.0, 0.0, 1'500.0, 4'800.0}) {
        for (const double surface_K : {230.0, 288.0, 305.0}) {
            const double closed = planetsim::hydrostatic_surface_pressure_Pa(parameters, gravity,
                                                                              height_m, surface_K);
            // RK4 in z on ln p.
            const double sea_level_K = surface_K + parameters.critical_lapse_rate_K_m * height_m;
            const auto slope = [&](double z) {
                return -gravity / (planetsim::dry_air_gas_constant_J_kg_K *
                                   (sea_level_K - parameters.critical_lapse_rate_K_m * z));
            };
            const int steps = 20'000;
            const double dz = height_m / steps;
            double log_p = std::log(parameters.reference_pressure_Pa);
            for (int step = 0; step < steps; ++step) {
                const double z = step * dz;
                log_p += dz / 6.0 *
                         (slope(z) + 4.0 * slope(z + 0.5 * dz) + slope(z + dz));
            }
            worst = std::max(worst, std::abs(closed - std::exp(log_p)) / closed);
            // The inverse reduction returns p₀.
            const double back = planetsim::sea_level_pressure_Pa(parameters, gravity, closed,
                                                                 height_m, surface_K);
            worst = std::max(worst, std::abs(back - parameters.reference_pressure_Pa) /
                                        parameters.reference_pressure_Pa);
        }
    }
    std::cout << "hydrostatic_worst_relative=" << worst << '\n';
    PLANETSIM_EXPECT(test, worst <= 1e-12);
}

// ADR-0010 V1: layer heights by the hypsometric equation. An isothermal
// column is exact, z = z_s + (R_d T / g) ln(1/σ); a column on the Γ_c
// profile agrees with z = z_s + (T_s / Γ_c)(1 − σ^κ_c) to the trapezoidal
// rule's discretisation error.
void check_layer_heights(planetsim::test::Context& test) {
    const AtmosphereParameters parameters;
    const double gravity =
        planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
    const double kappa = planetsim::critical_lapse_exponent(parameters, gravity);
    double isothermal_worst = 0.0;
    double profile_worst = 0.0;
    for (const std::size_t layers : {1U, 3U, 5U, 8U}) {
        std::vector<double> temperature(layers, 250.0);
        std::vector<double> height(layers);
        planetsim::layer_heights_m(temperature, 250.0, 1'000.0, gravity, height);
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const double exact =
                1'000.0 + planetsim::dry_air_gas_constant_J_kg_K * 250.0 / gravity *
                              std::log(1.0 / planetsim::layer_sigma(layer, layers));
            isothermal_worst = std::max(isothermal_worst, std::abs(height[layer] - exact) / exact);
        }
        for (std::size_t layer = 0; layer < layers; ++layer) {
            temperature[layer] =
                288.0 * std::pow(planetsim::layer_sigma(layer, layers), kappa);
        }
        planetsim::layer_heights_m(temperature, 288.0, 0.0, gravity, height);
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const double exact =
                288.0 / parameters.critical_lapse_rate_K_m *
                (1.0 - std::pow(planetsim::layer_sigma(layer, layers), kappa));
            profile_worst = std::max(profile_worst, std::abs(height[layer] - exact) / exact);
        }
    }
    std::cout << "layer_heights isothermal_worst_relative=" << isothermal_worst
              << " profile_worst_relative=" << profile_worst << '\n';
    PLANETSIM_EXPECT(test, isothermal_worst <= 1e-12);
    PLANETSIM_EXPECT(test, profile_worst <= 0.005);
}

void check_parameters(planetsim::test::Context& test) {
    PLANETSIM_EXPECT(test, planetsim::atmosphere_parameters_for(
                               planetsim::PlanetPreset::earth_like).layer_count == 3U);
    PLANETSIM_EXPECT(test, planetsim::atmosphere_parameters_for(
                               planetsim::PlanetPreset::dead_rock).layer_count == 0U);
    PLANETSIM_EXPECT(test, planetsim::atmosphere_parameters_for(
                               planetsim::PlanetPreset::aqua_planet).layer_count == 0U);
    AtmosphereParameters too_many;
    too_many.layer_count = planetsim::max_atmosphere_layer_count + 1U;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::validate_atmosphere_parameters(too_many));
    AtmosphereParameters flat;
    flat.critical_lapse_rate_K_m = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::validate_atmosphere_parameters(flat));
}

// ADR-0010 §4.3 on a generated Earth-like planet: every column reduces to
// p₀ at sea level, the mass is Σ A p_s / g, high terrain has low pressure,
// the layers cool upwards with the bottom one extrapolating to the surface,
// and the diagnostics are identical for any worker count.
void check_initialised_planet(planetsim::test::Context& test) {
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(4U, 6'371'000.0));
    const auto planet = planetsim::PlanetParameters::earth_development();
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, 1U, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like), 4U));
    planetsim::initialise_surface_temperatures(
        *mesh, state.slow(), planet,
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like), 4U);
    planetsim::initialise_cryosphere(*mesh, state.slow());
    const double gravity = planetsim::surface_gravity_m_s2(planet);
    const auto fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                                state.slow().sea_level_m);
    planetsim::Field2D<double> height_m;
    planetsim::compute_surface_height(*mesh, state.slow(), fractions, height_m);

    for (const std::uint32_t layers : {0U, 3U, 5U}) {
        AtmosphereParameters parameters;
        parameters.layer_count = layers;
        planetsim::initialise_atmosphere(*mesh, state.slow(), planet, parameters);
        const auto& slow = state.slow();
        PLANETSIM_EXPECT(test, slow.atmosphere_layer_count() == layers);
        const auto diagnostics =
            planetsim::diagnose_atmosphere(*mesh, slow, planet, parameters, 1U);
        if (layers == 0U) {
            bool empty = slow.atmosphere_temperature_K.size() == 0U;
            for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
                empty = empty && slow.atmosphere_surface_pressure_Pa[cell] == 0.0;
            }
            PLANETSIM_EXPECT(test, empty);
            PLANETSIM_EXPECT(test, diagnostics.mass_kg == 0.0);
            continue;
        }
        const double kappa = planetsim::critical_lapse_exponent(parameters, gravity);
        double mass_kg = 0.0;
        double worst_reduction = 0.0;
        double worst_air = 0.0;
        bool cooling = true;
        std::size_t highest = 0;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            const double p_s = slow.atmosphere_surface_pressure_Pa[cell];
            mass_kg += mesh->cells()[cell].area_m2 * p_s / gravity;
            const double air_K = planetsim::surface_air_temperature_K(
                slow.atmosphere_temperature_K.layer(0)[cell], layers, kappa);
            const double surface_K =
                planetsim::tile_mean_surface_temperature_K(slow, fractions, cell);
            worst_air = std::max(worst_air, std::abs(air_K - surface_K) / surface_K);
            const double reduced = planetsim::sea_level_pressure_Pa(parameters, gravity, p_s,
                                                                    height_m[cell], air_K);
            worst_reduction = std::max(worst_reduction,
                                       std::abs(reduced - parameters.reference_pressure_Pa) /
                                           parameters.reference_pressure_Pa);
            for (std::size_t layer = 1; layer < layers; ++layer) {
                cooling = cooling && slow.atmosphere_temperature_K.layer(layer)[cell] <=
                                         slow.atmosphere_temperature_K.layer(layer - 1U)[cell];
            }
            if (height_m[cell] > height_m[highest]) {
                highest = cell;
            }
        }
        std::cout << "atmosphere layers=" << layers << " mass_kg=" << diagnostics.mass_kg
                  << " mean_p_s=" << diagnostics.mean_surface_pressure_Pa
                  << " min_p_s=" << diagnostics.min_surface_pressure_Pa
                  << " highest_m=" << height_m[highest]
                  << " top_layer_K=" << diagnostics.mean_layer_temperature_K.back()
                  << " top_layer_height_m=" << diagnostics.mean_layer_height_m.back() << '\n';
        PLANETSIM_EXPECT(test, worst_reduction <= 1e-12);
        PLANETSIM_EXPECT(test, worst_air <= 1e-12);
        PLANETSIM_EXPECT(test, cooling);
        PLANETSIM_EXPECT(test, std::abs(diagnostics.mass_kg - mass_kg) <= 1e-12 * mass_kg);
        // Land lifts the mean surface below p₀; Earth's is about 98.5 kPa.
        PLANETSIM_EXPECT(test, diagnostics.mean_surface_pressure_Pa < 101'325.0 &&
                                   diagnostics.mean_surface_pressure_Pa > 95'000.0);
        PLANETSIM_EXPECT(test, diagnostics.min_surface_pressure_Pa ==
                                   slow.atmosphere_surface_pressure_Pa[highest]);
        PLANETSIM_EXPECT(test, diagnostics.layer_count == layers);

        const auto parallel = planetsim::diagnose_atmosphere(*mesh, slow, planet, parameters, 8U);
        PLANETSIM_EXPECT(test, std::bit_cast<std::uint64_t>(parallel.mass_kg) ==
                                   std::bit_cast<std::uint64_t>(diagnostics.mass_kg));
        PLANETSIM_EXPECT(test, parallel.mean_layer_height_m == diagnostics.mean_layer_height_m);
    }

    // The diagnostics refuse parameters that disagree with the state.
    AtmosphereParameters other;
    other.layer_count = 3U;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::diagnose_atmosphere(*mesh, state.slow(), planet, other));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_parameters(test);
    check_hydrostatic_surface_pressure(test);
    check_layer_heights(test);
    check_initialised_planet(test);
    return test.result();
}
