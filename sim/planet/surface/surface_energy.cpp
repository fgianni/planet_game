#include "sim/planet/surface/surface_energy.hpp"

#include "sim/planet/atmosphere/water.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/climatology/monthly_climatology.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "sim/planet/surface/land_snow.hpp"
#include "sim/planet/surface/sea_ice.hpp"
#include "sim/planet/surface/cover_fractions.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace planetsim {

SurfaceEnergyParameters surface_energy_parameters_for(PlanetPreset preset) noexcept {
    switch (preset) {
    case PlanetPreset::dead_rock:
        return {SurfaceMaterial::rock, 0.0, 0.0, 0.0, {}};
    case PlanetPreset::aqua_planet:
        return {SurfaceMaterial::dry_soil, 0.0, 0.0, 0.0, {}};
    case PlanetPreset::earth_like:
        return {SurfaceMaterial::dry_soil, 0.0, earth_like_transport_coefficient_W_m2_K,
                bulk_air_exchange_W_m2_K, atmosphere_parameters_for(preset)};
    }
    return {};
}

void compute_annual_mean_insolation(const PlanetMesh& mesh, const PlanetParameters& parameters,
                                    Field2D<double>& annual_mean_W_m2,
                                    std::size_t worker_count) {
    const std::size_t cells = mesh.cell_count();
    annual_mean_W_m2 = Field2D<double>(cells, 0.0);
    Field2D<float> substep_mean(cells, 0.0F);
    double total_ticks = 0.0;
    for (std::int64_t index = 0; index < climate_substeps_per_year; ++index) {
        const ClimateSubstep substep = climate_substep(index, parameters);
        compute_substep_mean_insolation(mesh, parameters, substep, substep_mean, worker_count);
        const auto length = static_cast<double>(substep.length_ticks());
        for (std::size_t cell = 0; cell < cells; ++cell) {
            annual_mean_W_m2[cell] += length * static_cast<double>(substep_mean[cell]);
        }
        total_ticks += length;
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        annual_mean_W_m2[cell] /= total_ticks;
    }
}

void initialise_surface_temperatures(const PlanetMesh& mesh, SlowState& slow,
                                     const PlanetParameters& parameters,
                                     const SurfaceEnergyParameters& surface,
                                     std::size_t worker_count) {
    const std::size_t cells = mesh.cell_count();
    Field2D<double> annual_mean;
    compute_annual_mean_insolation(mesh, parameters, annual_mean, worker_count);
    const ColumnProperties land = column_properties(surface.land_material, parameters);
    const ColumnProperties ocean = column_properties(SurfaceMaterial::ocean, parameters);
    // Under a layered atmosphere the start is the grey-layer equilibrium of
    // one slab of the column's whole optical depth, g = 1 − exp(−τ₀) (at most
    // 0.95): warm and free of ice, so that the spin-up cools into the
    // climate rather than starting inside the ice–albedo feedback (ADR-0010
    // §4.6).
    const double greenhouse =
        surface.atmosphere.layer_count > 0U
            ? std::min(0.95, -std::expm1(-surface.atmosphere.longwave_optical_depth))
            : surface.grey_emissivity;

    slow.land_surface_temperature_K = Field2D<float>(cells, 0.0F);
    slow.land_ground_temperature_K = Field2D<float>(cells, 0.0F);
    slow.ocean_mixed_layer_temperature_K = Field2D<double>(cells, 0.0);
    slow.ocean_deep_temperature_K = Field2D<double>(cells, 0.0);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                // A cell with no annual sunlight at all would have no finite
                // equilibrium above 0 K; no Earth-like orbit produces one.
                if (!(annual_mean[cell] > 0.0)) {
                    throw std::domain_error("a cell receives no annual-mean insolation");
                }
                const double land_K =
                    column_equilibrium_temperature_K(land, annual_mean[cell], greenhouse);
                const double ocean_K =
                    column_equilibrium_temperature_K(ocean, annual_mean[cell], greenhouse);
                slow.land_surface_temperature_K[cell] = static_cast<float>(land_K);
                slow.land_ground_temperature_K[cell] = static_cast<float>(land_K);
                slow.ocean_mixed_layer_temperature_K[cell] = ocean_K;
                slow.ocean_deep_temperature_K[cell] = ocean_K;
            }
        });
}

void initialise_climate(const PlanetMesh& mesh, SlowState& slow,
                        const PlanetParameters& parameters,
                        const SurfaceEnergyParameters& surface, std::size_t worker_count) {
    initialise_surface_temperatures(mesh, slow, parameters, surface, worker_count);
    initialise_cryosphere(mesh, slow);
    initialise_atmosphere(mesh, slow, parameters, surface.atmosphere);
}

void initialise_cryosphere(const PlanetMesh& mesh, SlowState& slow) {
    slow.land_snow_water_equivalent_kg_m2 = Field2D<double>(mesh.cell_count(), 0.0);
    slow.sea_ice_mass_kg_m2 = Field2D<double>(mesh.cell_count(), 0.0);
    // Seawater is never colder than its freezing point: an ADR-0007 state
    // without ice can hold polar oceans far below it, which the sea-ice
    // physics would pay for with hundreds of metres of ice (ADR-0008 §9).
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        auto& mixed = slow.ocean_mixed_layer_temperature_K[cell];
        auto& deep = slow.ocean_deep_temperature_K[cell];
        mixed = std::max(mixed, seawater_freezing_point_K);
        deep = std::max(deep, seawater_freezing_point_K);
    }
}

