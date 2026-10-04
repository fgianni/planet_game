#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/cover_fractions.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/land_snow.hpp"
#include "sim/planet/surface/surface_materials.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

using planetsim::ColumnProperties;
using planetsim::SurfaceMaterial;

constexpr std::array<SurfaceMaterial, 3> land_materials{
    SurfaceMaterial::rock, SurfaceMaterial::dry_soil, SurfaceMaterial::wet_soil};

[[nodiscard]] ColumnProperties column(SurfaceMaterial material) {
    return planetsim::column_properties(material,
                                        planetsim::PlanetParameters::earth_development());
}

[[nodiscard]] bool same(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second);
}

// ADR-0008 V6: without snow, the land tile is the ADR-0007 column bit for
// bit, whether precipitation is zero or falls as rain on a warm surface.
void check_no_snow_is_column(planetsim::test::Context& test) {
    bool identical = true;
    for (const auto material : land_materials) {
        const auto ground = column(material);
        for (const double insolation : {0.0, 340.0, 1'200.0}) {
            for (const double dt : {600.0, 2.63e6}) {
                for (const double start : {250.0, 290.0}) {
                    const planetsim::ColumnState state{start, start - 3.0};
                    const auto expected =
                        planetsim::step_column(ground, state, insolation, 0.4964, dt);
                    const double precipitation = start > planetsim::melting_point_K ? 3e-5 : 0.0;
                    const auto result = planetsim::step_land_tile(ground, state, 0.0, insolation,
                                                                  precipitation, 0.4964, dt);
                    identical = identical &&
                                same(result.column.state.surface_K, expected.state.surface_K) &&
                                same(result.column.state.lower_K, expected.state.lower_K) &&
                                same(result.column.emitted_W_m2, expected.emitted_W_m2) &&
                                same(result.column.storage_change_J_m2,
                                     expected.storage_change_J_m2) &&
                                result.snow_kg_m2 == 0.0 && result.melt_kg_m2 == 0.0 &&
                                result.rain_kg_m2 == precipitation * dt;
                }
            }
        }
    }
    PLANETSIM_EXPECT(test, identical);
}

// Clamped melt against the closed form of the surface balance at T_m, with
// the ground layer eliminated (ADR-0007 §4.3):
//   L_f m / Δt = (1 − α) Q + C_s (T_s − T_m) / Δt − r T_m⁴ − k_eff (T_m − T_l)
void check_clamped_melt(planetsim::test::Context& test) {
    const auto ground = column(SurfaceMaterial::dry_soil);
    const double dt = 3'600.0;
    const double snow = 200.0;
    const double insolation = 1'300.0;   // enough to melt under α ≈ 0.73
    const double grey = 0.4964;
    const planetsim::ColumnState state{273.0, 273.0};
    const auto result = planetsim::step_land_tile(ground, state, snow, insolation, 0.0, grey, dt);

    const double albedo = planetsim::snow_covered_albedo(ground.albedo, snow);
    const double radiative =
        (1.0 - 0.5 * grey) * ground.emissivity * planetsim::stefan_boltzmann_W_m2_K4;
    const double lower_rate = ground.lower_heat_capacity_J_m2_K / dt;
    const double k_eff =
        ground.exchange_W_m2_K * lower_rate / (lower_rate + ground.exchange_W_m2_K);
    const double t_m = planetsim::melting_point_K;
    const double surplus = (1.0 - albedo) * insolation +
                           ground.surface_heat_capacity_J_m2_K * (state.surface_K - t_m) / dt -
                           radiative * std::pow(t_m, 4) - k_eff * (t_m - state.lower_K);
    const double expected_melt = surplus * dt / planetsim::latent_heat_of_fusion_J_kg;

    PLANETSIM_EXPECT(test, result.column.state.surface_K == t_m);
    PLANETSIM_EXPECT(test, result.albedo == albedo);
    PLANETSIM_EXPECT(test, expected_melt > 0.0 && expected_melt < snow);
    PLANETSIM_EXPECT_NEAR(test, result.melt_kg_m2, expected_melt, 1e-9 * expected_melt);
    PLANETSIM_EXPECT_NEAR(test, result.snow_kg_m2, snow - expected_melt, 1e-9 * snow);
    PLANETSIM_EXPECT(test, result.runoff_kg_m2() == result.melt_kg_m2);
}

// All the snow melts within the step; the rest of the surplus warms the
// column above the melting point.
void check_complete_melt(planetsim::test::Context& test) {
    const auto ground = column(SurfaceMaterial::rock);
    const double snow = 0.5;
    const auto result = planetsim::step_land_tile(ground, {273.0, 272.0}, snow, 1'000.0, 0.0,
                                                  0.4964, 86'400.0);
    PLANETSIM_EXPECT(test, result.snow_kg_m2 == 0.0);
    PLANETSIM_EXPECT(test, result.melt_kg_m2 == snow);
    PLANETSIM_EXPECT(test, result.column.state.surface_K > planetsim::melting_point_K);
    PLANETSIM_EXPECT(test, std::abs(result.column.newton_residual_W_m2) <= 1e-6);
}

// Precipitation is snow when the surface starts at or below T_m, rain above.
void check_precipitation_phase(planetsim::test::Context& test) {
    const auto ground = column(SurfaceMaterial::dry_soil);
    const double rate = 5e-5;
    const double dt = 86'400.0;
    const auto cold =
        planetsim::step_land_tile(ground, {260.0, 262.0}, 0.0, 50.0, rate, 0.4964, dt);
    PLANETSIM_EXPECT(test, cold.snowfall_kg_m2 == rate * dt);
    PLANETSIM_EXPECT(test, cold.rain_kg_m2 == 0.0);
    PLANETSIM_EXPECT(test, cold.snow_kg_m2 == rate * dt);
    PLANETSIM_EXPECT(test, cold.column.state.surface_K < planetsim::melting_point_K);
    const auto at_melting = planetsim::step_land_tile(
        ground, {planetsim::melting_point_K, 270.0}, 0.0, 50.0, rate, 0.4964, dt);
    PLANETSIM_EXPECT(test, at_melting.snowfall_kg_m2 == rate * dt);
    const auto warm =
        planetsim::step_land_tile(ground, {280.0, 279.0}, 30.0, 50.0, rate, 0.4964, dt);
    PLANETSIM_EXPECT(test, warm.snowfall_kg_m2 == 0.0);
    PLANETSIM_EXPECT(test, warm.rain_kg_m2 == rate * dt);
}

// V1 and V2 per tile, and the V4 invariant, over materials, forcing, steps,
// starting temperatures and snow. The energy gate is ADR-0007's (1e-9 of the
// flux scale plus the rounding floor of the stored energy); the water gate
// is 1e-12 of the stock plus its rounding floor.
void check_closure_and_invariant(planetsim::test::Context& test) {
    double worst_energy = 0.0;
    double worst_water = 0.0;
    bool invariant = true;
    for (const auto material : land_materials) {
        const auto ground = column(material);
        for (const double insolation : {0.0, 150.0, 400.0, 1'200.0}) {
            for (const double dt : {600.0, 86'400.0, 2.63e6}) {
                for (const double start : {230.0, 268.0, 273.15, 285.0}) {
                    for (const double snow : {0.0, 0.01, 5.0, 300.0}) {
                        for (const double rate : {0.0, 3e-5}) {
                            const auto result = planetsim::step_land_tile(
                                ground, {start, start - 2.0}, snow, insolation, rate, 0.4964, dt);
                            const auto& step = result.column;
                            const double flux_energy =
                                dt * (step.absorbed_W_m2 - step.emitted_W_m2);
                            const double scale = dt * (step.absorbed_W_m2 + step.emitted_W_m2) +
                                                 std::abs(step.storage_change_J_m2) +
                                                 result.latent_J_m2;
                            const double floor =
                                4.0 * std::numeric_limits<double>::epsilon() *
                                (ground.surface_heat_capacity_J_m2_K * step.state.surface_K +
                                 ground.lower_heat_capacity_J_m2_K * step.state.lower_K);
                            worst_energy = std::max(
                                worst_energy,
                                std::abs(step.storage_change_J_m2 + result.latent_J_m2 -
                                         flux_energy) /
                                    (1e-9 * scale + floor));
                            const double stock = snow + result.snowfall_kg_m2;
                            const double water_floor =
                                4.0 * std::numeric_limits<double>::epsilon() * stock;
                            worst_water = std::max(
                                worst_water,
                                std::abs(result.snow_kg_m2 - snow -
                                         (result.snowfall_kg_m2 - result.melt_kg_m2)) /
                                    (1e-12 * stock + water_floor +
                                     std::numeric_limits<double>::min()));
                            invariant = invariant && result.snow_kg_m2 >= 0.0 &&
                                        result.melt_kg_m2 >= 0.0 &&
                                        std::isfinite(step.state.surface_K) &&
                                        (result.snow_kg_m2 == 0.0 ||
                                         step.state.surface_K <= planetsim::melting_point_K);
                        }
                    }
                }
            }
        }
    }
    PLANETSIM_EXPECT(test, worst_energy <= 1.0);
    PLANETSIM_EXPECT(test, worst_water <= 1.0);
    PLANETSIM_EXPECT(test, invariant);
}

// ADR-0009: an external source enters the surface equation like absorbed
// shortwave. Energy closes with it, the slope matches a finite difference,
// and a tile held at the melting point by snow has slope 0 (the extra heat
// melts snow).
void check_source_and_slope(planetsim::test::Context& test) {
    const auto ocean = column(SurfaceMaterial::ocean);
    const auto soil = column(SurfaceMaterial::dry_soil);
    double worst_closure = 0.0;
    double worst_slope = 0.0;
    for (const double dt : {600.0, 2.63e6}) {
        for (const double source : {-80.0, 0.0, 150.0}) {
            const auto water =
                planetsim::step_column(ocean, {285.0, 280.0}, 300.0, 0.4964, dt, source);
            const auto land = planetsim::step_land_tile(soil, {285.0, 283.0}, 0.0, 300.0, 0.0,
                                                        0.4964, dt);
            const auto tile = planetsim::prepare_land_tile(soil, {285.0, 283.0}, 0.0, 300.0,
                                                           0.0, 0.4964, dt);
            const auto sourced = planetsim::solve_land_tile(tile, source);
            for (const auto& step : {water, sourced.column}) {
                const double flux = dt * (step.absorbed_W_m2 - step.emitted_W_m2 + source);
                worst_closure = std::max(
                    worst_closure, std::abs(step.storage_change_J_m2 - flux) /
                                       (1e-9 * (std::abs(flux) + dt * step.emitted_W_m2) +
                                        1e-6));
            }
            const double delta = 1e-3;
            const auto nudged = planetsim::solve_land_tile(tile, source + delta);
            const double finite_difference =
                (nudged.column.state.surface_K - sourced.column.state.surface_K) / delta;
            worst_slope = std::max(
                worst_slope, std::abs(finite_difference - sourced.column.surface_slope_K_m2_W) /
                                 sourced.column.surface_slope_K_m2_W);
            PLANETSIM_EXPECT(test, source != 0.0 || sourced.column.state.surface_K ==
                                                        land.column.state.surface_K);
        }
    }
    PLANETSIM_EXPECT(test, worst_closure <= 1.0);
    PLANETSIM_EXPECT(test, worst_slope <= 1e-4);

    const auto melting = planetsim::solve_land_tile(
        planetsim::prepare_land_tile(soil, {273.0, 273.0}, 200.0, 1'300.0, 0.0, 0.4964, 3'600.0),
        50.0);
    PLANETSIM_EXPECT(test, melting.column.state.surface_K == planetsim::melting_point_K);
    PLANETSIM_EXPECT(test, melting.column.surface_slope_K_m2_W == 0.0);
    const auto more = planetsim::solve_land_tile(
        planetsim::prepare_land_tile(soil, {273.0, 273.0}, 200.0, 1'300.0, 0.0, 0.4964, 3'600.0),
        60.0);
    PLANETSIM_EXPECT_NEAR(test, more.melt_kg_m2 - melting.melt_kg_m2,
                          10.0 * 3'600.0 / planetsim::latent_heat_of_fusion_J_kg, 1e-9);
}

void check_albedo(planetsim::test::Context& test) {
    PLANETSIM_EXPECT(test, planetsim::snow_cover_fraction(0.0) == 0.0);
    PLANETSIM_EXPECT_NEAR(test, planetsim::snow_cover_fraction(10.0), 0.5, 1e-15);
    PLANETSIM_EXPECT(test, planetsim::snow_covered_albedo(0.3, 0.0) == 0.3);
    PLANETSIM_EXPECT_NEAR(test, planetsim::snow_covered_albedo(0.3, 10.0), 0.525, 1e-15);
    PLANETSIM_EXPECT(test, planetsim::snow_covered_albedo(0.3, 1e6) < planetsim::snow_albedo);
    PLANETSIM_EXPECT(test, planetsim::snow_covered_albedo(0.3, 20.0) >
                               planetsim::snow_covered_albedo(0.3, 10.0));
}

void check_validation(planetsim::test::Context& test) {
    const auto ground = column(SurfaceMaterial::rock);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_land_tile(ground, {260.0, 260.0}, -1.0, 100.0, 0.0,
                                                      0.0, 600.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_land_tile(ground, {260.0, 260.0}, 1.0, 100.0, -1e-6,
                                                      0.0, 600.0));
    PLANETSIM_EXPECT_THROWS(
        test, std::invalid_argument,
        planetsim::step_land_tile(ground, {260.0, 260.0},
                                  std::numeric_limits<double>::infinity(), 100.0, 0.0, 0.0, 600.0));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_no_snow_is_column(test);
    check_clamped_melt(test);
    check_complete_melt(test);
    check_precipitation_phase(test);
    check_closure_and_invariant(test);
    check_source_and_slope(test);
    check_albedo(test);
    check_validation(test);
    return test.result();
}
