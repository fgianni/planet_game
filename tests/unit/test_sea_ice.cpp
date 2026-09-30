#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/sea_ice.hpp"
#include "sim/planet/surface/surface_materials.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using planetsim::ColumnProperties;

[[nodiscard]] ColumnProperties ocean() {
    return planetsim::column_properties(planetsim::SurfaceMaterial::ocean,
                                        planetsim::PlanetParameters::earth_development());
}

[[nodiscard]] bool same(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second);
}

constexpr double freezing_K = planetsim::seawater_freezing_point_K;

// ADR-0008 V6: open water that stays above T_f is the ADR-0007 column bit
// for bit, with or without a source and an exchange.
void check_open_water_is_column(planetsim::test::Context& test) {
    const auto column = ocean();
    bool identical = true;
    for (const double dt : {600.0, 2.63e6}) {
        for (const double source : {-20.0, 0.0, 40.0}) {
            for (const double exchange : {0.0, 10.0}) {
                const planetsim::ColumnState state{290.0, 285.0};
                const auto expected = planetsim::step_column(
                    column, state, 400.0, 0.4455, dt, source + exchange * 288.0);
                const auto tile =
                    planetsim::prepare_ocean_tile(column, state, 0.0, 400.0, 0.4455, dt);
                const auto result =
                    planetsim::solve_ocean_tile(tile, source + exchange * 288.0, exchange);
                const auto reference = planetsim::solve_column_step(
                    column, tile.open, state, source + exchange * 288.0, exchange);
                identical = identical && result.ice_kg_m2 == 0.0 && result.frozen_kg_m2 == 0.0 &&
                            same(result.column.state.surface_K, reference.state.surface_K) &&
                            same(result.column.state.lower_K, reference.state.lower_K) &&
                            same(result.column.emitted_W_m2, reference.emitted_W_m2) &&
                            same(result.radiating_K, reference.state.surface_K) &&
                            (exchange != 0.0 ||
                             same(result.column.state.surface_K, expected.state.surface_K));
            }
        }
    }
    PLANETSIM_EXPECT(test, identical);
}

// Freezing onset: water that would cool below T_f is held there and the
// deficit a T_f + r T_f⁴ − b freezes, from the closed form.
void check_freezing_onset(planetsim::test::Context& test) {
    const auto column = ocean();
    const double dt = 2.63e6;
    const planetsim::ColumnState state{272.0, 275.0};
    const auto tile = planetsim::prepare_ocean_tile(column, state, 0.0, 20.0, 0.4455, dt);
    const auto result = planetsim::solve_ocean_tile(tile);
    const double need = tile.open.a * freezing_K +
                        tile.open.radiative * std::pow(freezing_K, 4) - tile.open.b;
    PLANETSIM_EXPECT(test, need > 0.0);
    PLANETSIM_EXPECT(test, result.column.state.surface_K == freezing_K);
    PLANETSIM_EXPECT(test, result.radiating_K == freezing_K);
    PLANETSIM_EXPECT_NEAR(test, result.frozen_kg_m2,
                          need * dt / planetsim::latent_heat_of_fusion_J_kg,
                          1e-12 * result.frozen_kg_m2);
    PLANETSIM_EXPECT(test, result.ice_kg_m2 == result.frozen_kg_m2);
    PLANETSIM_EXPECT(test, result.column.surface_slope_K_m2_W == 0.0);
}