namespace {

struct BudgetPartial {
    double absorbed_W = 0.0;
    double emitted_W = 0.0;
    double storage_change_J = 0.0;
    double stored_energy_J = 0.0;
    double latent_heat_J = 0.0;
    double transport_W = 0.0;
    double max_newton_residual_W_m2 = 0.0;
    double snowfall_kg = 0.0;
    double rain_kg = 0.0;
    double melt_kg = 0.0;
    double snow_change_kg = 0.0;
    double snow_kg = 0.0;
    double ice_frozen_kg = 0.0;
    double ice_melted_kg = 0.0;
    double ice_change_kg = 0.0;
    double ice_kg = 0.0;
    double water_evaporation_kg = 0.0;
    double bucket_evaporation_kg = 0.0;
    double snow_sublimation_kg = 0.0;
    double ice_sublimation_kg = 0.0;
    double bucket_runoff_kg = 0.0;   // the bucket's overflow to the ocean
    double vapour_change_kg = 0.0;
    double bucket_change_kg = 0.0;
    double bucket_kg = 0.0;
    double vapour_kg = 0.0;
    double evaporation_latent_J = 0.0;
    double precipitation_kg = 0.0;
    double ocean_precipitation_kg = 0.0;
    double condensation_latent_J = 0.0;
    double ice_area_north_m2 = 0.0;
    double ice_area_south_m2 = 0.0;
    double ice_mass_north_kg = 0.0;
    double ice_mass_south_kg = 0.0;
    double snow_area_north_m2 = 0.0;
    double snow_area_south_m2 = 0.0;
    double ice_cover_north_m2 = 0.0;
    double ice_cover_south_m2 = 0.0;
    double snow_cover_north_m2 = 0.0;
    double snow_cover_south_m2 = 0.0;
    double weighted_temperature_K_m2 = 0.0;
    double area_m2 = 0.0;
    std::array<double, 18> band_temperature_K_m2{};
    std::array<double, 18> band_area_m2{};
    double p2_temperature_K_m2 = 0.0;
    double p2_squared_m2 = 0.0;
    double land_temperature_K_m2 = 0.0;
    double land_area_m2 = 0.0;
    double ocean_temperature_K_m2 = 0.0;
    double ocean_area_m2 = 0.0;
    double min_K = std::numeric_limits<double>::infinity();
    double max_K = -std::numeric_limits<double>::infinity();
    // The atmosphere (ADR-0010).
    double atmosphere_storage_change_J = 0.0;
    double surface_upward_W = 0.0;
    double downward_W = 0.0;
    double sensible_W = 0.0;
    double max_column_residual_W_m2 = 0.0;
    double max_column_correction_K = 0.0;
    int max_column_iterations = 0;
    std::size_t unconverged_columns = 0;
    double convective_area_m2 = 0.0;
    std::array<double, max_atmosphere_layer_count> layer_temperature_K_m2{};
};

[[nodiscard]] BudgetPartial combine(BudgetPartial a, const BudgetPartial& b) {
    a.absorbed_W += b.absorbed_W;
    a.emitted_W += b.emitted_W;
    a.storage_change_J += b.storage_change_J;
    a.stored_energy_J += b.stored_energy_J;
    a.latent_heat_J += b.latent_heat_J;
    a.transport_W += b.transport_W;
    a.max_newton_residual_W_m2 = std::max(a.max_newton_residual_W_m2, b.max_newton_residual_W_m2);
    a.snowfall_kg += b.snowfall_kg;
    a.rain_kg += b.rain_kg;
    a.melt_kg += b.melt_kg;
    a.snow_change_kg += b.snow_change_kg;
    a.snow_kg += b.snow_kg;
    a.ice_frozen_kg += b.ice_frozen_kg;
    a.ice_melted_kg += b.ice_melted_kg;
    a.ice_change_kg += b.ice_change_kg;
    a.ice_kg += b.ice_kg;
    a.water_evaporation_kg += b.water_evaporation_kg;
    a.bucket_evaporation_kg += b.bucket_evaporation_kg;
    a.snow_sublimation_kg += b.snow_sublimation_kg;
    a.ice_sublimation_kg += b.ice_sublimation_kg;
    a.bucket_runoff_kg += b.bucket_runoff_kg;
    a.vapour_change_kg += b.vapour_change_kg;
    a.bucket_change_kg += b.bucket_change_kg;
    a.bucket_kg += b.bucket_kg;
    a.vapour_kg += b.vapour_kg;
    a.evaporation_latent_J += b.evaporation_latent_J;
    a.precipitation_kg += b.precipitation_kg;
    a.ocean_precipitation_kg += b.ocean_precipitation_kg;
    a.condensation_latent_J += b.condensation_latent_J;
    a.ice_area_north_m2 += b.ice_area_north_m2;
    a.ice_area_south_m2 += b.ice_area_south_m2;
    a.ice_mass_north_kg += b.ice_mass_north_kg;
    a.ice_mass_south_kg += b.ice_mass_south_kg;
    a.snow_area_north_m2 += b.snow_area_north_m2;
    a.snow_area_south_m2 += b.snow_area_south_m2;
    a.ice_cover_north_m2 += b.ice_cover_north_m2;
    a.ice_cover_south_m2 += b.ice_cover_south_m2;
    a.snow_cover_north_m2 += b.snow_cover_north_m2;
    a.snow_cover_south_m2 += b.snow_cover_south_m2;
    a.weighted_temperature_K_m2 += b.weighted_temperature_K_m2;
    a.area_m2 += b.area_m2;
    a.p2_temperature_K_m2 += b.p2_temperature_K_m2;
    a.p2_squared_m2 += b.p2_squared_m2;
    for (std::size_t band = 0; band < a.band_area_m2.size(); ++band) {
        a.band_temperature_K_m2[band] += b.band_temperature_K_m2[band];
        a.band_area_m2[band] += b.band_area_m2[band];
    }
    a.land_temperature_K_m2 += b.land_temperature_K_m2;
    a.land_area_m2 += b.land_area_m2;
    a.ocean_temperature_K_m2 += b.ocean_temperature_K_m2;
    a.ocean_area_m2 += b.ocean_area_m2;
    a.min_K = std::min(a.min_K, b.min_K);
    a.max_K = std::max(a.max_K, b.max_K);
    a.atmosphere_storage_change_J += b.atmosphere_storage_change_J;
    a.surface_upward_W += b.surface_upward_W;
    a.downward_W += b.downward_W;
    a.sensible_W += b.sensible_W;
    a.max_column_residual_W_m2 = std::max(a.max_column_residual_W_m2, b.max_column_residual_W_m2);
    a.max_column_correction_K = std::max(a.max_column_correction_K, b.max_column_correction_K);
    a.max_column_iterations = std::max(a.max_column_iterations, b.max_column_iterations);
    a.unconverged_columns += b.unconverged_columns;
    a.convective_area_m2 += b.convective_area_m2;
    for (std::size_t layer = 0; layer < a.layer_temperature_K_m2.size(); ++layer) {
        a.layer_temperature_K_m2[layer] += b.layer_temperature_K_m2[layer];
    }
    return a;
}

// `radiating_K` is the tile's surface temperature: the land surface layer,
// the mixed layer, or the sea-ice surface.
// Under an atmosphere (`to_space` false) the tile's emission goes into its
// column and its source is the exchange with it, both internal to the cell;
// the column accounts for what leaves the planet.
void accumulate_tile(BudgetPartial& partial, const ColumnProperties& column,
                     const ColumnStepResult& result, double radiating_K, double weight_m2,
                     bool to_space = true) {
    if (!(weight_m2 > 0.0)) {
        return;
    }
    partial.absorbed_W += weight_m2 * result.absorbed_W_m2;
    if (to_space) {
        partial.emitted_W += weight_m2 * result.emitted_W_m2;
        partial.transport_W += weight_m2 * result.source_W_m2;
    }
    partial.storage_change_J += weight_m2 * result.storage_change_J_m2;
    partial.stored_energy_J +=
        weight_m2 * (column.surface_heat_capacity_J_m2_K * result.state.surface_K +
                     column.lower_heat_capacity_J_m2_K * result.state.lower_K);
    partial.max_newton_residual_W_m2 =
        std::max(partial.max_newton_residual_W_m2, std::abs(result.newton_residual_W_m2));
    partial.weighted_temperature_K_m2 += weight_m2 * radiating_K;
    partial.area_m2 += weight_m2;
    partial.min_K = std::min(partial.min_K, radiating_K);
    partial.max_K = std::max(partial.max_K, radiating_K);
}

struct CellTiles {
    LandSnowStepResult land;
    OceanTileResult ocean;
    double mean_K = 0.0;          // T̄: area-weighted over the tiles with area
    double slope_K_m2_W = 0.0;    // dT̄/dh
};

// One cell's tiles under the transport source h (W/m² of cell area), ADR-0009
// §4.1 and §10. The tiles with area share h per unit area; with air exchange
// γ every tile also receives γ (T̄ − T_t), and T̄ is found by safeguarded
// Newton on G(T̄) = Σ w_t T_t(T̄) − T̄, which decreases with slope in (−1, 0).
// With a single tile of area, T̄ is that tile's temperature and the exchange
// cancels, so one solve gives it. A tile without area receives no transport
// and follows the cell's air. Without `with_tiles` only T̄ and its slope are
// computed (the transport solve's response).
CellTiles solve_cell(const LandSnowSystem& land_tile, const OceanTileSystem& ocean_tile,
                     double land_fraction, double ocean_fraction, double h, double exchange,
                     double first_guess_K, bool with_tiles) {
    const double share = land_fraction + ocean_fraction;
    const double land_weight = land_fraction / share;
    const double ocean_weight = ocean_fraction / share;
    const double land_source = land_fraction > 0.0 ? h / share : 0.0;
    const double ocean_source = ocean_fraction > 0.0 ? h / share : 0.0;

    CellTiles tiles;
    if (exchange == 0.0) {
        tiles.land = solve_land_tile(land_tile, land_source);
        tiles.ocean = solve_ocean_tile(ocean_tile, ocean_source);
        tiles.mean_K = land_weight * tiles.land.column.state.surface_K +
                       ocean_weight * tiles.ocean.radiating_K;
        tiles.slope_K_m2_W = (land_weight * tiles.land.column.surface_slope_K_m2_W +
                              ocean_weight * tiles.ocean.column.surface_slope_K_m2_W) /
                             share;
        return tiles;
    }

    // A tile whose right-hand side is not positive has no positive root;
    // it only happens far from the solution and counts as 0 K.
    const auto evaluate = [&](double mean_K, double& weighted_slope) {
        double weighted = 0.0;
        weighted_slope = 0.0;
        if (land_fraction > 0.0 &&
            land_tile.system.b + land_source + exchange * mean_K > 0.0) {
            const auto land = solve_land_tile(land_tile, land_source + exchange * mean_K, exchange);
            weighted += land_weight * land.column.state.surface_K;
            weighted_slope += land_weight * land.column.surface_slope_K_m2_W;
        }
        if (ocean_fraction > 0.0 &&
            (ocean_tile.ice_kg_m2 > 0.0 ||
             ocean_tile.open.b + ocean_source + exchange * mean_K > 0.0)) {
            const auto water =
                solve_ocean_tile(ocean_tile, ocean_source + exchange * mean_K, exchange);
            weighted += ocean_weight * water.radiating_K;
            weighted_slope += ocean_weight * water.column.surface_slope_K_m2_W;
        }
        return weighted - mean_K;
    };
    double mean_K = 0.0;
    double weighted_slope = 0.0;
    if (land_fraction <= 0.0 || ocean_fraction <= 0.0) {
        // T̄ = T_t: the tile's own equation with source h / share.
        if (land_fraction > 0.0) {
            const auto land = solve_land_tile(land_tile, land_source);
            mean_K = land.column.state.surface_K;
            tiles.slope_K_m2_W = land.column.surface_slope_K_m2_W / share;
        } else {
            const auto water = solve_ocean_tile(ocean_tile, ocean_source);
            mean_K = water.radiating_K;
            tiles.slope_K_m2_W = water.column.surface_slope_K_m2_W / share;
        }
        tiles.mean_K = mean_K;
        if (with_tiles) {
            tiles.land = solve_land_tile(land_tile, land_source + exchange * mean_K, exchange);
            tiles.ocean =
                solve_ocean_tile(ocean_tile, ocean_source + exchange * mean_K, exchange);
        }
        return tiles;
    }

    double low = 0.0;        // G(low) ≥ 0
    double high = 1.0e4;     // G(high) < 0
    mean_K = std::clamp(first_guess_K, low, high);
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double g = evaluate(mean_K, weighted_slope);
        (g > 0.0 ? low : high) = mean_K;
        const double derivative = exchange * weighted_slope - 1.0;
        double next = mean_K - g / derivative;
        if (!(next > low && next < high)) {
            next = 0.5 * (low + high);
        }
        // To full precision: the cell's T̄ error, times the neighbour
        // coupling K Σ w / A (thousands of W/m²/K at L6), is the floor of
        // the transport solve's residual.
        if (std::abs(next - mean_K) <= 4.0 * std::numeric_limits<double>::epsilon() * mean_K) {
            mean_K = next;
            break;
        }
        mean_K = next;
    }
    static_cast<void>(evaluate(mean_K, weighted_slope));

    if (with_tiles) {
        tiles.land = solve_land_tile(land_tile, land_source + exchange * mean_K, exchange);
        tiles.ocean = solve_ocean_tile(ocean_tile, ocean_source + exchange * mean_K, exchange);
    }
    tiles.mean_K = mean_K;
    // T̄ = Σ w T_t(h/share + γ T̄)  ⇒  dT̄/dh = (Σ w s_t / share) / (1 − γ Σ w s_t).
    tiles.slope_K_m2_W = weighted_slope / share / (1.0 - exchange * weighted_slope);
    return tiles;
}

// A cell's tiles as the surface below its atmospheric column (ADR-0010
// §4.4): for the downward longwave D and the surface air temperature A,
// each tile with area solves its step with the source ε_t D + γ A and the
// exchange γ, and the cell returns the longwave sent up (emission plus
// reflection; area not covered by a tile reflects) and the sensible heat
// given to the air, linearised through the tiles' slopes. The last call's
// tile results are kept.
struct CellSurface {
    const LandSnowSystem* land = nullptr;
    const OceanTileSystem* ocean = nullptr;
    double land_fraction = 0.0;
    double ocean_fraction = 0.0;
    double exchange = 0.0;
    // The water cycle (ADR-0021 §4.3 as amended in task M7-03, §4.4): the
    // surface air carries the bottom layer's relative humidity to the
    // surface air temperature A, q_a = q₀ q_sat(A, p_s) / q_sat(T₀, p₀) with
    // T₀ = A / air_factor, and holds q_cap = q_sat(A, p_s) where the layer
    // rains.
    bool water = false;
    double bottom_humidity = 0.0;   // q₀ at the start of the step
    double bottom_pressure_Pa = 0.0;
    double surface_pressure_Pa = 0.0;
    double air_factor = 1.0;
    LandSnowStepResult land_result;
    OceanTileResult ocean_result;

