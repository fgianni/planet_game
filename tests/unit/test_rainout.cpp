#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

// Saturation rainout in the atmospheric column (ADR-0021 §4.4, V4; task
// M7-03): single columns over analytic surfaces.
namespace {

using planetsim::ColumnMoisture;
using planetsim::ColumnRadiation;
using planetsim::LayerArray;
using planetsim::SurfaceExchange;

constexpr double month_s = 2.63e6;
constexpr double surface_pressure_Pa = 100'000.0;

// A surface held at `surface_K`: it emits as a black body, exchanges γ (T_s −
// A) with the air and evaporates E = τ (q_s − k (A − A_ref)) into it, which
// decreases as the air warms (a stand-in for the bulk formula's response).
struct HeldSurface {
    double surface_K = 290.0;
    double exchange_W_m2_K = 10.0;
    double evaporation_kg_m2_s = 0.0;
    double evaporation_slope_kg_m2_s_K = 0.0;   // dE/dA
    double reference_air_K = 285.0;

    SurfaceExchange operator()(double /*downward_W_m2*/, double air_K) const {
        SurfaceExchange x;
        const double t2 = surface_K * surface_K;
        x.upward_W_m2 = planetsim::column_stefan_boltzmann_W_m2_K4 * t2 * t2;
        x.sensible_W_m2 = exchange_W_m2_K * (surface_K - air_K);
        x.d_sensible_d_air = -exchange_W_m2_K;
        x.vapour_kg_m2_s =
            evaporation_kg_m2_s + evaporation_slope_kg_m2_s_K * (air_K - reference_air_K);
        x.d_vapour_d_air = evaporation_slope_kg_m2_s_K;
        return x;
    }
};

struct Column {
    planetsim::AtmosphereParameters parameters;
    ColumnRadiation radiation;
    LayerArray before{};
    ColumnMoisture moisture;

    explicit Column(std::size_t layers) {
        parameters.layer_count = static_cast<std::uint32_t>(layers);
        const double gravity =
            planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * static_cast<double>(layers));
        moisture.latent_J_kg = planetsim::latent_heat_vaporisation_J_kg;
        for (std::size_t k = 0; k < layers; ++k) {
            before[k] = 285.0 - 25.0 * static_cast<double>(k);
            moisture.pressure_Pa[k] = planetsim::layer_sigma(k, layers) * surface_pressure_Pa;
        }
    }

    // Each layer at `relative` of its saturation at the start.
    void humidity(double relative) {
        for (std::size_t k = 0; k < radiation.layers; ++k) {
            moisture.humidity_kg_kg[k] =
                relative * planetsim::saturation_specific_humidity(before[k],
                                                                   moisture.pressure_Pa[k]);
        }
    }

