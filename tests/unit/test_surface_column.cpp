#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/surface_materials.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

using planetsim::ColumnProperties;
using planetsim::ColumnState;
using planetsim::SurfaceMaterial;

constexpr std::array<SurfaceMaterial, 4> materials{SurfaceMaterial::rock, SurfaceMaterial::dry_soil,
                                                   SurfaceMaterial::wet_soil,
                                                   SurfaceMaterial::ocean};

[[nodiscard]] ColumnProperties column(SurfaceMaterial material) {
    return planetsim::column_properties(material,
                                        planetsim::PlanetParameters::earth_development());
}

// The derived Earth layers of task M3-02 §1.1.
void check_derived_layers(planetsim::test::Context& test) {
    const auto rock = column(SurfaceMaterial::rock);
    PLANETSIM_EXPECT_NEAR(test, rock.surface_heat_capacity_J_m2_K, 3.889e5, 1e-3 * 3.889e5);
    PLANETSIM_EXPECT_NEAR(test, rock.lower_heat_capacity_J_m2_K, 7.433e6, 1e-3 * 7.433e6);
    PLANETSIM_EXPECT_NEAR(test, rock.exchange_W_m2_K, 1.406, 1e-3 * 1.406);
    const auto dry = column(SurfaceMaterial::dry_soil);
    PLANETSIM_EXPECT_NEAR(test, dry.surface_heat_capacity_J_m2_K, 1.036e5, 1e-3 * 1.036e5);
    const auto ocean = column(SurfaceMaterial::ocean);
    PLANETSIM_EXPECT(test, ocean.surface_heat_capacity_J_m2_K == 2.9e8);
    PLANETSIM_EXPECT(test, ocean.lower_heat_capacity_J_m2_K == 2.5e9);
    PLANETSIM_EXPECT(test, ocean.exchange_W_m2_K == 0.7);
}

// V1 stability and V2 energy closure over materials, forcing, steps and
// starting temperatures, including steps far beyond any explicit limit. The
// storage change C·(T' − T) cannot be computed more accurately than the
// rounding of that difference, 4ε times the stored energy C·T: a ten-minute
// ocean step changes 150 K by 5e-5 K, so the floor there is about 6e-10 of the
// step's flux. The gate is 1e-9 of the flux scale plus that floor
// (ADR-0007 §9).
void check_stability_and_closure(planetsim::test::Context& test) {
    bool stable = true;
    double worst_closure = 0.0;
    for (const auto material : materials) {
        const auto properties = column(material);
        for (const double insolation : {0.0, 340.0, 1'400.0}) {
            for (const double dt : {600.0, 2.63e6, 3.156e7, 1.0e12}) {
                for (const double start : {150.0, 288.0, 350.0}) {
                    for (const double grey : {0.0, 0.39, 0.9}) {
                        const auto result = planetsim::step_column(
                            properties, {start, start - 5.0}, insolation, grey, dt);
                        stable = stable && std::isfinite(result.state.surface_K) &&
                                 std::isfinite(result.state.lower_K) &&
                                 result.state.surface_K > 0.0 && result.state.lower_K > 0.0;
                        const double flux_energy =
                            dt * (result.absorbed_W_m2 - result.emitted_W_m2);
                        const double scale =
                            dt * (result.absorbed_W_m2 + result.emitted_W_m2) +
                            std::abs(result.storage_change_J_m2);
                        const double rounding_floor =
                            4.0 * std::numeric_limits<double>::epsilon() *
                            (properties.surface_heat_capacity_J_m2_K * result.state.surface_K +
                             properties.lower_heat_capacity_J_m2_K * result.state.lower_K);
                        worst_closure = std::max(
                            worst_closure,
                            std::abs(result.storage_change_J_m2 - flux_energy) /
                                (1e-9 * scale + rounding_floor));
                    }
                }
            }
        }
    }
    PLANETSIM_EXPECT(test, stable);
    PLANETSIM_EXPECT(test, worst_closure <= 1.0);
}

// V3 signs.
void check_signs(planetsim::test::Context& test) {
    const double dt = 2.63e6;
    const ColumnState start{288.0, 288.0};
    for (const auto material : materials) {
        const auto base = column(material);
        const double reference =
            planetsim::step_column(base, start, 340.0, 0.39, dt).state.surface_K;
        PLANETSIM_EXPECT(test, planetsim::step_column(base, start, 350.0, 0.39, dt).state.surface_K >
                                   reference);
        auto brighter = base;
        brighter.albedo += 0.05;
        PLANETSIM_EXPECT(test,
                         planetsim::step_column(brighter, start, 340.0, 0.39, dt).state.surface_K <
                             reference);
        auto emissive = base;
        emissive.emissivity = std::min(1.0, emissive.emissivity + 0.02);
        PLANETSIM_EXPECT(test,
                         planetsim::step_column(emissive, start, 340.0, 0.39, dt).state.surface_K <
                             reference);
        PLANETSIM_EXPECT(test, planetsim::step_column(base, start, 340.0, 0.2, dt).state.surface_K <
                                   reference);
    }
}

// V4: one step with an effectively infinite Δt is the steady state, which
// must equal the closed form in both layers.
void check_equilibrium(planetsim::test::Context& test) {
    double worst = 0.0;
    for (const auto material : materials) {
        const auto properties = column(material);
        for (const double insolation : {50.0, 340.0, 1'000.0}) {
            for (const double grey : {0.0, 0.39}) {
                const double expected =
                    planetsim::column_equilibrium_temperature_K(properties, insolation, grey);
                const auto result =
                    planetsim::step_column(properties, {200.0, 250.0}, insolation, grey, 1.0e20);
                worst = std::max({worst, std::abs(result.state.surface_K - expected),
                                  std::abs(result.state.lower_K - expected)});
            }
        }
    }
    PLANETSIM_EXPECT(test, worst <= 1e-6);
    // A monthly-stepped land column under constant forcing converges to it too.
    const auto rock = column(SurfaceMaterial::rock);
    ColumnState state{250.0, 250.0};
    for (int step = 0; step < 600; ++step) {
        state = planetsim::step_column(rock, state, 340.0, 0.0, 2.63e6).state;
    }
    PLANETSIM_EXPECT_NEAR(test, state.surface_K,
                          planetsim::column_equilibrium_temperature_K(rock, 340.0, 0.0), 1e-6);
}

void check_validation(planetsim::test::Context& test) {
    const auto rock = column(SurfaceMaterial::rock);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_column(rock, {288.0, 288.0}, 340.0, 1.0, 600.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_column(rock, {288.0, 288.0}, 340.0, 0.0, 0.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_column(rock, {0.0, 288.0}, 340.0, 0.0, 600.0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::step_column(rock, {288.0, 288.0}, -1.0, 0.0, 600.0));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_derived_layers(test);
    check_stability_and_closure(test);
    check_signs(test);
    check_equilibrium(test);
    check_validation(test);
    return test.result();
}