    SurfaceExchange operator()(double downward_W_m2, double air_K) {
        SurfaceExchange x;
        SurfaceAir air;
        double air_slope = 0.0;   // dq_a/dA
        double cap_slope = 0.0;   // dq_cap/dA
        if (water) {
            const double bottom_K = air_K / air_factor;
            const double layer_saturated =
                saturation_specific_humidity(bottom_K, bottom_pressure_Pa);
            const double air_saturated = saturation_specific_humidity(air_K, surface_pressure_Pa);
            const double air_saturated_slope =
                saturation_specific_humidity_slope(air_K, surface_pressure_Pa);
            const double factor = air_saturated / layer_saturated;
            air.humidity = factor * bottom_humidity;
            air.cap_humidity = air_saturated;
            air_slope = air.humidity *
                        (air_saturated_slope / air_saturated -
                         saturation_specific_humidity_slope(bottom_K, bottom_pressure_Pa) /
                             (air_factor * layer_saturated));
            cap_slope = air_saturated_slope;
        }
        // A tile's source is ε_t D + γ A; with the water cycle A also moves
        // q_a and q_cap, which act on the tile's surface as a source
        // −∂(L E)/∂q δq (LandSnowStepResult). `source_air` is ds/dA.
        // `emitted_slope` is d(emitted)/d(source): one surface's 4 r T³ dT/ds,
        // or the floes' share of it under ice (ADR-0008 §10).
        const auto add = [&](double fraction, double emissivity, const ColumnStepResult& step,
                             double surface_K, double emitted_slope, double source_air) {
            const double slope = step.surface_slope_K_m2_W;
            x.upward_W_m2 += fraction * (step.emitted_W_m2 + (1.0 - emissivity) * downward_W_m2);
            x.d_upward_d_downward += fraction * (emitted_slope * emissivity + 1.0 - emissivity);
            x.d_upward_d_air += fraction * emitted_slope * source_air;
            x.sensible_W_m2 += fraction * exchange * (surface_K - air_K);
            x.d_sensible_d_downward += fraction * exchange * slope * emissivity;
            x.d_sensible_d_air += fraction * exchange * (slope * source_air - 1.0);
        };
        const auto source_air_of = [&](double latent_air, double latent_cap) {
            return exchange - latent_air * air_slope - latent_cap * cap_slope;
        };
        const auto add_vapour = [&](double fraction, double emissivity, double vapour,
                                    double source_slope, double air_vapour_slope,
                                    double cap_vapour_slope, double source_air) {
            x.vapour_kg_m2_s += fraction * vapour;
            x.d_vapour_d_downward += fraction * source_slope * emissivity;
            x.d_vapour_d_air += fraction * (source_slope * source_air +
                                            air_vapour_slope * air_slope +
                                            cap_vapour_slope * cap_slope);
        };
        if (land_fraction > 0.0) {
            const double emissivity = land->column.emissivity;
            land_result = solve_land_tile(*land, emissivity * downward_W_m2 + exchange * air_K,
                                          exchange, air);
            const ColumnStepResult& step = land_result.column;
            const double source_air =
                source_air_of(land_result.latent_air_slope, land_result.latent_cap_slope);
            add(land_fraction, emissivity, step, step.state.surface_K,
                4.0 * step.emitted_W_m2 / step.state.surface_K * step.surface_slope_K_m2_W,
                source_air);
            add_vapour(land_fraction, emissivity, land_result.vapour_kg_m2_s,
                       land_result.vapour_source_slope, land_result.vapour_air_slope,
                       land_result.vapour_cap_slope, source_air);
        }
        if (ocean_fraction > 0.0) {
            const double emissivity = ocean->column.emissivity;
            ocean_result = solve_ocean_tile(*ocean, emissivity * downward_W_m2 + exchange * air_K,
                                            exchange, air);
            const double source_air =
                source_air_of(ocean_result.latent_air_slope, ocean_result.latent_cap_slope);
            add(ocean_fraction, emissivity, ocean_result.column, ocean_result.radiating_K,
                ocean_result.emitted_slope, source_air);
            add_vapour(ocean_fraction, emissivity, ocean_result.vapour_kg_m2_s,
                       ocean_result.vapour_source_slope, ocean_result.vapour_air_slope,
                       ocean_result.vapour_cap_slope, source_air);
        }
        // Not clamped: where the float fractions sum to slightly more than
        // one, the tiles receive D over Σ f and the column gives D over its
        // area; the small negative remainder keeps the exchange exact.
        const double uncovered = 1.0 - land_fraction - ocean_fraction;
        x.upward_W_m2 += uncovered * downward_W_m2;
        x.d_upward_d_downward += uncovered;
        return x;
    }
};

// ADR-0021 §4.5: the humidity on the coarse graph's groups, mass-weighted
// (A p_s), carried by solve_implicit_tracer; each cell's humidity then
// scales with its group's, which conserves the water and keeps it
// non-negative.
[[nodiscard]] HumidityTransportDiagnostics transport_humidity(
    const PlanetMesh& mesh, SlowState& slow, const AdvectionDiffusion& transport,
    double gravity_m_s2, double dt_s, std::size_t worker_count) {
    const std::vector<std::size_t>* group_map = nullptr;
    const TransportGraph& graph = agglomerated_transport_graph(mesh, group_map);
    const std::vector<std::size_t>& group_of_cell = *group_map;
    const std::size_t cells = mesh.cell_count();
    const std::size_t groups = graph.size();
    const std::size_t layers = slow.atmosphere_layer_count();
    if (transport.layers != layers) {
        throw std::invalid_argument("the circulation's layers do not match the humidity's");
    }
    auto& humidity = slow.atmosphere_specific_humidity_kg_kg;
    std::vector<double> column_mass(groups, 0.0);    // Σ A p_s / g
    std::vector<double> vapour(layers * groups, 0.0);   // Σ A p_s q / g
    HumidityTransportDiagnostics d;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const std::size_t group = group_of_cell[cell];
        const double mass =
            mesh.cells()[cell].area_m2 * slow.atmosphere_surface_pressure_Pa[cell] / gravity_m_s2;
        column_mass[group] += mass;
        for (std::size_t layer = 0; layer < layers; ++layer) {
            vapour[layer * groups + group] += mass * humidity.layer(layer)[cell];
        }
    }
    const double n = static_cast<double>(layers);
    std::vector<double> layer_mass(groups, 0.0);
    std::vector<double> mean(layers * groups, 0.0);
    for (std::size_t group = 0; group < groups; ++group) {
        layer_mass[group] = column_mass[group] / n;
        for (std::size_t layer = 0; layer < layers; ++layer) {
            mean[layer * groups + group] = vapour[layer * groups + group] / column_mass[group];
            d.vapour_kg += vapour[layer * groups + group] / n;
        }
    }
    const TracerTransportResult moved =
        solve_implicit_tracer(graph, transport, layer_mass, mean, dt_s, 1e-12, 2'000, worker_count);
    double after = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const std::size_t group = group_of_cell[cell];
        const double mass = mesh.cells()[cell].area_m2 *
                            slow.atmosphere_surface_pressure_Pa[cell] / (gravity_m_s2 * n);
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const std::size_t at = layer * groups + group;
            double& q = humidity.layer(layer)[cell];
            q = mean[at] > 0.0 ? q * (moved.tracer[at] / mean[at]) : moved.tracer[at];
            after += mass * q;
        }
    }
    d.transported = true;
    d.iterations = moved.iterations;
    d.relative_residual = moved.relative_residual;
    d.vapour_change_kg = after - d.vapour_kg;
    d.clipped_kg = moved.clipped_kg;
    d.column_divergence_kg_s = moved.column_divergence_kg_s;
    return d;
}

}  // namespace

double SurfaceEnergyDiagnostics::closure_residual_J() const noexcept {
    // Evaporation's latent heat leaves the sensible energy for the vapour;
    // condensation's returns to it.
    return std::abs(storage_change_J + latent_heat_J + evaporation_latent_J -
                    condensation_latent_J -
                    duration_s * (absorbed_W - emitted_W + transport_W));
}

double SurfaceEnergyDiagnostics::closure_gate_J() const noexcept {
    // The atmosphere's internal streams are computed and rounded too.
    const double scale = duration_s * (absorbed_W + emitted_W + transport_absolute_W +
                                       surface_upward_longwave_W + downward_longwave_W +
                                       std::abs(sensible_heat_W)) +
                         std::abs(storage_change_J) + std::abs(latent_heat_J) +
                         std::abs(evaporation_latent_J) + condensation_latent_J;
    return 1e-9 * scale + 4.0 * std::numeric_limits<double>::epsilon() * stored_energy_J;
}

double SurfaceEnergyDiagnostics::water_residual_kg() const noexcept {
    return std::abs(snow_change_kg + ice_change_kg -
                    (snowfall_kg - melt_kg - snow_sublimation_kg + ice_frozen_kg - ice_melted_kg -
                     ice_sublimation_kg));
}

double SurfaceEnergyDiagnostics::water_cycle_residual_kg() const noexcept {
    // The ocean is the reservoir: it gives its evaporation and takes the rain
    // on it, the bucket's overflow and the ice's melt.
    return std::abs(vapour_change_kg + bucket_change_kg + snow_change_kg + ice_change_kg -
                    (water_evaporation_kg - ocean_precipitation_kg - bucket_runoff_kg +
                     ice_frozen_kg - ice_melted_kg));
}

double SurfaceEnergyDiagnostics::water_cycle_gate_kg() const noexcept {
    const double stock = vapour_kg + bucket_kg + snow_kg + ice_kg;
    return 1e-12 * (std::abs(water_evaporation_kg) + std::abs(bucket_evaporation_kg) +
                    std::abs(snow_sublimation_kg) + std::abs(ice_sublimation_kg) +
                    precipitation_kg + rain_kg +
                    snowfall_kg + bucket_runoff_kg + ice_frozen_kg + ice_melted_kg + melt_kg + stock) +
           4.0 * std::numeric_limits<double>::epsilon() * stock;
}

double SurfaceEnergyDiagnostics::water_gate_kg() const noexcept {
    const double stock = snow_kg + ice_kg;
    return 1e-12 * (snowfall_kg + melt_kg + ice_frozen_kg + ice_melted_kg + stock) +
           4.0 * std::numeric_limits<double>::epsilon() * stock;
}

double AnnualSurfaceSummary::peak_poleward_transport_W() const noexcept {
    double north = 0.0;
    double south = 0.0;
    for (std::size_t band = 0; band < northward_transport_W.size(); ++band) {
        north = std::max(north, northward_transport_W[band]);
        south = std::max(south, -northward_transport_W[band]);
    }
    return std::max(north, south);
}

double AnnualSurfaceSummary::equator_to_pole_difference_K() const noexcept {
    const auto& t = zonal_mean_surface_temperature_K;
    return 0.5 * (t[8] + t[9]) - 0.5 * (t[0] + t[17]);
}

double AnnualSurfaceSummary::relative_imbalance() const noexcept {
    return (absorbed_W - emitted_W) / absorbed_W;
}