// ADR-0008 V3: Stefan growth. The surface is pinned near T_s by a very
// large exchange, there is no sunlight and no ocean heat flux, so each step
// solves ρ L (h' − h) / Δt = k ΔT / h' exactly; over a winter the discrete
// solution converges at first order to h² = h₀² + 2 k ΔT t / (ρ L).
void check_stefan(planetsim::test::Context& test) {
    auto column = ocean();
    column.exchange_W_m2_K = 0.0;   // no ocean heat flux
    constexpr double pinned_K = 253.15;
    constexpr double pin = 1.0e9;
    const double rho_l = planetsim::sea_ice_density_kg_m3 * planetsim::latent_heat_of_fusion_J_kg;
    const double k = planetsim::sea_ice_conductivity_W_m_K;
    const double winter_s = 180.0 * 86'400.0;
    const double h0 = 0.1;

    double worst_discrete = 0.0;
    double previous_error = 1.0;
    bool converging = true;
    double error_one_day = 0.0;
    for (const double dt : {30.0 * 86'400.0, 10.0 * 86'400.0, 86'400.0}) {
        double mass = h0 * planetsim::sea_ice_density_kg_m3;
        planetsim::ColumnState state{freezing_K, freezing_K};
        double delta_sum = 0.0;
        for (double t = 0.0; t < winter_s - 0.5 * dt; t += dt) {
            const auto tile = planetsim::prepare_ocean_tile(column, state, mass, 0.0, 0.0, dt);
            const auto result = planetsim::solve_ocean_tile(tile, pin * pinned_K, pin);
            const double h = mass / planetsim::sea_ice_density_kg_m3;
            const double delta = freezing_K - result.radiating_K;
            const double c = k * delta * dt / rho_l;
            const double expected_m = 0.5 * (h + std::sqrt(h * h + 4.0 * c));
            const double actual_m = result.ice_kg_m2 / planetsim::sea_ice_density_kg_m3;
            worst_discrete = std::max(worst_discrete, std::abs(actual_m - expected_m) / expected_m);
            mass = result.ice_kg_m2;
            state = result.column.state;
            delta_sum += delta * dt;
        }
        const double analytic = std::sqrt(h0 * h0 + 2.0 * k * delta_sum / rho_l);
        const double error =
            std::abs(mass / planetsim::sea_ice_density_kg_m3 - analytic) / analytic;
        std::cout << "stefan dt_days=" << dt / 86'400.0 << " h_m=" << mass / 917.0
                  << " analytic_m=" << analytic << " relative_error=" << error << '\n';
        converging = converging && error < previous_error;
        previous_error = error;
        error_one_day = error;
    }
    std::cout << "stefan worst_discrete=" << worst_discrete << '\n';
    PLANETSIM_EXPECT(test, worst_discrete <= 1e-9);
    PLANETSIM_EXPECT(test, converging);
    PLANETSIM_EXPECT(test, error_one_day <= 0.01);
}

// All the ice melts within the step; the rest warms the open water.
void check_complete_melt(planetsim::test::Context& test) {
    const auto column = ocean();
    const double ice = 5.0;
    const auto tile = planetsim::prepare_ocean_tile(column, {freezing_K, 278.0}, ice, 600.0,
                                                    0.4455, 30.0 * 86'400.0);
    const auto result = planetsim::solve_ocean_tile(tile);
    PLANETSIM_EXPECT(test, result.ice_kg_m2 == 0.0);
    PLANETSIM_EXPECT_NEAR(test, result.melted_kg_m2, ice, 1e-12 * ice);
    PLANETSIM_EXPECT(test, result.column.state.surface_K > freezing_K);
}

// V1, V2 and V4 per tile over a grid of cases.
void check_closure_and_invariants(planetsim::test::Context& test) {
    const auto column = ocean();
    double worst_energy = 0.0;
    double worst_water = 0.0;
    bool invariants = true;
    for (const double ice : {0.0, 1.0, 100.0, 1'500.0}) {
        for (const double insolation : {0.0, 200.0, 500.0}) {
            for (const double dt : {600.0, 86'400.0, 2.63e6}) {
                for (const double mixed : {freezing_K, 280.0}) {
                    for (const double source : {-50.0, 0.0, 100.0}) {
                        for (const double exchange : {0.0, 10.0}) {
                            const planetsim::ColumnState state{ice > 0.0 ? freezing_K : mixed,
                                                               mixed - 1.0};
                            const auto tile = planetsim::prepare_ocean_tile(column, state, ice,
                                                                            insolation, 0.4455, dt);
                            const double s = source + exchange * 265.0;
                            const auto result = planetsim::solve_ocean_tile(tile, s, exchange);
                            const auto& step = result.column;
                            const double flux =
                                dt * (step.absorbed_W_m2 - step.emitted_W_m2 + step.source_W_m2);
                            const double scale =
                                dt * (step.absorbed_W_m2 + step.emitted_W_m2 +
                                      std::abs(step.source_W_m2)) +
                                std::abs(step.storage_change_J_m2) + std::abs(result.latent_J_m2);
                            const double floor =
                                4.0 * std::numeric_limits<double>::epsilon() *
                                (column.surface_heat_capacity_J_m2_K * step.state.surface_K +
                                 column.lower_heat_capacity_J_m2_K * step.state.lower_K +
                                 planetsim::latent_heat_of_fusion_J_kg *
                                     std::max(ice, result.ice_kg_m2));
                            worst_energy = std::max(
                                worst_energy,
                                std::abs(step.storage_change_J_m2 + result.latent_J_m2 - flux) /
                                    (1e-9 * scale + floor));
                            const double stock = ice + result.frozen_kg_m2 + result.melted_kg_m2;
                            worst_water = std::max(
                                worst_water,
                                std::abs(result.ice_kg_m2 - ice -
                                         (result.frozen_kg_m2 - result.melted_kg_m2)) /
                                    (1e-12 * stock +
                                     4.0 * std::numeric_limits<double>::epsilon() * stock +
                                     std::numeric_limits<double>::min()));
                            const bool ok =
                                result.ice_kg_m2 >= 0.0 &&
                                result.frozen_kg_m2 >= 0.0 && result.melted_kg_m2 >= 0.0 &&
                                std::isfinite(result.radiating_K) &&
                                (result.ice_kg_m2 == 0.0 ||
                                 (step.state.surface_K == freezing_K &&
                                  result.radiating_K <= planetsim::melting_point_K)) &&
                                (result.ice_kg_m2 > 0.0 || step.state.surface_K >= freezing_K);
                            invariants = invariants && ok;
                        }
                    }
                }
            }
        }
    }
    std::cout << "closure worst_energy=" << worst_energy << " worst_water=" << worst_water << '\n';
    PLANETSIM_EXPECT(test, worst_energy <= 1.0);
    PLANETSIM_EXPECT(test, worst_water <= 1.0);
    PLANETSIM_EXPECT(test, invariants);
}

void check_albedo(planetsim::test::Context& test) {
    PLANETSIM_EXPECT(test, planetsim::sea_ice_covered_albedo(0.06, 0.0) == 0.06);
    PLANETSIM_EXPECT_NEAR(test, planetsim::sea_ice_covered_albedo(0.06, 0.25 * 917.0),
                          0.06 + 0.5 * (planetsim::sea_ice_albedo - 0.06), 1e-15);
    PLANETSIM_EXPECT(test, planetsim::sea_ice_covered_albedo(0.06, 2'000.0) ==
                               planetsim::sea_ice_albedo);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::prepare_ocean_tile(ocean(), {280.0, 280.0}, -1.0, 100.0,
                                                          0.0, 600.0));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_open_water_is_column(test);
    check_freezing_onset(test);
    check_stefan(test);
    check_complete_melt(test);
    check_closure_and_invariants(test);
    check_albedo(test);
    return test.result();
}
