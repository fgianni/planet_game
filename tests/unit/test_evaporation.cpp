#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/atmosphere/water.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/land_snow.hpp"
#include "sim/planet/surface/sea_ice.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

// Evaporation in the surface tiles (ADR-0021 §4.3; task M7-02).
namespace {

constexpr double month_s = 2.63e6;
constexpr double transfer = 1.2 * 1.5e-3 * 10.0;   // ρ C_E V_e, kg/m²/s per unit of humidity
constexpr double pressure = 100'000.0;

[[nodiscard]] planetsim::ColumnProperties material(planetsim::SurfaceMaterial kind) {
    return planetsim::column_properties(kind, planetsim::PlanetParameters::earth_development());
}

// The tile budget: storage + latent of fusion + Δt · evaporation =
// Δt (absorbed − emitted + source), against the scale of its terms.
[[nodiscard]] double budget_error(const planetsim::ColumnStepResult& step, double latent_J_m2,
                                  double dt) {
    const double in = dt * (step.absorbed_W_m2 - step.emitted_W_m2 + step.source_W_m2);
    const double out = step.storage_change_J_m2 + latent_J_m2 + dt * step.evaporation_W_m2;
    const double scale = dt * (step.absorbed_W_m2 + step.emitted_W_m2 + std::abs(step.source_W_m2) +
                               std::abs(step.evaporation_W_m2)) +
                         std::abs(step.storage_change_J_m2) + std::abs(latent_J_m2);
    return std::abs(in - out) / scale;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto ocean = material(planetsim::SurfaceMaterial::ocean);
    const auto soil = material(planetsim::SurfaceMaterial::wet_soil);

    // 1. Open water: the root balances evaporation; its slope is the root's
    // response to the forcing; dew warms the surface.
    {
        const planetsim::ColumnState state{290.0, 285.0};
        const auto dry = planetsim::column_system(ocean, state, 300.0, 0.0, month_s);
        auto wet = dry;
        wet.water_transfer = transfer;
        wet.pressure_Pa = pressure;
        wet.air_humidity = 0.7 * planetsim::saturation_specific_humidity(288.0, pressure);
        wet.vapour_kg_m2 = 30.0;
        wet.step_s = month_s;
        const double x_dry = planetsim::solve_column_surface(dry);
        const double x = planetsim::solve_column_surface(wet);
        PLANETSIM_EXPECT(test, x < x_dry);
        PLANETSIM_EXPECT(test, std::abs(wet.surplus_W_m2(x)) <= 1e-9 * wet.b);
        const double latent = wet.latent_flux_W_m2(x);
        std::cout << "open water: T_s " << x << " K (dry " << x_dry << "), latent " << latent
                  << " W/m2\n";
        PLANETSIM_EXPECT(test, latent > 30.0 && latent < 300.0);
        auto shifted = wet;
        shifted.b += 1.0;
        const double finite = planetsim::solve_column_surface(shifted) - x;
        PLANETSIM_EXPECT(test, std::abs(wet.slope_K_m2_W(x) / finite - 1.0) <= 1e-3);
        auto dew = wet;
        dew.air_humidity = 1.5 * planetsim::saturation_specific_humidity(x_dry, pressure);
        const double x_dew = planetsim::solve_column_surface(dew);
        PLANETSIM_EXPECT(test, x_dew > x_dry && dew.latent_flux_W_m2(x_dew) < 0.0);
        // A month's dew never takes more than the air's vapour.
        auto heavy = dew;
        heavy.water_transfer = 100.0 * transfer;
        const double x_heavy = planetsim::solve_column_surface(heavy);
        PLANETSIM_EXPECT(test, -heavy.evaporation_kg_m2_s(x_heavy).water * month_s <=
                                   heavy.vapour_kg_m2 * (1.0 + 1e-12));
    }

    // 2. The stores' limits: a month never takes more than the bucket or
    // the snow holds, and the bucket's rate is continuous where β reaches 1.
    {
        planetsim::ColumnSystem system;
        system.a = 10.0;
        system.radiative = 0.97 * planetsim::stefan_boltzmann_W_m2_K4;
        system.b = 10.0 * 300.0 + 800.0;
        system.bucket_transfer = 10.0 * transfer;   // a strong demand
        system.bucket_threshold_kg_m2 = 0.75 * planetsim::bucket_capacity_kg_m2;
        system.step_s = month_s;
        system.pressure_Pa = pressure;
        system.air_humidity = 0.002;
        system.vapour_kg_m2 = 10.0;
        for (const double water : {0.0, 1.0, 30.0, 112.5, 150.0}) {
            system.bucket_kg_m2 = water;
            const double x = planetsim::solve_column_surface(system);
            const double taken = system.evaporation_kg_m2_s(x).bucket * month_s;
            PLANETSIM_EXPECT(test, taken >= 0.0 && taken <= water * (1.0 + 1e-12));
            PLANETSIM_EXPECT(test, std::abs(system.surplus_W_m2(x)) <= 1e-9 * system.b);
        }
        // Continuity at the β = 1 boundary, W = W_c + κ Δt.
        system.bucket_kg_m2 = 150.0;
        const double kappa = 0.05 * system.bucket_transfer;
        const double threshold_dq =
            (150.0 - system.bucket_threshold_kg_m2) / (system.bucket_transfer * month_s);
        PLANETSIM_EXPECT(test, threshold_dq > 0.0 && kappa > 0.0);
        system.bucket_transfer = transfer;
        system.snow_transfer = 10.0 * transfer;
        system.snow_kg_m2 = 5.0;
        const double x = planetsim::solve_column_surface(system);
        PLANETSIM_EXPECT(test, system.evaporation_kg_m2_s(x).snow * month_s <= 5.0 * (1.0 + 1e-12));
    }

    // 3. Land with snow: the energy budget closes with evaporation, whether
    // the surface is free, held at the melting point or melts out; snow,
    // bucket and sublimation stay within their stores.
    {
        double worst = 0.0;
        for (const double snow : {0.0, 20.0, 400.0}) {
            for (const double insolation : {50.0, 250.0, 450.0}) {
                for (const double surface : {260.0, 273.15, 285.0}) {
                    for (const double bucket : {0.0, 40.0, 150.0}) {
                        planetsim::EvaporationForcing forcing;
                        forcing.transfer_kg_m2_s = transfer;
                        forcing.pressure_Pa = pressure;
                        forcing.air_humidity = 0.6 * planetsim::saturation_specific_humidity(
                                                         surface - 2.0, pressure);
                        forcing.bucket_kg_m2 = bucket;
                        forcing.vapour_kg_m2 = 15.0;
                        const auto tile = planetsim::prepare_land_tile(
                            soil, {surface, surface - 1.0}, snow, insolation, 2e-5, 0.0, month_s,
                            forcing);
                        const auto result = planetsim::solve_land_tile(tile, 10.0 + 5.0 * 265.0, 5.0);
                        worst = std::max(worst,
                                         budget_error(result.column, result.latent_J_m2, month_s));
                        const double snow_in = snow + result.snowfall_kg_m2;
                        PLANETSIM_EXPECT(test, result.snow_kg_m2 >= 0.0);
                        PLANETSIM_EXPECT(test, std::abs(result.snow_kg_m2 - (snow_in -
                                                                             result.melt_kg_m2 -
                                                                             result.sublimation_kg_m2)) <=
                                                   1e-9 * (snow_in + 1.0));
                        PLANETSIM_EXPECT(test, result.bucket_evaporation_kg_m2 <=
                                                   (bucket + result.rain_kg_m2) * (1.0 + 1e-12) +
                                                       1e-12);
                        // Its latent heat is what the water fluxes carry.
                        const double latent =
                            planetsim::latent_heat_vaporisation_J_kg *
                                result.bucket_evaporation_kg_m2 +
                            planetsim::latent_heat_sublimation_J_kg * result.sublimation_kg_m2;
                        PLANETSIM_EXPECT(test, std::abs(latent - month_s * result.column.evaporation_W_m2) <=
                                                   1e-9 * (std::abs(latent) + 1.0));
                    }
                }
            }
        }
        std::cout << "land tiles: worst budget error " << worst << '\n';
        PLANETSIM_EXPECT(test, worst <= 1e-12);
    }

    // 4. The ocean tile, open and under ice: the energy budget closes with
    // the leads' evaporation and the floes' sublimation, which leaves ice.
    {
        double worst = 0.0;
        for (const double ice : {0.0, 100.0, 2'000.0}) {
            for (const double insolation : {0.0, 150.0, 400.0}) {
                for (const double source : {-60.0, 0.0, 80.0}) {
                    planetsim::EvaporationForcing forcing;
                    forcing.transfer_kg_m2_s = transfer;
                    forcing.pressure_Pa = pressure;
                    forcing.air_humidity = 0.5 * planetsim::saturation_specific_humidity(265.0, pressure);
                    forcing.vapour_kg_m2 = 5.0;
                    const double freezing = planetsim::seawater_freezing_point_K;
                    const planetsim::ColumnState state{ice > 0.0 ? freezing : 280.0, 279.0};
                    const auto tile = planetsim::prepare_ocean_tile(ocean, state, ice, insolation,
                                                                    0.0, month_s, forcing);
                    const auto result = planetsim::solve_ocean_tile(tile, source + 10.0 * 265.0,
                                                                    10.0);
                    worst = std::max(worst,
                                     budget_error(result.column, result.latent_J_m2, month_s));
                    PLANETSIM_EXPECT(test, result.ice_kg_m2 >= 0.0);
                }
            }
        }
        std::cout << "ocean tiles: worst budget error " << worst << '\n';
        PLANETSIM_EXPECT(test, worst <= 1e-12);
    }

    // 5. No transfer: the systems are the ADR-0007/ADR-0008 ones bit for bit.
    {
        const planetsim::ColumnState state{280.0, 279.0};
        const auto plain = planetsim::prepare_land_tile(soil, state, 10.0, 200.0, 1e-5, 0.0, month_s);
        const auto none =
            planetsim::prepare_land_tile(soil, state, 10.0, 200.0, 1e-5, 0.0, month_s, {});
        const auto a = planetsim::solve_land_tile(plain, 30.0, 5.0);
        const auto b = planetsim::solve_land_tile(none, 30.0, 5.0);
        PLANETSIM_EXPECT(test, a.column.state.surface_K == b.column.state.surface_K &&
                                   b.column.evaporation_W_m2 == 0.0);
    }
    // 6. V5: monthly steps of a wet, dry-soil (low heat capacity) land tile
    // under a strong demand approach their balance without oscillating: ΔT
    // never changes sign from one step to the next.
    {
        const auto dry_soil = material(planetsim::SurfaceMaterial::dry_soil);
        planetsim::ColumnState state{300.0, 295.0};
        double previous = 0.0;
        bool monotone = true;
        for (int month = 0; month < 24; ++month) {
            planetsim::EvaporationForcing forcing;
            forcing.transfer_kg_m2_s = 3.0 * transfer;
            forcing.pressure_Pa = pressure;
            forcing.air_humidity = 0.002;
            forcing.bucket_kg_m2 = planetsim::bucket_capacity_kg_m2;
            forcing.vapour_kg_m2 = 15.0;
            const auto tile = planetsim::prepare_land_tile(dry_soil, state, 0.0, 300.0, 0.0, 0.0,
                                                           month_s, forcing);
            const auto result = planetsim::solve_land_tile(tile, 10.0 * 280.0, 10.0);
            const double change = result.column.state.surface_K - state.surface_K;
            monotone = monotone && change * previous >= 0.0;
            previous = change;
            state = result.column.state;
        }
        PLANETSIM_EXPECT(test, monotone);
    }
    return test.result();
}