SurfaceEnergyDiagnostics step_surface_energy(PlanetState& state,
                                             const PlanetParameters& parameters,
                                             const SurfaceEnergyParameters& surface,
                                             const SurfaceFractions& fractions,
                                             const Field2D<float>& insolation_W_m2, double dt_s,
                                             std::size_t worker_count,
                                             const CirculationTransport* circulation) {
    const PlanetMesh& mesh = state.mesh();
    const std::size_t cells = mesh.cell_count();
    const Field2D<float>& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
    if (insolation_W_m2.size() != cells || fractions.land_fraction.size() != cells ||
        fractions.ocean_fraction.size() != cells || precipitation.size() != cells ||
        state.slow().land_snow_water_equivalent_kg_m2.size() != cells ||
        state.slow().sea_ice_mass_kg_m2.size() != cells) {
        throw std::invalid_argument("surface energy inputs do not match the mesh");
    }
    const double coefficient = surface.transport_coefficient_W_m2_K;
    if (!std::isfinite(coefficient) || coefficient < 0.0) {
        throw std::invalid_argument("transport coefficient must be finite and non-negative");
    }
    const ColumnProperties land = column_properties(surface.land_material, parameters);
    const ColumnProperties ocean = column_properties(SurfaceMaterial::ocean, parameters);
    SlowState& slow = state.slow();

    // Each tile's system is prepared once; the transport solve re-solves it
    // for as many sources as it needs (ADR-0009 §4.3).
    std::vector<LandSnowSystem> land_tiles(cells);
    std::vector<OceanTileSystem> ocean_tiles(cells);
    std::vector<double> tile_share(cells, 0.0);   // f_land + f_ocean
    std::vector<std::uint8_t> band_of_cell(cells, 0U);   // 10° band from 90° S
    std::vector<double> p2_of_cell(cells, 0.0);           // P2(sin φ)
    // Evaporation (ADR-0021 §4.3): the bulk transfer from the circulation's
    // bottom-layer wind with gustiness, the bottom layer's humidity, vapour
    // and density.
    const bool water = surface.water_cycle && surface.atmosphere.layer_count > 0U;
    if (water && (slow.atmosphere_specific_humidity_kg_kg.layer_count() !=
                      surface.atmosphere.layer_count ||
                  slow.land_surface_water_kg_m2.size() != cells)) {
        throw std::invalid_argument("the water cycle needs the state's water (initialise_water)");
    }
    // ADR-0021 §4.5: the month's transport moves the humidity first, with the
    // circulation's fixed fluxes on the coarse graph (split B: the columns
    // then evaporate and rain out). A month whose circulation failed moves
    // none (ADR-0011 §17.4's fallback has no mass fluxes).
    HumidityTransportDiagnostics humidity_transport;
    if (water && circulation != nullptr && circulation->active) {
        humidity_transport = transport_humidity(mesh, slow, circulation->transport,
                                                surface_gravity_m_s2(parameters), dt_s,
                                                worker_count);
    }
    const auto& circulation_fields = state.circulation();
    const bool has_wind = circulation_fields.available() &&
                          circulation_fields.eastward_wind_m_s.cell_count() == cells;
    const double water_gravity = surface_gravity_m_s2(parameters);
    const double water_layers = static_cast<double>(surface.atmosphere.layer_count);
    const double water_lapse = critical_lapse_exponent(surface.atmosphere, water_gravity);
    std::vector<double> bottom_mass(water ? cells : 0U, 0.0);   // m₀, kg/m²
    // The share of the cell's condensate that falls as snow: its land tile's
    // where that starts the step frozen (ADR-0008 §4.3).
    std::vector<double> snow_share(water ? cells : 0U, 0.0);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double insolation = insolation_W_m2[cell];
                EvaporationForcing land_forcing;
                EvaporationForcing ocean_forcing;
                if (water) {
                    const double ps = slow.atmosphere_surface_pressure_Pa[cell];
                    const double air_K = surface_air_temperature_K(
                        slow.atmosphere_temperature_K.layer(0)[cell],
                        surface.atmosphere.layer_count, water_lapse);
                    const double density = ps / (dry_air_gas_constant_J_kg_K * air_K);
                    const double wind =
                        (has_wind ? std::hypot(static_cast<double>(
                                                   circulation_fields.eastward_wind_m_s.layer(0)[cell]),
                                               static_cast<double>(
                                                   circulation_fields.northward_wind_m_s.layer(0)[cell]))
                                  : 0.0) +
                        surface.gustiness_m_s;
                    bottom_mass[cell] = ps / (water_gravity * water_layers);
                    snow_share[cell] =
                        slow.land_surface_temperature_K[cell] <= melting_point_K
                            ? static_cast<double>(fractions.land_fraction[cell])
                            : 0.0;
                    const double q0 = slow.atmosphere_specific_humidity_kg_kg.layer(0)[cell];
                    // The surface air at the bottom layer's relative humidity
                    // (ADR-0021 §4.3 as amended): q_a = f q₀ with
                    // f = q_sat(A, p_s) / q_sat(T₀, p₀); each solve updates f
                    // with the column's A (CellSurface).
                    const double factor =
                        saturation_specific_humidity(air_K, ps) /
                        saturation_specific_humidity(
                            slow.atmosphere_temperature_K.layer(0)[cell],
                            layer_sigma(0U, surface.atmosphere.layer_count) * ps);
                    // The bottom layer's humidity responds within the step
                    // (backward Euler in q₀): q₀' = q₀ + Δt Σ f_t τ (q_sat − f q₀')/m₀
                    // divides every tile's transfer by 1 + f Δt Σ f_t τ / m₀.
                    // A month's τ Δt / m₀ is about 15; with q₀ held, the
                    // layer overshoots and dews it back the next month.
                    const double cell_transfer =
                        density * wind *
                        (fractions.land_fraction[cell] * land_transfer_coefficient +
                         fractions.ocean_fraction[cell] * ocean_transfer_coefficient);
                    const double response =
                        1.0 / (1.0 + factor * dt_s * cell_transfer / bottom_mass[cell]);
                    ocean_forcing.transfer_kg_m2_s =
                        density * ocean_transfer_coefficient * wind * response;
                    ocean_forcing.air_humidity = factor * q0;
                    ocean_forcing.pressure_Pa = ps;
                    ocean_forcing.vapour_kg_m2 = q0 * bottom_mass[cell];
                    // The raining branch's τ / τ_u.
                    ocean_forcing.cap_ratio = 1.0 / response;
                    land_forcing = ocean_forcing;
                    land_forcing.transfer_kg_m2_s =
                        density * land_transfer_coefficient * wind * response;
                    land_forcing.bucket_kg_m2 = slow.land_surface_water_kg_m2[cell];
                }
                land_tiles[cell] = prepare_land_tile(
                    land,
                    {slow.land_surface_temperature_K[cell], slow.land_ground_temperature_K[cell]},
                    slow.land_snow_water_equivalent_kg_m2[cell], insolation,
                    water ? 0.0 : static_cast<double>(precipitation[cell]),
                    surface.grey_emissivity, dt_s, land_forcing);
                ocean_tiles[cell] = prepare_ocean_tile(
                    ocean,
                    {slow.ocean_mixed_layer_temperature_K[cell],
                     slow.ocean_deep_temperature_K[cell]},
                    slow.sea_ice_mass_kg_m2[cell], insolation, surface.grey_emissivity, dt_s,
                    ocean_forcing);
                tile_share[cell] = static_cast<double>(fractions.land_fraction[cell]) +
                                   static_cast<double>(fractions.ocean_fraction[cell]);
                const double latitude = latitude_rad(mesh.cells()[cell].center_unit);
                const double mu = std::sin(latitude);
                p2_of_cell[cell] = 0.5 * (3.0 * mu * mu - 1.0);
                const double latitude_deg = latitude * 180.0 / std::numbers::pi;
                band_of_cell[cell] = static_cast<std::uint8_t>(
                    std::clamp(std::floor((latitude_deg + 90.0) / 10.0), 0.0, 17.0));
            }
        });
    const double exchange = surface.air_exchange_W_m2_K;
    if (!std::isfinite(exchange) || exchange < 0.0) {
        throw std::invalid_argument("air exchange must be finite and non-negative");
    }
    // ADR-0010: with a layered atmosphere every cell's tiles sit under an
    // atmospheric column that receives the transport; the grey layer is
    // retired (§4.2).
    const AtmosphereParameters& atmosphere = surface.atmosphere;
    const std::size_t layers = atmosphere.layer_count;
    if (coefficient > 0.0 && layers == 0U && !(exchange > 0.0)) {
        throw std::invalid_argument("horizontal transport needs air exchange (ADR-0009 §11)");
    }
    std::vector<ColumnRadiation> columns;
    std::vector<double> layers_before;   // cell-major, layer 0 first
    std::vector<double> layers_guess;    // first guess of each column solve, then its result
    if (layers > 0U) {
        validate_atmosphere_parameters(atmosphere);
        if (surface.grey_emissivity != 0.0) {
            throw std::invalid_argument(
                "the grey layer is retired under a layered atmosphere (ADR-0010 §4.2)");
        }
        if (slow.atmosphere_layer_count() != layers ||
            slow.atmosphere_surface_pressure_Pa.size() != cells) {
            throw std::invalid_argument(
                "the state's atmosphere has " + std::to_string(slow.atmosphere_layer_count()) +
                " layers, the surface parameters " + std::to_string(layers) +
                " (initialise_atmosphere)");
        }
        const double gravity = surface_gravity_m_s2(parameters);
        columns.resize(cells);
        layers_before.resize(cells * layers);
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    columns[cell] = column_radiation(
                        atmosphere, slow.atmosphere_surface_pressure_Pa[cell], gravity);
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        layers_before[cell * layers + layer] =
                            slow.atmosphere_temperature_K.layer(layer)[cell];
                    }
                }
            });
        layers_guess = layers_before;
    }
    std::vector<ColumnWarmStart> warm_starts(layers > 0U ? cells : 0U);
    // One cell's column under the transport source h (W/m² of cell area);
    // the solution is left in layers_guess.
    const auto solve_column = [&](std::size_t cell, double h, CellSurface& cell_surface) {
        cell_surface.land = &land_tiles[cell];
        cell_surface.ocean = &ocean_tiles[cell];
        cell_surface.land_fraction = fractions.land_fraction[cell];
        cell_surface.ocean_fraction = fractions.ocean_fraction[cell];
        cell_surface.exchange = exchange;
        ColumnMoisture moisture;
        if (water) {
            // ADR-0021 §4.4: each layer's humidity (the month's transport is
            // M7-04's), the condensate's latent heat L_v, and L_f more for
            // the share falling as snow on a land tile that starts frozen
            // (ADR-0008 §4.3), so that its freezing is not lost.
            const double ps = slow.atmosphere_surface_pressure_Pa[cell];
            for (std::size_t layer = 0; layer < layers; ++layer) {
                moisture.humidity_kg_kg[layer] =
                    slow.atmosphere_specific_humidity_kg_kg.layer(layer)[cell];
                moisture.pressure_Pa[layer] = layer_sigma(layer, layers) * ps;
            }
            moisture.layer_mass_kg_m2 = bottom_mass[cell];
            moisture.latent_J_kg = latent_heat_vaporisation_J_kg +
                                   snow_share[cell] * latent_heat_of_fusion_J_kg;
            cell_surface.water = true;
            cell_surface.bottom_humidity = moisture.humidity_kg_kg[0];
            cell_surface.bottom_pressure_Pa = moisture.pressure_Pa[0];
            cell_surface.surface_pressure_Pa = ps;
            cell_surface.air_factor = columns[cell].air_factor;
        }
        return solve_atmosphere_column(
            columns[cell], std::span<const double>(layers_before.data() + cell * layers, layers),
            dt_s, h, atmosphere.convection, cell_surface,
            std::span<double>(layers_guess.data() + cell * layers, layers), ColumnSolveSettings{},
            &warm_starts[cell], water ? &moisture : nullptr);
    };
    // The start-of-step cell temperature: the first guess of each cell solve.
    std::vector<double> first_guess(cells, 0.0);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double land_K = slow.land_surface_temperature_K[cell];
                first_guess[cell] =
                    (fractions.land_fraction[cell] * land_K +
                     fractions.ocean_fraction[cell] * slow.ocean_mixed_layer_temperature_K[cell]) /
                    tile_share[cell];
            }
        });

    // Transport runs on the mesh one level coarser (ADR-0009 §12):
    // every cell of a group receives the group's source H, which reaches each
    // tile with area as H / (f_land + f_ocean) per unit area, so the
    // tile-weighted sum is Σ A H exactly (task M4-02 §1.2).
    Field2D<double> transport(cells, 0.0);
    ImplicitTransportResult solve;
    AdvectionDiffusionResult coupled_solve;
    // The circulation carries the heat when it solved this month (ADR-0011
    // §17.1); otherwise ADR-0009's diffusion with D (§17.4).
    const bool coupled = circulation != nullptr && circulation->active && layers > 0U;
    double transport_seconds = 0.0;
    if (coefficient > 0.0 || coupled) {
        const double conductance = coefficient * mesh.radius_m() * mesh.radius_m();
        const std::vector<std::size_t>* group_map = nullptr;
        const TransportGraph& graph = agglomerated_transport_graph(mesh, group_map);
        const std::vector<std::size_t>& group_of_cell = *group_map;
        const std::size_t groups = graph.size();
        // Each group's cells in cell order.
        std::vector<std::size_t> member_offset(groups + 1U, 0U);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            ++member_offset[group_of_cell[cell] + 1U];
        }
        for (std::size_t group = 0; group < groups; ++group) {
            member_offset[group + 1U] += member_offset[group];
        }
        std::vector<std::size_t> members(cells);
        {
            std::vector<std::size_t> next(member_offset.begin(), member_offset.end() - 1);
            for (std::size_t cell = 0; cell < cells; ++cell) {
                members[next[group_of_cell[cell]]++] = cell;
            }
        }
        std::vector<double> cell_air(cells, 0.0);
        std::vector<double> cell_slope(cells, 0.0);
        const TransportResponse response = [&](const Field2D<double>& source,
                                               Field2D<double>& mean, Field2D<double>& slope) {
            for_each_deterministic_block(
                mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                    for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                        const double h = source[group_of_cell[cell]];
                        if (layers > 0U) {
                            // The diffused temperature is the column's mean
                            // θ_c (ADR-0010 §3.4 C).
                            CellSurface cell_surface;
                            const auto column = solve_column(cell, h, cell_surface);
                            cell_air[cell] = column.theta_K;
                            cell_slope[cell] = column.theta_slope_K_m2_W;
                            continue;
                        }
                        const auto tiles = solve_cell(
                            land_tiles[cell], ocean_tiles[cell],
                            fractions.land_fraction[cell], fractions.ocean_fraction[cell], h,
                            exchange, first_guess[cell], false);
                        // The diffused temperature is the air (ADR-0009 §11):
                        // from the air's balance, T_a = T̄ + h / (γ s),
                        // s = f_land + f_ocean. Its slope is never 0, so a
                        // freezing or melting cell cannot become an unlimited
                        // source or sink of transported heat.
                        const double air_share = exchange * tile_share[cell];
                        cell_air[cell] = tiles.mean_K + h / air_share;
                        cell_slope[cell] = tiles.slope_K_m2_W + 1.0 / air_share;
                        // The next response's sources are close to these: its
                        // cell solve starts here (a deterministic sequence of
                        // calls, one cell per writer).
                        first_guess[cell] = tiles.mean_K;
                    }
                });
            // A group's air is the area-weighted mean of its cells' air.
            for_each_deterministic_block(
                std::span<const CellBlock>(graph.blocks), worker_count,
                [&](std::size_t, const CellBlock& block) {
                    for (std::size_t group = block.begin; group < block.end; ++group) {
                        double air = 0.0;
                        double air_slope = 0.0;
                        for (std::size_t k = member_offset[group]; k < member_offset[group + 1U];
                             ++k) {
                            const std::size_t cell = members[k];
                            const double area = mesh.cells()[cell].area_m2;
                            air += area * cell_air[cell];
                            air_slope += area * cell_slope[cell];
                        }
                        mean[group] = air / graph.area_m2[group];
                        slope[group] = air_slope / graph.area_m2[group];
                    }
                });
        };
        // Below this source a cell's mean surface temperature would fall under
        // 100 K; a group takes the highest floor of its cells.
        Field2D<double> floor(groups, -std::numeric_limits<double>::infinity());
        for (std::size_t cell = 0; cell < cells && layers > 0U; ++cell) {
            // Under an atmosphere, the source at which the bottom layer,
            // which receives it (ADR-0010 §11), would balance at 100 K with
            // what the surface then supplies.
            CellSurface cell_surface;
            cell_surface.land = &land_tiles[cell];
            cell_surface.ocean = &ocean_tiles[cell];
            cell_surface.land_fraction = fractions.land_fraction[cell];
            cell_surface.ocean_fraction = fractions.ocean_fraction[cell];
            cell_surface.exchange = exchange;
            const double cell_floor = bottom_source_for(
                columns[cell],
                std::span<const double>(layers_before.data() + cell * layers, layers), dt_s,
                100.0, cell_surface);
            auto& group_floor = floor[group_of_cell[cell]];
            group_floor = std::max(group_floor, cell_floor);
        }
        for (std::size_t cell = 0; cell < cells && layers == 0U; ++cell) {
            constexpr double lowest_K = 100.0;
            // Only tiles with area take part; the exchange terms cancel when
            // all tiles are equal.
            const double land_fraction = fractions.land_fraction[cell];
            const double ocean_fraction = fractions.ocean_fraction[cell];
            const double land_floor =
                land_fraction > 0.0 ? -land_tiles[cell].system.surplus_W_m2(lowest_K) : 0.0;
            const double ocean_floor =
                ocean_fraction > 0.0 ? ocean_tile_source_floor_W_m2(ocean_tiles[cell], lowest_K)
                                     : 0.0;
            const double cell_floor = land_fraction * land_floor + ocean_fraction * ocean_floor;
            auto& group_floor = floor[group_of_cell[cell]];
            group_floor = std::max(group_floor, cell_floor);
        }
        if (coupled) {
            // The coupled response: θ_c as above, and each group's layer dry
            // static energy, mass-weighted over its cells, with its slope.
            const auto& phi_s = circulation->surface_geopotential_m2_s2;
            if (phi_s.size() != cells ||
                circulation->transport.conductance_W_K.size() != graph.neighbour.size() ||
                circulation->transport.layers != layers) {
                throw std::invalid_argument("the circulation's transport does not match the step");
            }
            std::vector<double> cell_energy(layers * cells, 0.0);
            std::vector<double> cell_energy_slope(layers * cells, 0.0);
            const AdvectionResponse coupled_response =
                [&](const Field2D<double>& source, Field2D<double>& mean, Field2D<double>& slope,
                    std::vector<double>& energy, std::vector<double>& energy_slope) {
                    mean = Field2D<double>(groups, 0.0);
                    slope = Field2D<double>(groups, 0.0);
                    energy.assign(layers * groups, 0.0);
                    energy_slope.assign(layers * groups, 0.0);
                    for_each_deterministic_block(
                        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                            LayerArray s{};
                            LayerArray ds{};
                            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                                CellSurface cell_surface;
                                const auto column =
                                    solve_column(cell, source[group_of_cell[cell]], cell_surface);
                                cell_air[cell] = column.theta_K;
                                cell_slope[cell] = column.theta_slope_K_m2_W;
                                layer_dry_static_energy(
                                    phi_s[cell],
                                    std::span<const double>(layers_guess.data() + cell * layers,
                                                            layers),
                                    std::span<double>(s.data(), layers));
                                layer_dry_static_energy(
                                    0.0,
                                    std::span<const double>(
                                        column.temperature_slope_K_m2_W.data(), layers),
                                    std::span<double>(ds.data(), layers));
                                for (std::size_t l = 0; l < layers; ++l) {
                                    cell_energy[l * cells + cell] = s[l];
                                    cell_energy_slope[l * cells + cell] = ds[l];
                                }
                            }
                        });
                    for_each_deterministic_block(
                        std::span<const CellBlock>(graph.blocks), worker_count,
                        [&](std::size_t, const CellBlock& block) {
                            for (std::size_t group = block.begin; group < block.end; ++group) {
                                double air = 0.0;
                                double air_slope = 0.0;
                                double column_mass = 0.0;
                                for (std::size_t k = member_offset[group];
                                     k < member_offset[group + 1U]; ++k) {
                                    const std::size_t cell = members[k];
                                    const double area = mesh.cells()[cell].area_m2;
                                    air += area * cell_air[cell];
                                    air_slope += area * cell_slope[cell];
                                    column_mass += area * slow.atmosphere_surface_pressure_Pa[cell];
                                }
                                mean[group] = air / graph.area_m2[group];
                                slope[group] = air_slope / graph.area_m2[group];
                                for (std::size_t l = 0; l < layers; ++l) {
                                    double sum = 0.0;
                                    double sum_slope = 0.0;
                                    for (std::size_t k = member_offset[group];
                                         k < member_offset[group + 1U]; ++k) {
                                        const std::size_t cell = members[k];
                                        const double weight =
                                            mesh.cells()[cell].area_m2 *
                                            slow.atmosphere_surface_pressure_Pa[cell];
                                        sum += weight * cell_energy[l * cells + cell];
                                        sum_slope += weight * cell_energy_slope[l * cells + cell];
                                    }
                                    energy[l * groups + group] = sum / column_mass;
                                    energy_slope[l * groups + group] = sum_slope / column_mass;
                                }
                            }
                        });
                };
            const auto solve_start = std::chrono::steady_clock::now();
            // The coupled Newton stops at 1e-4 W/m² (ADR-0009 §13): the
            // transport applied is the conservative H of its last iterate,
            // so energy closes whatever the residual, and the last two
            // passes over every column (1e-4 → 1e-6) cost a tenth of an L6
            // climate step.
            ImplicitTransportSettings coupled_settings;
            coupled_settings.newton_tolerance_W_m2 = coupled_newton_tolerance_W_m2;
            // An inexact Newton: each linear solve to 1e-3 of its residual.
            coupled_settings.cg_relative_tolerance = 1e-3;
            coupled_solve = solve_implicit_advection_diffusion(
                graph, circulation->transport, floor, coupled_response, coupled_settings,
                worker_count);
            transport_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - solve_start)
                                    .count();
            for (std::size_t cell = 0; cell < cells; ++cell) {
                transport[cell] = coupled_solve.source_W_m2[group_of_cell[cell]];
            }
        } else {
            const auto solve_start = std::chrono::steady_clock::now();
            solve = solve_implicit_transport(graph, conductance, floor, response, {},
                                             worker_count);
            transport_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - solve_start)
                                    .count();
            for (std::size_t cell = 0; cell < cells; ++cell) {
                transport[cell] = solve.source_W_m2[group_of_cell[cell]];
            }
        }
    }

    const BudgetPartial total = reduce_deterministic_blocks<BudgetPartial>(
        mesh.blocks(), worker_count, BudgetPartial{},
        [&](std::size_t, const CellBlock& block) {
            BudgetPartial partial;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double area_m2 = mesh.cells()[cell].area_m2;
                const double snow = slow.land_snow_water_equivalent_kg_m2[cell];
                const bool to_space = layers == 0U;
                CellTiles tiles;
                double precipitation_kg_m2 = 0.0;
                double condensation_W_m2 = 0.0;
                double cell_evaporation_kg_m2_s = 0.0;
                LayerArray humidity_after{};
                if (layers > 0U) {
                    CellSurface cell_surface;
                    const auto column = solve_column(cell, transport[cell], cell_surface);
                    tiles.land = cell_surface.land_result;
                    tiles.ocean = cell_surface.ocean_result;
                    // A tile without area follows the surface air.
                    const double air_K = column.air_K;
                    const double down = column.downward_surface_W_m2;
                    if (!(fractions.land_fraction[cell] > 0.0)) {
                        tiles.land = solve_land_tile(
                            land_tiles[cell],
                            land_tiles[cell].column.emissivity * down + exchange * air_K,
                            exchange);
                    }
                    if (!(fractions.ocean_fraction[cell] > 0.0)) {
                        tiles.ocean = solve_ocean_tile(
                            ocean_tiles[cell],
                            ocean_tiles[cell].column.emissivity * down + exchange * air_K,
                            exchange);
                    }
                    if (water) {
                        // The column's rain reaches the surface at the end of
                        // the step: snow on land that started frozen, else
                        // rain into the bucket; on the ocean it is the
                        // ocean's.
                        precipitation_kg_m2 = column.precipitation_kg_m2_s * dt_s;
                        condensation_W_m2 = column.condensation_W_m2;
                        cell_evaporation_kg_m2_s = column.evaporation_kg_m2_s;
                        humidity_after = column.humidity_kg_kg;
                        if (snow_share[cell] > 0.0) {
                            tiles.land.snowfall_kg_m2 += precipitation_kg_m2;
                            tiles.land.snow_kg_m2 += precipitation_kg_m2;
                        } else {
                            tiles.land.rain_kg_m2 += precipitation_kg_m2;
                        }
                    }
                    const ColumnRadiation& radiation = columns[cell];
                    double change = 0.0;
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        const double after = layers_guess[cell * layers + layer];
                        change += after - layers_before[cell * layers + layer];
                        partial.stored_energy_J +=
                            area_m2 * radiation.layer_heat_capacity_J_m2_K * after;
                        partial.layer_temperature_K_m2[layer] += area_m2 * after;
                        slow.atmosphere_temperature_K.layer(layer)[cell] = after;
                    }
                    const double storage_J =
                        area_m2 * radiation.layer_heat_capacity_J_m2_K * change;
                    partial.storage_change_J += storage_J;
                    partial.atmosphere_storage_change_J += storage_J;
                    partial.emitted_W += area_m2 * column.outgoing_W_m2;
                    partial.transport_W += area_m2 * transport[cell];
                    partial.surface_upward_W += area_m2 * column.upward_surface_W_m2;
                    partial.downward_W += area_m2 * down;
                    partial.sensible_W += area_m2 * column.sensible_W_m2;
                    partial.max_column_residual_W_m2 =
                        std::max(partial.max_column_residual_W_m2, column.max_residual_W_m2);
                    partial.max_column_correction_K =
                        std::max(partial.max_column_correction_K, column.max_correction_K);
                    partial.max_column_iterations =
                        std::max(partial.max_column_iterations, column.iterations);
                    partial.unconverged_columns += column.converged ? 0U : 1U;
                    if (column.adjusted) {
                        partial.convective_area_m2 += area_m2;
                    }
                } else {
                    tiles = solve_cell(land_tiles[cell], ocean_tiles[cell],
                                       fractions.land_fraction[cell],
                                       fractions.ocean_fraction[cell], transport[cell], exchange,
                                       first_guess[cell], true);
                }
                const LandSnowStepResult& land_tile = tiles.land;
                const ColumnStepResult& land_step = land_tile.column;
                const OceanTileResult& ocean_tile = tiles.ocean;
                const ColumnStepResult& ocean_step = ocean_tile.column;
                const double ocean_K = ocean_tile.radiating_K;
                const double land_weight = area_m2 * fractions.land_fraction[cell];
                const double ocean_weight = area_m2 * fractions.ocean_fraction[cell];
                accumulate_tile(partial, land, land_step, land_step.state.surface_K, land_weight,
                                to_space);
                accumulate_tile(partial, ocean_tiles[cell].column, ocean_step, ocean_K,
                                ocean_weight, to_space);
                if (land_weight > 0.0) {
                    partial.land_temperature_K_m2 += land_weight * land_step.state.surface_K;
                    partial.land_area_m2 += land_weight;
                    partial.latent_heat_J += land_weight * land_tile.latent_J_m2;
                    partial.snowfall_kg += land_weight * land_tile.snowfall_kg_m2;
                    partial.rain_kg += land_weight * land_tile.rain_kg_m2;
                    partial.melt_kg += land_weight * land_tile.melt_kg_m2;
                    partial.snow_change_kg += land_weight * (land_tile.snow_kg_m2 - snow);
                    partial.snow_kg += land_weight * land_tile.snow_kg_m2;
                    const bool north = mesh.cells()[cell].center_unit.z > 0.0;
                    if (land_tile.snow_kg_m2 > 0.0) {
                        (north ? partial.snow_area_north_m2 : partial.snow_area_south_m2) +=
                            land_weight;
                    }
                    (north ? partial.snow_cover_north_m2 : partial.snow_cover_south_m2) +=
                        land_weight * snow_cover_fraction(land_tile.snow_kg_m2);
                }
                if (ocean_weight > 0.0) {
                    const double ice = slow.sea_ice_mass_kg_m2[cell];
                    partial.ocean_temperature_K_m2 += ocean_weight * ocean_K;
                    partial.ocean_area_m2 += ocean_weight;
                    partial.latent_heat_J += ocean_weight * ocean_tile.latent_J_m2;
                    partial.ice_frozen_kg += ocean_weight * ocean_tile.frozen_kg_m2;
                    partial.ice_melted_kg += ocean_weight * ocean_tile.melted_kg_m2;
                    partial.ice_change_kg += ocean_weight * (ocean_tile.ice_kg_m2 - ice);
                    partial.ice_kg += ocean_weight * ocean_tile.ice_kg_m2;
                    (mesh.cells()[cell].center_unit.z > 0.0 ? partial.ice_cover_north_m2
                                                            : partial.ice_cover_south_m2) +=
                        ocean_weight * sea_ice_cover_fraction(ocean_tile.ice_kg_m2);
                    if (ocean_tile.ice_kg_m2 > 0.0) {
                        const bool north = mesh.cells()[cell].center_unit.z > 0.0;
                        (north ? partial.ice_area_north_m2 : partial.ice_area_south_m2) +=
                            ocean_weight;
                        (north ? partial.ice_mass_north_kg : partial.ice_mass_south_kg) +=
                            ocean_weight * ocean_tile.ice_kg_m2;
                    }
                }
                const std::size_t band = band_of_cell[cell];
                partial.band_temperature_K_m2[band] +=
                    land_weight * land_step.state.surface_K + ocean_weight * ocean_K;
                partial.band_area_m2[band] += land_weight + ocean_weight;
                const double p2 = p2_of_cell[cell];
                partial.p2_temperature_K_m2 +=
                    p2 * (land_weight * land_step.state.surface_K + ocean_weight * ocean_K);
                partial.p2_squared_m2 += p2 * p2 * (land_weight + ocean_weight);
                if (water) {
                    // The water's update (ADR-0021 §4.3): the bucket takes rain
                    // and meltwater, loses its evaporation and overflows to
                    // the ocean; the bottom layer takes the cell's
                    // evaporation.
                    const double bucket = slow.land_surface_water_kg_m2[cell];
                    double after = bucket + land_tile.rain_kg_m2 + land_tile.melt_kg_m2 -
                                   land_tile.bucket_evaporation_kg_m2;
                    const double overflow = std::max(0.0, after - bucket_capacity_kg_m2);
                    after = std::max(0.0, after - overflow);
                    slow.land_surface_water_kg_m2[cell] = after;
                    const double land_share = fractions.land_fraction[cell];
                    // The layers' humidity after evaporation and rainout.
                    double vapour_change = 0.0;
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        auto& q = slow.atmosphere_specific_humidity_kg_kg.layer(layer)[cell];
                        vapour_change += humidity_after[layer] - q;
                        q = humidity_after[layer];
                    }
                    state.forcing().evaporation_kg_m2_s[cell] =
                        static_cast<float>(cell_evaporation_kg_m2_s);
                    state.forcing().precipitation_kg_m2_s[cell] =
                        static_cast<float>(precipitation_kg_m2 / dt_s);
                    state.forcing().runoff_kg_m2_s[cell] =
                        static_cast<float>(land_share * overflow / dt_s);
                    partial.precipitation_kg += area_m2 * precipitation_kg_m2;
                    partial.ocean_precipitation_kg += (area_m2 - land_weight) * precipitation_kg_m2;
                    partial.condensation_latent_J += area_m2 * dt_s * condensation_W_m2;
                    partial.water_evaporation_kg += ocean_weight * ocean_tile.evaporation_kg_m2;
                    partial.bucket_evaporation_kg +=
                        land_weight * land_tile.bucket_evaporation_kg_m2;
                    partial.snow_sublimation_kg += land_weight * land_tile.sublimation_kg_m2;
                    partial.ice_sublimation_kg += ocean_weight * ocean_tile.sublimation_kg_m2;
                    partial.bucket_runoff_kg += land_weight * overflow;
                    partial.vapour_change_kg += area_m2 * bottom_mass[cell] * vapour_change;
                    partial.bucket_change_kg += land_weight * (after - bucket);
                    partial.bucket_kg += land_weight * after;
                    partial.evaporation_latent_J +=
                        dt_s * (land_weight * land_step.evaporation_W_m2 +
                                ocean_weight * ocean_step.evaporation_W_m2);
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        partial.vapour_kg += area_m2 * bottom_mass[cell] *
                                             slow.atmosphere_specific_humidity_kg_kg.layer(layer)[cell];
                    }
                }
                slow.land_surface_temperature_K[cell] =
                    static_cast<float>(land_step.state.surface_K);
                slow.land_ground_temperature_K[cell] = static_cast<float>(land_step.state.lower_K);
                slow.land_snow_water_equivalent_kg_m2[cell] = land_tile.snow_kg_m2;
                slow.ocean_mixed_layer_temperature_K[cell] = ocean_step.state.surface_K;
                slow.ocean_deep_temperature_K[cell] = ocean_step.state.lower_K;
                slow.sea_ice_mass_kg_m2[cell] = ocean_tile.ice_kg_m2;
                state.forcing().surface_temperature_K[cell] = static_cast<float>(
                    (fractions.land_fraction[cell] * land_step.state.surface_K +
                     fractions.ocean_fraction[cell] * ocean_K) /
                    tile_share[cell]);
            }
            return partial;
        },
        combine);

    SurfaceEnergyDiagnostics diagnostics;
    diagnostics.duration_s = dt_s;
    diagnostics.absorbed_W = total.absorbed_W;
    diagnostics.emitted_W = total.emitted_W;
    diagnostics.storage_change_J = total.storage_change_J;
    diagnostics.stored_energy_J = total.stored_energy_J;
    diagnostics.latent_heat_J = total.latent_heat_J;
    diagnostics.transport_W = total.transport_W;
    diagnostics.p2_surface_temperature_K = total.p2_temperature_K_m2 / total.p2_squared_m2;
    for (std::size_t band = 0; band < total.band_area_m2.size(); ++band) {
        diagnostics.zonal_mean_surface_temperature_K[band] =
            total.band_area_m2[band] > 0.0
                ? total.band_temperature_K_m2[band] / total.band_area_m2[band]
                : 0.0;
    }
    diagnostics.transport_solve_s = transport_seconds;
    if (coupled) {
        diagnostics.transport_coupled = true;
        diagnostics.transport_cell_sum_W = coupled_solve.sum_W;
        diagnostics.transport_absolute_W = coupled_solve.absolute_sum_W;
        diagnostics.transport_consistency_W_m2 = coupled_solve.consistency_residual_W_m2;
        diagnostics.transport_newton_iterations = coupled_solve.newton_iterations;
        diagnostics.transport_cg_iterations = coupled_solve.linear_iterations;
        diagnostics.transport_eddy_absolute_W = coupled_solve.eddy_absolute_sum_W;
        diagnostics.transport_advective_absolute_W = coupled_solve.advective_absolute_sum_W;
    } else {
        diagnostics.transport_cell_sum_W = solve.sum_W;
        diagnostics.transport_absolute_W = solve.absolute_sum_W;
        diagnostics.transport_dissipation_W_K = solve.dissipation_W_K;
        diagnostics.transport_consistency_W_m2 = solve.consistency_residual_W_m2;
        diagnostics.transport_newton_iterations = solve.newton_iterations;
        diagnostics.transport_cg_iterations = solve.cg_iterations;
    }
    if (coefficient > 0.0) {
        // Heat delivered to each 10° band, then summed north of each
        // boundary: the northward transport across it.
        std::array<double, 18> delivered{};
        for (std::size_t cell = 0; cell < cells; ++cell) {
            delivered[band_of_cell[cell]] += mesh.cells()[cell].area_m2 * transport[cell];
        }
        for (std::size_t boundary = 0; boundary < diagnostics.northward_transport_W.size();
             ++boundary) {
            double north = 0.0;
            for (std::size_t band = boundary + 1U; band < delivered.size(); ++band) {
                north += delivered[band];
            }
            diagnostics.northward_transport_W[boundary] = north;
        }
    }
    diagnostics.snowfall_kg = total.snowfall_kg;
    diagnostics.rain_kg = total.rain_kg;
    diagnostics.melt_kg = total.melt_kg;
    diagnostics.snow_change_kg = total.snow_change_kg;
    diagnostics.snow_kg = total.snow_kg;
    diagnostics.ice_frozen_kg = total.ice_frozen_kg;
    diagnostics.ice_melted_kg = total.ice_melted_kg;
    diagnostics.water_evaporation_kg = total.water_evaporation_kg;
    diagnostics.bucket_evaporation_kg = total.bucket_evaporation_kg;
    diagnostics.snow_sublimation_kg = total.snow_sublimation_kg;
    diagnostics.ice_sublimation_kg = total.ice_sublimation_kg;
    diagnostics.bucket_runoff_kg = total.bucket_runoff_kg;
    diagnostics.vapour_change_kg = total.vapour_change_kg;
    diagnostics.bucket_change_kg = total.bucket_change_kg;
    diagnostics.bucket_kg = total.bucket_kg;
    diagnostics.vapour_kg = total.vapour_kg;
    diagnostics.evaporation_latent_J = total.evaporation_latent_J;
    diagnostics.precipitation_kg = total.precipitation_kg;
    diagnostics.humidity_transport = humidity_transport;
    diagnostics.ocean_precipitation_kg = total.ocean_precipitation_kg;
    diagnostics.condensation_latent_J = total.condensation_latent_J;
    diagnostics.ice_change_kg = total.ice_change_kg;
    diagnostics.ice_kg = total.ice_kg;
    diagnostics.ice_area_north_m2 = total.ice_area_north_m2;
    diagnostics.ice_area_south_m2 = total.ice_area_south_m2;
    diagnostics.ice_mass_north_kg = total.ice_mass_north_kg;
    diagnostics.ice_mass_south_kg = total.ice_mass_south_kg;
    diagnostics.snow_area_north_m2 = total.snow_area_north_m2;
    diagnostics.snow_area_south_m2 = total.snow_area_south_m2;
    diagnostics.ice_cover_north_m2 = total.ice_cover_north_m2;
    diagnostics.ice_cover_south_m2 = total.ice_cover_south_m2;
    diagnostics.snow_cover_north_m2 = total.snow_cover_north_m2;
    diagnostics.snow_cover_south_m2 = total.snow_cover_south_m2;
    diagnostics.max_newton_residual_W_m2 = total.max_newton_residual_W_m2;
    diagnostics.mean_surface_temperature_K = total.weighted_temperature_K_m2 / total.area_m2;
    diagnostics.land_mean_surface_temperature_K =
        total.land_area_m2 > 0.0 ? total.land_temperature_K_m2 / total.land_area_m2 : 0.0;
    diagnostics.ocean_mean_surface_temperature_K =
        total.ocean_area_m2 > 0.0 ? total.ocean_temperature_K_m2 / total.ocean_area_m2 : 0.0;
    diagnostics.min_surface_temperature_K = total.min_K;
    diagnostics.max_surface_temperature_K = total.max_K;
    diagnostics.atmosphere_layers = layers;
    diagnostics.atmosphere_storage_change_J = total.atmosphere_storage_change_J;
    diagnostics.surface_upward_longwave_W = total.surface_upward_W;
    diagnostics.downward_longwave_W = total.downward_W;
    diagnostics.sensible_heat_W = total.sensible_W;
    diagnostics.max_column_residual_W_m2 = total.max_column_residual_W_m2;
    diagnostics.max_column_correction_K = total.max_column_correction_K;
    diagnostics.max_column_iterations = total.max_column_iterations;
    diagnostics.unconverged_columns = total.unconverged_columns;
    diagnostics.convective_area_m2 = total.convective_area_m2;
    double planet_area_m2 = 0.0;
    for (std::size_t cell = 0; cell < cells && layers > 0U; ++cell) {
        planet_area_m2 += mesh.cells()[cell].area_m2;
    }
    for (std::size_t layer = 0; layer < layers; ++layer) {
        diagnostics.mean_layer_temperature_K[layer] =
            total.layer_temperature_K_m2[layer] / planet_area_m2;
    }
    return diagnostics;
}