    planetsim::ColumnSolveResult solve(const HeldSurface& surface, LayerArray& after,
                                       bool moist = true, double source_W_m2 = 0.0) const {
        after = before;
        return planetsim::solve_atmosphere_column(
            radiation, std::span<const double>(before.data(), radiation.layers), month_s,
            source_W_m2, false, surface, std::span<double>(after.data(), radiation.layers), {},
            nullptr, moist ? &moisture : nullptr);
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. No condensate and no evaporation: the moist solve is the dry one
    // bit for bit.
    {
        Column column(3U);
        column.humidity(0.5);
        const HeldSurface surface;
        LayerArray dry{};
        LayerArray moist{};
        static_cast<void>(column.solve(surface, dry, false));
        const auto result = column.solve(surface, moist, true);
        bool same = true;
        for (std::size_t k = 0; k < 3U; ++k) {
            same = same && dry[k] == moist[k];
        }
        PLANETSIM_EXPECT(test, same && result.precipitation_kg_m2_s == 0.0);
    }

    // 2. V4: a supersaturated column rains to exactly saturation at its new
    // temperatures, keeps its water, and its latent heat warms it.
    {
        double worst_saturation = 0.0;
        double worst_water = 0.0;
        int worst_iterations = 0;
        bool converged = true;
        bool warmer = true;
        for (const std::size_t layers : {1U, 3U, 5U}) {
            for (const double relative : {0.9, 0.99, 1.0, 1.01, 1.2, 2.0}) {
                for (const double evaporation : {0.0, 2e-5, 1e-4}) {
                    Column column(layers);
                    column.humidity(relative);
                    HeldSurface surface;
                    surface.evaporation_kg_m2_s = evaporation;
                    surface.evaporation_slope_kg_m2_s_K = -0.05 * evaporation;
                    LayerArray dry{};
                    LayerArray after{};
                    static_cast<void>(column.solve(surface, dry, false));
                    const auto result = column.solve(surface, after, true);
                    converged = converged && result.converged;
                    worst_iterations = std::max(worst_iterations, result.iterations);
                    double water_before = 0.0;
                    double water_after = 0.0;
                    for (std::size_t k = 0; k < layers; ++k) {
                        const double saturated = planetsim::saturation_specific_humidity(
                            after[k], column.moisture.pressure_Pa[k]);
                        if (result.condensate_kg_kg[k] > 0.0) {
                            worst_saturation = std::max(
                                worst_saturation,
                                std::abs(result.humidity_kg_kg[k] / saturated - 1.0));
                        } else {
                            PLANETSIM_EXPECT(test, result.humidity_kg_kg[k] <=
                                                       saturated * (1.0 + 1e-12));
                        }
                        water_before += column.moisture.humidity_kg_kg[k];
                        water_after += result.humidity_kg_kg[k];
                    }
                    const double m = column.moisture.layer_mass_kg_m2;
                    const double in = m * water_before + result.evaporation_kg_m2_s * month_s;
                    const double out = m * water_after + result.precipitation_kg_m2_s * month_s;
                    worst_water = std::max(worst_water, std::abs(in - out) / in);
                    PLANETSIM_EXPECT(test, std::abs(result.condensation_W_m2 -
                                                    planetsim::latent_heat_vaporisation_J_kg *
                                                        result.precipitation_kg_m2_s) <=
                                               1e-12 * (result.condensation_W_m2 + 1.0));
                    if (result.precipitation_kg_m2_s > 0.0) {
                        warmer = warmer && after[0] + after[layers - 1U] > dry[0] + dry[layers - 1U];
                    }
                }
            }
        }
        std::cout << "rainout: saturation " << worst_saturation << ", water " << worst_water
                  << ", iterations " << worst_iterations << '\n';
        PLANETSIM_EXPECT(test, converged);
        PLANETSIM_EXPECT(test, worst_saturation <= 1e-9);
        PLANETSIM_EXPECT(test, worst_water <= 1e-13);
        PLANETSIM_EXPECT(test, warmer);
    }

    // 3. V4 energy: the column's storage is the longwave it keeps, the
    // sensible heat and the latent heat of its rain,
    // Σ C (T − T⁰)/Δt = U_s − D_s − OLR + H + L P.
    {
        double worst = 0.0;
        for (const double relative : {1.0, 1.5, 3.0}) {
            Column column(3U);
            column.humidity(relative);
            HeldSurface surface;
            surface.evaporation_kg_m2_s = 5e-5;
            surface.evaporation_slope_kg_m2_s_K = -2e-6;
            LayerArray after{};
            const auto result = column.solve(surface, after, true);
            double storage = 0.0;
            for (std::size_t k = 0; k < 3U; ++k) {
                storage += column.radiation.layer_heat_capacity_J_m2_K * (after[k] - column.before[k]) /
                           month_s;
            }
            const double in = result.upward_surface_W_m2 - result.downward_surface_W_m2 -
                              result.outgoing_W_m2 + result.sensible_W_m2 +
                              result.condensation_W_m2;
            const double scale = result.upward_surface_W_m2 + result.downward_surface_W_m2 +
                                 result.outgoing_W_m2 + std::abs(result.sensible_W_m2) +
                                 result.condensation_W_m2;
            worst = std::max(worst, std::abs(storage - in) / scale);
            PLANETSIM_EXPECT(test, result.converged && result.condensation_W_m2 > 0.0);
        }
        std::cout << "rainout energy: worst " << worst << '\n';
        PLANETSIM_EXPECT(test, worst <= 1e-12);
    }
    // 4. V4: evaporation into a dry column, monthly steps with convection: the
    // column moistens, saturates and then rains out what evaporates, never
    // above saturation.
    {
        Column column(3U);
        column.humidity(0.1);
        HeldSurface surface;
        surface.evaporation_kg_m2_s = 3e-5;   // about 75 W/m² of latent heat
        bool bounded = true;
        bool first_rain_found = false;
        int first_rain = -1;
        double ratio = 0.0;
        for (int month = 0; month < 240; ++month) {
            LayerArray after = column.before;
            const auto result = planetsim::solve_atmosphere_column(
                column.radiation, std::span<const double>(column.before.data(), 3U), month_s,
                0.0, true, surface, std::span<double>(after.data(), 3U), {}, nullptr,
                &column.moisture);
            for (std::size_t k = 0; k < 3U; ++k) {
                bounded = bounded && result.humidity_kg_kg[k] <=
                                         planetsim::saturation_specific_humidity(
                                             after[k], column.moisture.pressure_Pa[k]) *
                                             (1.0 + 1e-12);
                column.moisture.humidity_kg_kg[k] = result.humidity_kg_kg[k];
            }
            if (!first_rain_found && result.precipitation_kg_m2_s > 0.0) {
                first_rain_found = true;
                first_rain = month;
            }
            ratio = result.precipitation_kg_m2_s / result.evaporation_kg_m2_s;
            column.before = after;
        }
        std::cout << "dry column: first rain in month " << first_rain << ", P / E after 20 years "
                  << ratio << '\n';
        PLANETSIM_EXPECT(test, bounded);
        PLANETSIM_EXPECT(test, first_rain >= 0 && first_rain < 12);
        PLANETSIM_EXPECT(test, std::abs(ratio - 1.0) <= 1e-6);
    }
    return test.result();
}