void register_surface_energy(Scheduler& scheduler, PlanetState& state,
                             const PlanetParameters& parameters,
                             const SurfaceEnergyParameters& surface,
                             const SurfaceFractions& fractions, std::size_t worker_count,
                             SurfaceEnergyDiagnostics* last, bool reference_diffusion,
                             const CirculationTransport* circulation) {
    SurfaceEnergyParameters reference_surface = surface;
    if (!reference_diffusion) {
        reference_surface.transport_coefficient_W_m2_K = 0.0;
    }
    scheduler.register_process(
        {"surface_energy_climate", SimulationMode::climate, 0},
        [&state, &parameters, surface, &fractions, worker_count, last,
         circulation](const StepContext& context) {
            // A coupled circulation, registered first, has already set it.
            if (circulation == nullptr) {
                update_substep_mean_insolation(state, parameters, *context.substep,
                                               worker_count);
            }
            const auto diagnostics = step_surface_energy(
                state, parameters, surface, fractions,
                state.forcing().substep_mean_insolation_W_m2,
                simulation_time_s(context.length_ticks()), worker_count, circulation);
            accumulate_climatology(state, context.substep->index, worker_count);
            if (last != nullptr) {
                *last = diagnostics;
            }
        });
    scheduler.register_process(
        {"surface_energy_reference", SimulationMode::reference, 0},
        [&state, &parameters, reference_surface, &fractions, worker_count,
         last](const StepContext& context) {
            update_solar_forcing(state, parameters,
                                 context.begin_tick + context.length_ticks() / 2, worker_count);
            const auto diagnostics = step_surface_energy(
                state, parameters, reference_surface, fractions,
                state.forcing().top_of_atmosphere_insolation_W_m2,
                simulation_time_s(context.length_ticks()), worker_count);
            if (last != nullptr) {
                *last = diagnostics;
            }
        });
}

AnnualSurfaceSummary spin_up_surface_energy(PlanetState& state,
                                            const PlanetParameters& parameters,
                                            const SurfaceEnergyParameters& surface,
                                            const SurfaceFractions& fractions, int years,
                                            std::size_t worker_count,
                                            const SpinUpCirculation& circulation) {
    if (years <= 0) {
        throw std::invalid_argument("spin-up needs at least one year");
    }
    AnnualSurfaceSummary summary;
    for (int year = 0; year < years; ++year) {
        summary = {};
        double total_s = 0.0;
        for (std::int64_t month = 0; month < climate_substeps_per_year; ++month) {
            const ClimateSubstep substep =
                climate_substep(year * climate_substeps_per_year + month, parameters);
            update_substep_mean_insolation(state, parameters, substep, worker_count);
            const CirculationTransport* transport =
                circulation ? circulation(state, substep) : nullptr;
            const double dt_s = simulation_time_s(substep.length_ticks());
            const auto step = step_surface_energy(state, parameters, surface, fractions,
                                                  state.forcing().substep_mean_insolation_W_m2,
                                                  dt_s, worker_count, transport);
            summary.absorbed_W += dt_s * step.absorbed_W;
            summary.emitted_W += dt_s * step.emitted_W;
            summary.mean_surface_temperature_K += dt_s * step.mean_surface_temperature_K;
            summary.land_mean_surface_temperature_K += dt_s * step.land_mean_surface_temperature_K;
            summary.ocean_mean_surface_temperature_K +=
                dt_s * step.ocean_mean_surface_temperature_K;
            summary.latent_W += step.latent_heat_J;
            summary.snowfall_kg += step.snowfall_kg;
            summary.rain_kg += step.rain_kg;
            summary.melt_kg += step.melt_kg;
            summary.snow_kg = step.snow_kg;
            summary.ice_kg = step.ice_kg;
            if (month == 0) {
                summary.ice_area_north_max_m2 = summary.ice_area_north_min_m2 =
                    step.ice_area_north_m2;
                summary.ice_area_south_max_m2 = summary.ice_area_south_min_m2 =
                    step.ice_area_south_m2;
                summary.snow_area_north_max_m2 = summary.snow_area_north_min_m2 =
                    step.snow_area_north_m2;
                summary.snow_area_south_max_m2 = summary.snow_area_south_min_m2 =
                    step.snow_area_south_m2;
            }
            const std::array<double, 4> cover{step.ice_cover_north_m2, step.ice_cover_south_m2,
                                              step.snow_cover_north_m2, step.snow_cover_south_m2};
            for (std::size_t index = 0; index < cover.size(); ++index) {
                summary.cover_max_m2[index] =
                    month == 0 ? cover[index] : std::max(summary.cover_max_m2[index], cover[index]);
                summary.cover_min_m2[index] =
                    month == 0 ? cover[index] : std::min(summary.cover_min_m2[index], cover[index]);
            }
            summary.snow_area_north_max_m2 =
                std::max(summary.snow_area_north_max_m2, step.snow_area_north_m2);
            summary.snow_area_north_min_m2 =
                std::min(summary.snow_area_north_min_m2, step.snow_area_north_m2);
            summary.snow_area_south_max_m2 =
                std::max(summary.snow_area_south_max_m2, step.snow_area_south_m2);
            summary.snow_area_south_min_m2 =
                std::min(summary.snow_area_south_min_m2, step.snow_area_south_m2);
            summary.ice_area_north_max_m2 =
                std::max(summary.ice_area_north_max_m2, step.ice_area_north_m2);
            summary.ice_area_north_min_m2 =
                std::min(summary.ice_area_north_min_m2, step.ice_area_north_m2);
            summary.ice_area_south_max_m2 =
                std::max(summary.ice_area_south_max_m2, step.ice_area_south_m2);
            summary.ice_area_south_min_m2 =
                std::min(summary.ice_area_south_min_m2, step.ice_area_south_m2);
            summary.worst_closure_ratio = std::max(
                summary.worst_closure_ratio, step.closure_residual_J() / step.closure_gate_J());
            for (std::size_t layer = 0; layer < step.atmosphere_layers; ++layer) {
                summary.mean_layer_temperature_K[layer] +=
                    dt_s * step.mean_layer_temperature_K[layer];
            }
            summary.surface_upward_longwave_W += dt_s * step.surface_upward_longwave_W;
            summary.downward_longwave_W += dt_s * step.downward_longwave_W;
            summary.sensible_heat_W += dt_s * step.sensible_heat_W;
            summary.convective_area_fraction +=
                dt_s * step.convective_area_m2 / (4.0 * std::numbers::pi * state.mesh().radius_m() *
                                                  state.mesh().radius_m());
            summary.max_column_residual_W_m2 =
                std::max(summary.max_column_residual_W_m2, step.max_column_residual_W_m2);
            summary.max_column_correction_K =
                std::max(summary.max_column_correction_K, step.max_column_correction_K);
            summary.max_column_iterations =
                std::max(summary.max_column_iterations, step.max_column_iterations);
            summary.unconverged_columns += step.unconverged_columns;
            summary.p2_surface_temperature_K += dt_s * step.p2_surface_temperature_K;
            for (std::size_t band = 0; band < summary.northward_transport_W.size(); ++band) {
                summary.northward_transport_W[band] += dt_s * step.northward_transport_W[band];
            }
            for (std::size_t band = 0; band < summary.zonal_mean_surface_temperature_K.size();
                 ++band) {
                summary.zonal_mean_surface_temperature_K[band] +=
                    dt_s * step.zonal_mean_surface_temperature_K[band];
            }
            total_s += dt_s;
        }
        summary.absorbed_W /= total_s;
        summary.latent_W /= total_s;
        summary.emitted_W /= total_s;
        summary.mean_surface_temperature_K /= total_s;
        summary.land_mean_surface_temperature_K /= total_s;
        summary.ocean_mean_surface_temperature_K /= total_s;
        summary.p2_surface_temperature_K /= total_s;
        for (auto& value : summary.mean_layer_temperature_K) {
            value /= total_s;
        }
        summary.surface_upward_longwave_W /= total_s;
        summary.downward_longwave_W /= total_s;
        summary.sensible_heat_W /= total_s;
        summary.convective_area_fraction /= total_s;
        for (auto& value : summary.northward_transport_W) {
            value /= total_s;
        }
        for (auto& value : summary.zonal_mean_surface_temperature_K) {
            value /= total_s;
        }
    }
    return summary;
}


void compute_atmosphere_heating(const PlanetState& state, const PlanetParameters& parameters,
                                const SurfaceEnergyParameters& surface,
                                const SurfaceFractions& fractions,
                                const Field2D<float>& insolation_W_m2,
                                AtmosphereHeating& heating, std::size_t worker_count) {
    constexpr double dt_s = 60.0;   // the held surface's step
    const PlanetMesh& mesh = state.mesh();
    const SlowState& slow = state.slow();
    const std::size_t cells = mesh.cell_count();
    const AtmosphereParameters& atmosphere = surface.atmosphere;
    const std::size_t layers = atmosphere.layer_count;
    if (layers == 0U || slow.atmosphere_layer_count() != layers) {
        throw std::invalid_argument("atmosphere heating needs the state's atmosphere");
    }
    validate_atmosphere_parameters(atmosphere);
    const Field2D<float>& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
    if (insolation_W_m2.size() != cells || fractions.land_fraction.size() != cells ||
        precipitation.size() != cells) {
        throw std::invalid_argument("atmosphere heating inputs do not match the mesh");
    }
    const ColumnProperties land = column_properties(surface.land_material, parameters);
    const ColumnProperties ocean = column_properties(SurfaceMaterial::ocean, parameters);
    const double gravity = surface_gravity_m_s2(parameters);
    const double exchange = surface.air_exchange_W_m2_K;
    heating.rate_K_s = Field3D<double>(layers, cells);
    heating.derivative_s = Field3D<double>(layers, cells);
    heating.outgoing_W_m2 = Field2D<double>(cells);
    heating.surface_upward_W_m2 = Field2D<double>(cells);
    heating.surface_downward_W_m2 = Field2D<double>(cells);
    heating.sensible_W_m2 = Field2D<double>(cells);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double insolation = insolation_W_m2[cell];
                const LandSnowSystem land_tile = prepare_land_tile(
                    land,
                    {slow.land_surface_temperature_K[cell], slow.land_ground_temperature_K[cell]},
                    slow.land_snow_water_equivalent_kg_m2[cell], insolation, precipitation[cell],
                    surface.grey_emissivity, dt_s);
                const OceanTileSystem ocean_tile = prepare_ocean_tile(
                    ocean,
                    {slow.ocean_mixed_layer_temperature_K[cell],
                     slow.ocean_deep_temperature_K[cell]},
                    slow.sea_ice_mass_kg_m2[cell], insolation, surface.grey_emissivity, dt_s);
                CellSurface cell_surface;
                cell_surface.land = &land_tile;
                cell_surface.ocean = &ocean_tile;
                cell_surface.land_fraction = fractions.land_fraction[cell];
                cell_surface.ocean_fraction = fractions.ocean_fraction[cell];
                cell_surface.exchange = exchange;

                const ColumnRadiation column =
                    column_radiation(atmosphere, slow.atmosphere_surface_pressure_Pa[cell], gravity);
                LayerArray temperature{};
                LayerArray emission{};
                for (std::size_t k = 0; k < layers; ++k) {
                    temperature[k] = slow.atmosphere_temperature_K.layer(k)[cell];
                    const double t2 = temperature[k] * temperature[k];
                    emission[k] = column.emissivity[k] * column_stefan_boltzmann_W_m2_K4 * t2 * t2;
                }
                const double down = downward_surface_longwave(column, emission);
                const double air_K = column.air_factor * temperature[0];
                const SurfaceExchange surface_exchange = cell_surface(down, air_K);
                const LongwaveFluxes fluxes =
                    longwave_fluxes(column, emission, surface_exchange.upward_W_m2);

                // The held surface reflects (1 − ε_t) of D on its tiles and
                // all of it where no tile has area.
                const double land_fraction = fractions.land_fraction[cell];
                const double ocean_fraction = fractions.ocean_fraction[cell];
                const double reflectivity =
                    1.0 - land_fraction * land_tile.column.emissivity -
                    ocean_fraction * ocean_tile.column.emissivity;
                const double capacity = column.layer_heat_capacity_J_m2_K;
                for (std::size_t k = 0; k < layers; ++k) {
                    double q = fluxes.absorbed_in[k] - 2.0 * emission[k];
                    if (k == 0U) {
                        q += surface_exchange.sensible_W_m2;
                    }
                    heating.rate_K_s.layer(k)[cell] = q / capacity;
                    LayerArray d_emission{};
                    d_emission[k] = 4.0 * emission[k] / temperature[k];
                    const double d_down = downward_surface_longwave(column, d_emission);
                    const LongwaveFluxes d_fluxes =
                        longwave_fluxes(column, d_emission, reflectivity * d_down);
                    double dq = d_fluxes.absorbed_in[k] - 2.0 * d_emission[k];
                    if (k == 0U) {
                        dq -= exchange * (land_fraction + ocean_fraction) * column.air_factor;
                    }
                    heating.derivative_s.layer(k)[cell] = dq / capacity;
                }
                heating.outgoing_W_m2[cell] = fluxes.outgoing_W_m2;
                heating.surface_upward_W_m2[cell] = surface_exchange.upward_W_m2;
                heating.surface_downward_W_m2[cell] = fluxes.downward_surface_W_m2;
                heating.sensible_W_m2[cell] = surface_exchange.sensible_W_m2;
            }
        });
}

}  // namespace planetsim
