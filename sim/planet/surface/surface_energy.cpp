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
#include <optional>
#include <span>
#include <utility>
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
    case PlanetPreset::earth_like: {
        SurfaceEnergyParameters surface{SurfaceMaterial::dry_soil, 0.0,
                                        earth_like_transport_coefficient_W_m2_K,
                                        bulk_air_exchange_W_m2_K, atmosphere_parameters_for(preset)};
        surface.water_cycle = true;   // ADR-0021, since the M7-07 refit
        surface.transport_fallback_residual_W_m2 = 100.0;   // ADR-0024 §9, R_f
        surface.heat_group_mixing = 1.0;                    // ADR-0027, m
        surface.equatorial_wave_speed_m_s = 50.0;           // ADR-0028, c
        return surface;
    }
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
    double cloud_cover_m2 = 0.0;
    double high_cloud_cover_m2 = 0.0;
    double cloud_optical_depth_m2 = 0.0;
    std::array<SurfaceEnergyDiagnostics::VapourBudget, 6> vapour_budget{};
    std::array<double, 18> band_cloud_m2{};
    std::array<double, 18> band_cell_area_m2{};
    double cloud_shortwave_effect_W = 0.0;
    double cloud_longwave_effect_W = 0.0;
    double cloud_water_change_kg = 0.0;
    double cloud_water_kg = 0.0;
    double convective_precipitation_kg = 0.0;
    double detrained_kg = 0.0;
    double cloud_evaporation_kg = 0.0;
    double rain_evaporation_kg = 0.0;
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
    // INV-04: the land tiles' budget and each surface's 10° bands.
    double land_absorbed_W = 0.0;
    double land_source_W = 0.0;
    double land_emitted_W = 0.0;
    double land_evaporation_W = 0.0;
    double land_melt_J = 0.0;
    double land_storage_change_J = 0.0;
    std::array<double, 18> land_band_K_m2{};
    std::array<double, 18> land_band_m2{};
    std::array<double, 18> ocean_band_K_m2{};
    std::array<double, 18> ocean_band_m2{};
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
    a.cloud_cover_m2 += b.cloud_cover_m2;
    a.high_cloud_cover_m2 += b.high_cloud_cover_m2;
    for (std::size_t band = 0; band < a.vapour_budget.size(); ++band) {
        auto& x = a.vapour_budget[band];
        const auto& y = b.vapour_budget[band];
        for (std::size_t k = 0; k < x.transport_kg.size(); ++k) {
            x.transport_kg[k] += y.transport_kg[k];
            x.evaporation_kg[k] += y.evaporation_kg[k];
            x.condensation_kg[k] += y.condensation_kg[k];
            x.convective_kg[k] += y.convective_kg[k];
            x.cloud_evaporation_kg[k] += y.cloud_evaporation_kg[k];
            x.rain_evaporation_kg[k] += y.rain_evaporation_kg[k];
            x.column_change_kg[k] += y.column_change_kg[k];
            x.vapour_kg[k] += y.vapour_kg[k];
        }
    }
    a.cloud_optical_depth_m2 += b.cloud_optical_depth_m2;
    for (std::size_t band = 0; band < a.band_cloud_m2.size(); ++band) {
        a.band_cloud_m2[band] += b.band_cloud_m2[band];
        a.band_cell_area_m2[band] += b.band_cell_area_m2[band];
    }
    a.cloud_shortwave_effect_W += b.cloud_shortwave_effect_W;
    a.cloud_longwave_effect_W += b.cloud_longwave_effect_W;
    a.cloud_water_change_kg += b.cloud_water_change_kg;
    a.cloud_water_kg += b.cloud_water_kg;
    a.convective_precipitation_kg += b.convective_precipitation_kg;
    a.detrained_kg += b.detrained_kg;
    a.cloud_evaporation_kg += b.cloud_evaporation_kg;
    a.rain_evaporation_kg += b.rain_evaporation_kg;
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
    a.land_absorbed_W += b.land_absorbed_W;
    a.land_source_W += b.land_source_W;
    a.land_emitted_W += b.land_emitted_W;
    a.land_evaporation_W += b.land_evaporation_W;
    a.land_melt_J += b.land_melt_J;
    a.land_storage_change_J += b.land_storage_change_J;
    for (std::size_t band = 0; band < a.land_band_m2.size(); ++band) {
        a.land_band_K_m2[band] += b.land_band_K_m2[band];
        a.land_band_m2[band] += b.land_band_m2[band];
        a.ocean_band_K_m2[band] += b.ocean_band_K_m2[band];
        a.ocean_band_m2[band] += b.ocean_band_m2[band];
    }
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

// ADR-0023 §4.4: a cell's clouds as the radiation sees them through the
// step, from the start-of-step state (the humidity after the step's
// transport). `latent_J_kg` is the column's (the parcel's moist static
// energy for the convective trigger).
[[nodiscard]] CloudOptics cell_cloud_optics(const SlowState& slow,
                                            const AtmosphereParameters& atmosphere,
                                            std::size_t cell, double gravity_m_s2) {
    const std::size_t layers = atmosphere.layer_count;
    ColumnRadiation column;
    column.layers = layers;
    ColumnMoisture moisture;
    moisture.layer_mass_kg_m2 =
        slow.atmosphere_surface_pressure_Pa[cell] / (gravity_m_s2 * static_cast<double>(layers));
    for (std::size_t layer = 0; layer < layers; ++layer) {
        moisture.cloud_water_kg_kg[layer] = slow.atmosphere_cloud_water_kg_kg.layer(layer)[cell];
    }
    return cloud_optics(atmosphere, column, moisture);
}

// ADR-0021 §4.5: the humidity on the coarse graph's groups, mass-weighted
// (A p_s), carried by solve_implicit_tracer; each cell's humidity then
// scales with its group's, which conserves the water and keeps it
// non-negative. `eddy_latent_W_m2`, if given, receives each group's E
// (ADR-0024 §4.3): L_v times the water its eddies brought, per unit area,
// from the solution; antisymmetric per face, so Σ A E = 0 to rounding.
[[nodiscard]] HumidityTransportDiagnostics transport_humidity(
    const PlanetMesh& mesh, SlowState& slow, const CirculationTransport& circulation,
    const SurfaceEnergyParameters& surface, double gravity_m_s2, double dt_s,
    std::size_t worker_count, std::vector<double>* eddy_latent_W_m2 = nullptr) {
    AdvectionDiffusion adjusted;
    const bool sensitivity = surface.humidity_azonal_share != 0.0 ||
                             surface.humidity_azonal_column_share != 0.0 ||
                             surface.humidity_group_mixing != 0.0 ||
                             surface.humidity_eddy_factor != 1.0 ||
                             surface.humidity_zonal_share != 0.0;
    if (sensitivity) {
        adjusted = circulation.transport;
        for (double& g : adjusted.conductance_W_K) {
            g *= surface.humidity_eddy_factor;
        }
        if (surface.humidity_azonal_share != 0.0) {
            if (circulation.azonal_outflow_kg_s.size() != adjusted.outflow_kg_s.size()) {
                throw std::invalid_argument("the circulation has no azonal fluxes");
            }
            for (std::size_t k = 0; k < adjusted.outflow_kg_s.size(); ++k) {
                adjusted.outflow_kg_s[k] +=
                    surface.humidity_azonal_share * circulation.azonal_outflow_kg_s[k];
            }
        }
        if (surface.humidity_azonal_column_share != 0.0) {
            if (circulation.azonal_outflow_kg_s.size() != adjusted.outflow_kg_s.size()) {
                throw std::invalid_argument("the circulation has no azonal fluxes");
            }
            const std::size_t layers = adjusted.layers;
            const std::size_t entries = adjusted.outflow_kg_s.size() / layers;
            for (std::size_t k = 0; k < entries; ++k) {
                double column = 0.0;
                for (std::size_t layer = 0; layer < layers; ++layer) {
                    column += circulation.azonal_outflow_kg_s[layer * entries + k];
                }
                for (std::size_t layer = 0; layer < layers; ++layer) {
                    adjusted.outflow_kg_s[layer * entries + k] +=
                        surface.humidity_azonal_column_share * column /
                        static_cast<double>(layers);
                }
            }
        }
        if (surface.humidity_zonal_share != 0.0) {
            if (circulation.zonal_outflow_kg_s.size() != adjusted.outflow_kg_s.size()) {
                throw std::invalid_argument("the circulation has no zonal fluxes");
            }
            for (std::size_t k = 0; k < adjusted.outflow_kg_s.size(); ++k) {
                adjusted.outflow_kg_s[k] +=
                    surface.humidity_zonal_share * circulation.zonal_outflow_kg_s[k];
            }
        }
    }
    const AdvectionDiffusion& transport = sensitivity ? adjusted : circulation.transport;
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
    {
        // INV-04: the fluxes across each latitude, each face counted once,
        // from its group south of the latitude to the one north of it.
        const PlanetMesh& coarse = agglomerated_mesh(mesh);
        const std::size_t entries = graph.neighbour.size();
        const double exchange_scale = 1.0 / (transport.heat_capacity_J_kg_K * n);
        const auto latitude_deg = [&](std::size_t group) {
            return std::asin(std::clamp(coarse.cells()[group].center_unit.z, -1.0, 1.0)) * 180.0 /
                   std::numbers::pi;
        };
        for (std::size_t a = 0; a < groups; ++a) {
            const double south = latitude_deg(a);
            for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                const std::size_t b = graph.neighbour[k];
                const double north = latitude_deg(b);
                for (std::size_t boundary = 0; boundary < 17U; ++boundary) {
                    const double phi = -80.0 + 10.0 * static_cast<double>(boundary);
                    if (!(south < phi && north >= phi)) continue;
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        const double flux = transport.outflow_kg_s[layer * entries + k];
                        const double ca = moved.tracer[layer * groups + a];
                        const double cb = moved.tracer[layer * groups + b];
                        d.northward_mass_kg_s[layer][boundary] += flux;
                        d.northward_advected_water_kg_s[layer][boundary] += flux * (flux > 0.0 ? ca : cb);
                        d.northward_eddy_water_kg_s[layer][boundary] +=
                            transport.conductance_W_K[k] * exchange_scale * (ca - cb);
                    }
                }
            }
        }
    }
    if (eddy_latent_W_m2 != nullptr) {
        // The tracer's eddy exchange, G / (c_p N) per layer (kg/s).
        const double exchange_scale = 1.0 / (transport.heat_capacity_J_kg_K * n);
        eddy_latent_W_m2->assign(groups, 0.0);
        for (std::size_t a = 0; a < groups; ++a) {
            double water = 0.0;
            for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                const std::size_t b = graph.neighbour[k];
                double difference = 0.0;
                for (std::size_t layer = 0; layer < layers; ++layer) {
                    difference += moved.tracer[layer * groups + b] - moved.tracer[layer * groups + a];
                }
                water += transport.conductance_W_K[k] * exchange_scale * difference;
            }
            (*eddy_latent_W_m2)[a] = latent_heat_vaporisation_J_kg * water / graph.area_m2[a];
        }
    }
    double after = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const std::size_t group = group_of_cell[cell];
        const double mass = mesh.cells()[cell].area_m2 *
                            slow.atmosphere_surface_pressure_Pa[cell] / (gravity_m_s2 * n);
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const std::size_t at = layer * groups + group;
            double& q = humidity.layer(layer)[cell];
            q = mean[at] > 0.0 ? q * (moved.tracer[at] / mean[at]) : moved.tracer[at];
            if (surface.humidity_group_mixing != 0.0) {
                q += surface.humidity_group_mixing * (moved.tracer[at] - q);
            }
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
                         std::abs(evaporation_latent_J) + std::abs(condensation_latent_J);
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
    return std::abs(vapour_change_kg + cloud_water_change_kg + bucket_change_kg + snow_change_kg +
                    ice_change_kg -
                    (water_evaporation_kg - ocean_precipitation_kg - bucket_runoff_kg +
                     ice_frozen_kg - ice_melted_kg));
}

double SurfaceEnergyDiagnostics::water_cycle_gate_kg() const noexcept {
    const double stock = vapour_kg + cloud_water_kg + bucket_kg + snow_kg + ice_kg;
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
    if (water && !(surface.condensation_time_s > 0.0 && std::isfinite(surface.condensation_time_s))) {
        throw std::invalid_argument("the condensation time must be positive and finite");
    }
    // ADR-0021 §4.5: the month's transport moves the humidity first, with the
    // circulation's fixed fluxes on the coarse graph (split B: the columns
    // then evaporate and rain out). A month whose circulation failed moves
    // none (ADR-0011 §17.4's fallback has no mass fluxes).
    // With moist convection the eddies carry moist enthalpy (ADR-0024): the
    // transport takes the latent heat its eddies' water brought from their
    // heat, E, fixed for the step.
    const bool moist_eddies = water && surface.atmosphere.moist_convection &&
                              surface.atmosphere.convection;
    HumidityTransportDiagnostics humidity_transport;
    std::vector<double> eddy_latent;
    // The vapour budget's 30° bands (task M8-07).
    const auto band_of = [&](std::size_t cell) {
        const double latitude = std::asin(std::clamp(mesh.cells()[cell].center_unit.z, -1.0, 1.0));
        return static_cast<std::size_t>(
            std::clamp(std::floor((latitude * 180.0 / std::numbers::pi + 90.0) / 30.0), 0.0, 5.0));
    };
    std::array<std::array<double, max_atmosphere_layer_count>, 6> transport_by_band{};
    std::array<double, 18> latent_delivered_W{};   // by 10° band from 90° S
    std::vector<double> latent_delivered_W_m2(cells, 0.0);   // INV-04, per cell
    if (water && circulation != nullptr && circulation->active) {
        const std::size_t n = slow.atmosphere_layer_count();
        std::vector<double> before(n * cells);
        for (std::size_t k = 0; k < n; ++k) {
            const auto q = slow.atmosphere_specific_humidity_kg_kg.layer(k);
            std::copy(q.begin(), q.end(), before.begin() + static_cast<std::ptrdiff_t>(k * cells));
        }
        humidity_transport = transport_humidity(mesh, slow, *circulation, surface,
                                                surface_gravity_m_s2(parameters), dt_s,
                                                worker_count,
                                                moist_eddies ? &eddy_latent : nullptr);
        const double g = surface_gravity_m_s2(parameters);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const double mass = mesh.cells()[cell].area_m2 *
                                slow.atmosphere_surface_pressure_Pa[cell] / (g * static_cast<double>(n));
            const double latitude_deg =
                latitude_rad(mesh.cells()[cell].center_unit) * 180.0 / std::numbers::pi;
            const auto band10 = static_cast<std::size_t>(
                std::clamp(std::floor((latitude_deg + 90.0) / 10.0), 0.0, 17.0));
            for (std::size_t k = 0; k < n; ++k) {
                const double moved = mass * (slow.atmosphere_specific_humidity_kg_kg.layer(k)[cell] -
                                             before[k * cells + cell]);
                transport_by_band[band_of(cell)][k] += moved;
                latent_delivered_W[band10] += latent_heat_vaporisation_J_kg * moved / dt_s;
                latent_delivered_W_m2[cell] += latent_heat_vaporisation_J_kg * moved / dt_s /
                                               mesh.cells()[cell].area_m2;
            }
        }
    }
    const auto& circulation_fields = state.circulation();
    // The coupled climate circulation's balanced winds, or in reference mode
    // the resolved ones (AtmosphereDynamics writes them with the water
    // cycle). A circulation solved only for diagnostics must not reach the
    // state (ADR-0011 §17.2), so its winds are not used.
    const bool wind_source = (circulation != nullptr && circulation->active) ||
                             state.has_fast_state();
    const bool has_wind = wind_source &&
                          circulation_fields.eastward_wind_m_s.cell_count() == cells &&
                          circulation_fields.eastward_wind_m_s.layer_count() > 0U &&
                          circulation_fields.northward_wind_m_s.cell_count() == cells;
    const double water_gravity = surface_gravity_m_s2(parameters);
    const double water_layers = static_cast<double>(surface.atmosphere.layer_count);
    const double water_lapse = critical_lapse_exponent(surface.atmosphere, water_gravity);
    // Clouds (ADR-0023 §4.4) with moist convection: they reflect part of
    // the insolation before it reaches the tiles and add to the layers'
    // longwave depth.
    const bool clouds = moist_eddies;
    std::vector<CloudOptics> cloud(clouds ? cells : 0U);
    if (clouds && (slow.atmosphere_cloud_water_kg_kg.layer_count() !=
                   surface.atmosphere.layer_count)) {
        throw std::invalid_argument("clouds need the state's cloud water (initialise_water)");
    }
    if (clouds && state.forcing().cloud_fraction.layer_count() != surface.atmosphere.layer_count) {
        state.forcing().cloud_fraction = Field3D<float>(surface.atmosphere.layer_count, cells, 0.0F);
    }
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
                double surface_insolation = insolation;
                if (clouds) {
                    cloud[cell] = cell_cloud_optics(slow, surface.atmosphere, cell, water_gravity);
                    surface_insolation *= cloud[cell].transmission;
                }
                land_tiles[cell] = prepare_land_tile(
                    land,
                    {slow.land_surface_temperature_K[cell], slow.land_ground_temperature_K[cell]},
                    slow.land_snow_water_equivalent_kg_m2[cell], surface_insolation,
                    water ? 0.0 : static_cast<double>(precipitation[cell]),
                    surface.grey_emissivity, dt_s, land_forcing);
                ocean_tiles[cell] = prepare_ocean_tile(
                    ocean,
                    {slow.ocean_mixed_layer_temperature_K[cell],
                     slow.ocean_deep_temperature_K[cell]},
                    slow.sea_ice_mass_kg_m2[cell], surface_insolation, surface.grey_emissivity, dt_s,
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
                    if (water) {
                        // ADR-0021 §4.6: the vapour path, from the
                        // humidity after the month's transport.
                        LayerArray humidity{};
                        for (std::size_t layer = 0; layer < layers; ++layer) {
                            humidity[layer] =
                                slow.atmosphere_specific_humidity_kg_kg.layer(layer)[cell];
                        }
                        columns[cell] = column_radiation(
                            atmosphere, slow.atmosphere_surface_pressure_Pa[cell], gravity,
                            std::span<const double>(humidity.data(), layers));
                        if (clouds) {
                            add_cloud_longwave(columns[cell], cloud[cell]);
                        }
                    } else {
                        columns[cell] = column_radiation(
                            atmosphere, slow.atmosphere_surface_pressure_Pa[cell], gravity);
                    }
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
            moisture.condensation_fraction = -std::expm1(-dt_s / surface.condensation_time_s);
            // ADR-0023 §4.2: Betts–Miller in place of τ_c, when on.
            moisture.moist_convection = atmosphere.moist_convection;
            moisture.moist_convection_fraction =
                -std::expm1(-dt_s / atmosphere.moist_convection_time_s);
            moisture.moist_convection_relative_humidity =
                atmosphere.moist_convection_relative_humidity;
            // ADR-0023 §4.3: the cloud water and its time scales.
            for (std::size_t layer = 0; layer < layers; ++layer) {
                moisture.cloud_water_kg_kg[layer] =
                    slow.atmosphere_cloud_water_kg_kg.layer(layer)[cell];
            }
            moisture.cloud_water_threshold_kg_kg = atmosphere.cloud_water_threshold_kg_kg;
            moisture.autoconversion_fraction = -std::expm1(-dt_s / atmosphere.autoconversion_time_s);
            moisture.autoconversion_time_ratio = atmosphere.autoconversion_time_s / dt_s;
            moisture.evaporation_fraction = -std::expm1(-dt_s / atmosphere.rain_evaporation_time_s);
            moisture.rain_evaporation_efficiency = atmosphere.rain_evaporation_efficiency;
            moisture.subgrid_width_fraction = atmosphere.cloud_subgrid_width;
            moisture.convective_moistening = atmosphere.convective_moistening;
            moisture.non_precipitating_path_kg_m2 = atmosphere.cloud_non_precipitating_path_kg_m2;
            moisture.detrainment_share = atmosphere.convective_detrainment_share;
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
    // A column solved for one of the transport solve's trial sources. A trial
    // far from the solution can ask a column for a source it cannot balance
    // at any positive temperature (with a nearly black cloudy bottom layer,
    // task M8-05); the column is then restored to its start and reported
    // missing, and the transport solve rejects the trial. Per cell, so the
    // same for any worker count.
    const auto solve_trial_column =
        [&](std::size_t cell, double h,
            CellSurface& cell_surface) -> std::optional<ColumnSolveResult> {
        try {
            return solve_column(cell, h, cell_surface);
        } catch (const std::runtime_error&) {
            for (std::size_t layer = 0; layer < layers; ++layer) {
                layers_guess[cell * layers + layer] = layers_before[cell * layers + layer];
            }
            warm_starts[cell].valid = false;
            return std::nullopt;
        }
    };
    // A missing column's θ and s: NaN, so its group's residual and the trial's
    // merit are NaN and the line search rejects the trial.
    constexpr double missing = std::numeric_limits<double>::quiet_NaN();
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
    bool fell_back = false;   // ADR-0024 §9
    bool fallback_unconverged = false;   // ADR-0024 §9.6
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
                            // θ_c (ADR-0010 §3.4 C), or with moist
                            // convection its moist enthalpy temperature
                            // T_m (ADR-0024 §4.6).
                            CellSurface cell_surface;
                            const auto column = solve_trial_column(cell, h, cell_surface);
                            if (!column) {
                                cell_air[cell] = missing;
                                cell_slope[cell] = missing;
                            } else if (moist_eddies) {
                                cell_air[cell] = column->moist_enthalpy_K;
                                cell_slope[cell] = column->moist_enthalpy_slope_K_m2_W;
                            } else {
                                cell_air[cell] = column->theta_K;
                                cell_slope[cell] = column->theta_slope_K_m2_W;
                            }
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
        std::vector<double> cell_floor_W_m2(cells, -std::numeric_limits<double>::infinity());
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
            cell_floor_W_m2[cell] = cell_floor;
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
            // INV-04's heat_group_mixing: each cell's shift δ from its
            // group's source, held fixed through a coupled solve and updated
            // between solves, δ ← δ + m (θ* − θ_c) / s_c from the cells at
            // H + δ (zero when off). A converged solve's last response is at
            // its last iterate, so its cells' θ_c and s_c are used as they
            // are; otherwise the columns are solved again at H + δ.
            std::vector<double> cell_shift(cells, 0.0);
            const double mixing = surface.heat_group_mixing;
            const auto update_shift = [&](const Field2D<double>& source, bool converged) {
                std::vector<double> theta(cells, missing);
                std::vector<double> theta_slope(cells, missing);
                if (converged) {
                    theta = cell_air;
                    theta_slope = cell_slope;
                } else {
                    for_each_deterministic_block(
                        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                                CellSurface cell_surface;
                                const auto solved = solve_trial_column(
                                    cell, source[group_of_cell[cell]] + cell_shift[cell],
                                    cell_surface);
                                if (solved) {
                                    theta[cell] =
                                        moist_eddies ? solved->moist_enthalpy_K : solved->theta_K;
                                    theta_slope[cell] = moist_eddies
                                                            ? solved->moist_enthalpy_slope_K_m2_W
                                                            : solved->theta_slope_K_m2_W;
                                }
                            }
                        });
                }
                for (std::size_t group = 0; group < groups; ++group) {
                    double weighted = 0.0;
                    double weight = 0.0;
                    for (std::size_t k = member_offset[group]; k < member_offset[group + 1U]; ++k) {
                        const std::size_t cell = members[k];
                        if (std::isfinite(theta[cell]) && theta_slope[cell] > 0.0) {
                            const double w = mesh.cells()[cell].area_m2 / theta_slope[cell];
                            weighted += w * theta[cell];
                            weight += w;
                        }
                    }
                    const double target = weight > 0.0 ? weighted / weight : 0.0;
                    // The update, scaled by λ ≤ 1 (which keeps Σ A δ = 0) so
                    // that no cell's source falls more than half way to its
                    // floor.
                    double scale = 1.0;
                    for (std::size_t k = member_offset[group]; k < member_offset[group + 1U]; ++k) {
                        const std::size_t cell = members[k];
                        if (std::isfinite(theta[cell]) && theta_slope[cell] > 0.0) {
                            const double change =
                                mixing * (target - theta[cell]) / theta_slope[cell];
                            const double room =
                                source[group] + cell_shift[cell] - cell_floor_W_m2[cell];
                            if (change < 0.0 && std::isfinite(room)) {
                                scale = std::min(scale, std::max(0.0, 0.5 * room / -change));
                            }
                        }
                    }
                    for (std::size_t k = member_offset[group]; k < member_offset[group + 1U]; ++k) {
                        const std::size_t cell = members[k];
                        if (std::isfinite(theta[cell]) && theta_slope[cell] > 0.0) {
                            cell_shift[cell] +=
                                scale * mixing * (target - theta[cell]) / theta_slope[cell];
                        }
                    }
                }
            };
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
                                const auto solved = solve_trial_column(
                                    cell, source[group_of_cell[cell]] + cell_shift[cell],
                                    cell_surface);
                                if (!solved) {
                                    cell_air[cell] = missing;
                                    cell_slope[cell] = missing;
                                    for (std::size_t l = 0; l < layers; ++l) {
                                        cell_energy[l * cells + cell] = missing;
                                        cell_energy_slope[l * cells + cell] = missing;
                                    }
                                    continue;
                                }
                                const ColumnSolveResult& column = *solved;
                                cell_air[cell] =
                                    moist_eddies ? column.moist_enthalpy_K : column.theta_K;
                                cell_slope[cell] = moist_eddies
                                                       ? column.moist_enthalpy_slope_K_m2_W
                                                       : column.theta_slope_K_m2_W;
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
            // With heat_group_mixing, two solves, δ updated after the first,
            // the second started from the first's sources; the last solve's
            // δ is the one applied. A pass whose solve ends with a larger
            // residual than the one before is undone.
            const int passes = mixing != 0.0 ? 2 : 1;
            std::vector<double> previous_shift;
            AdvectionDiffusionResult previous_solve;
            for (int pass = 0; pass < passes; ++pass) {
                if (pass > 0) {
                    previous_shift = cell_shift;
                    previous_solve = coupled_solve;
                    update_shift(coupled_solve.source_W_m2,
                                 coupled_solve.consistency_residual_W_m2 <=
                                     coupled_newton_tolerance_W_m2);
                    coupled_settings.initial_source_W_m2 = &previous_solve.source_W_m2;
                }
                if (eddy_latent.empty()) {
                    coupled_solve = solve_implicit_advection_diffusion(
                        graph, circulation->transport, floor, coupled_response, coupled_settings,
                        worker_count);
                } else {
                    AdvectionDiffusion moist_transport = circulation->transport;
                    moist_transport.eddy_latent_W_m2 = eddy_latent;   // kept for the fallback
                    coupled_solve = solve_implicit_advection_diffusion(
                        graph, moist_transport, floor, coupled_response, coupled_settings,
                        worker_count);
                }
                if (pass > 0 && !(coupled_solve.consistency_residual_W_m2 <=
                                  std::max(previous_solve.consistency_residual_W_m2,
                                           coupled_newton_tolerance_W_m2))) {
                    cell_shift = std::move(previous_shift);
                    coupled_solve = std::move(previous_solve);
                    break;
                }
            }
            transport_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - solve_start)
                                    .count();
            for (std::size_t cell = 0; cell < cells; ++cell) {
                transport[cell] = coupled_solve.source_W_m2[group_of_cell[cell]] + cell_shift[cell];
            }
            // ADR-0024 §9: an unconverged coupled month takes ADR-0009's
            // diffusive transport, as a failed circulation month does
            // (ADR-0011 §17), with the eddies' latent heat E taken off: the
            // humidity transport already moved it as water.
            if (surface.transport_fallback_residual_W_m2 > 0.0 &&
                coupled_solve.consistency_residual_W_m2 > surface.transport_fallback_residual_W_m2 &&
                coefficient > 0.0) {
                // §9.6: the fallback's columns start from the step's start,
                // not from the coupled solve's last trials.
                const auto restart_columns = [&] {
                    layers_guess = layers_before;
                    for (ColumnWarmStart& warm : warm_starts) {
                        warm.valid = false;
                    }
                };
                restart_columns();
                solve = solve_implicit_transport(graph, conductance, floor, response, {},
                                                 worker_count,
                                                 eddy_latent.empty() ? nullptr : &eddy_latent);
                fell_back = true;
                if (solve.consistency_residual_W_m2 > surface.transport_fallback_residual_W_m2) {
                    // §9.6: a fallback that does not converge either moves
                    // no heat this month (H = 0 sums to zero; the water has
                    // moved and carries its latent energy).
                    restart_columns();
                    std::fill(transport.values().begin(), transport.values().end(), 0.0);
                    fallback_unconverged = true;
                } else {
                    for (std::size_t cell = 0; cell < cells; ++cell) {
                        transport[cell] = solve.source_W_m2[group_of_cell[cell]];
                    }
                }
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

    std::vector<double> land_melt_kg_m2(cells, 0.0);   // INV-04, per cell
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
                double convective_kg_m2_s = 0.0;
                double detrained_kg_m2_s = 0.0;
                double condensation_W_m2 = 0.0;
                double cell_evaporation_kg_m2_s = 0.0;
                LayerArray humidity_after{};
                LayerArray cloud_after{};
                double cloud_evaporation_kg_m2_s = 0.0;
                double rain_evaporation_kg_m2_s = 0.0;
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
                        convective_kg_m2_s = column.convective_precipitation_kg_m2_s;
                        detrained_kg_m2_s = column.detrained_kg_m2_s;
                        condensation_W_m2 = column.condensation_W_m2;
                        cell_evaporation_kg_m2_s = column.evaporation_kg_m2_s;
                        humidity_after = column.humidity_kg_kg;
                        cloud_after = column.cloud_water_kg_kg;
                        {
                            auto& budget = partial.vapour_budget[band_of(cell)];
                            const double mass = area_m2 * bottom_mass[cell];
                            budget.evaporation_kg[0] += area_m2 * column.evaporation_kg_m2_s * dt_s;
                            for (std::size_t k = 0; k < layers; ++k) {
                                budget.condensation_kg[k] += mass * column.condensate_kg_kg[k];
                                budget.convective_kg[k] += mass * column.convective_condensate_kg_kg[k];
                                budget.cloud_evaporation_kg[k] +=
                                    mass * column.cloud_evaporation_by_layer_kg_kg[k];
                                budget.rain_evaporation_kg[k] +=
                                    mass * column.rain_evaporation_by_layer_kg_kg[k];
                                budget.column_change_kg[k] +=
                                    mass * (column.humidity_kg_kg[k] -
                                            slow.atmosphere_specific_humidity_kg_kg.layer(k)[cell]);
                                budget.vapour_kg[k] += mass * column.humidity_kg_kg[k];
                            }
                        }
                        cloud_evaporation_kg_m2_s = column.cloud_evaporation_kg_m2_s;
                        rain_evaporation_kg_m2_s = column.rain_evaporation_kg_m2_s;
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
                    if (clouds) {
                        // The cloud radiative effects at the top (ADR-0023
                        // §4.7): the shortwave the clouds keep from the
                        // tiles, −S (1 − T) Σ f_t (1 − α_t), and the
                        // longwave, the clear column's outgoing at the same
                        // temperatures and surface emission minus the cloudy
                        // one's.
                        const CloudOptics& optics = cloud[cell];
                        const double absorbing =
                            fractions.land_fraction[cell] * (1.0 - land_tiles[cell].column.albedo) +
                            fractions.ocean_fraction[cell] * (1.0 - ocean_tiles[cell].column.albedo);
                        const double shortwave =
                            -static_cast<double>(insolation_W_m2[cell]) * (1.0 - optics.transmission) *
                            absorbing;
                        LayerArray humidity{};
                        for (std::size_t layer = 0; layer < layers; ++layer) {
                            // Still the start-of-step humidity the solve's
                            // radiation used: the cell's water is written below.
                            humidity[layer] =
                                slow.atmosphere_specific_humidity_kg_kg.layer(layer)[cell];
                        }
                        const ColumnRadiation clear = column_radiation(
                            atmosphere, slow.atmosphere_surface_pressure_Pa[cell],
                            surface_gravity_m_s2(parameters),
                            std::span<const double>(humidity.data(), layers));
                        // Both columns at the step's final temperatures, so
                        // the difference is the clouds' alone.
                        const auto outgoing = [&](const ColumnRadiation& radiating) {
                            LayerArray emission{};
                            for (std::size_t layer = 0; layer < layers; ++layer) {
                                const double t = layers_guess[cell * layers + layer];
                                emission[layer] = radiating.emissivity[layer] *
                                                  column_stefan_boltzmann_W_m2_K4 * t * t * t * t;
                            }
                            return longwave_fluxes(radiating, emission, column.upward_surface_W_m2)
                                .outgoing_W_m2;
                        };
                        const double longwave = outgoing(clear) - outgoing(columns[cell]);
                        for (std::size_t layer = 0; layer < layers; ++layer) {
                            state.forcing().cloud_fraction.layer(layer)[cell] =
                                static_cast<float>(optics.fraction[layer]);
                        }
                        state.forcing().cloud_shortwave_effect_W_m2[cell] = static_cast<float>(shortwave);
                        state.forcing().cloud_longwave_effect_W_m2[cell] = static_cast<float>(longwave);
                        state.forcing().cloud_optical_depth[cell] =
                            static_cast<float>(optics.column_optical_depth);
                        partial.cloud_cover_m2 += area_m2 * optics.column_fraction;
                        double clear_above = 1.0;
                        for (std::size_t layer = 1; layer < layers; ++layer) {
                            clear_above *= 1.0 - optics.fraction[layer];
                        }
                        partial.high_cloud_cover_m2 += area_m2 * (1.0 - clear_above);
                        partial.cloud_optical_depth_m2 += area_m2 * optics.column_optical_depth;
                        partial.band_cloud_m2[band_of_cell[cell]] += area_m2 * optics.column_fraction;
                        partial.band_cell_area_m2[band_of_cell[cell]] += area_m2;
                        partial.cloud_shortwave_effect_W += area_m2 * shortwave;
                        partial.cloud_longwave_effect_W += area_m2 * longwave;
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
                    partial.land_absorbed_W += land_weight * land_step.absorbed_W_m2;
                    partial.land_source_W += land_weight * land_step.source_W_m2;
                    partial.land_emitted_W += land_weight * land_step.emitted_W_m2;
                    partial.land_evaporation_W += land_weight * land_step.evaporation_W_m2;
                    partial.land_melt_J += land_weight * land_tile.latent_J_m2;
                    partial.land_storage_change_J += land_weight * land_step.storage_change_J_m2;
                    partial.land_band_K_m2[band_of_cell[cell]] +=
                        land_weight * land_step.state.surface_K;
                    partial.land_band_m2[band_of_cell[cell]] += land_weight;
                    land_melt_kg_m2[cell] = land_tile.melt_kg_m2;
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
                    partial.ocean_band_K_m2[band_of_cell[cell]] += ocean_weight * ocean_K;
                    partial.ocean_band_m2[band_of_cell[cell]] += ocean_weight;
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
                    // The cloud water, where moist convection runs it
                    // (ADR-0023 §4.3); otherwise it is left as it is.
                    double cloud_change = 0.0;
                    double cloud_stock = 0.0;
                    for (std::size_t layer = 0; layer < layers; ++layer) {
                        auto& c = slow.atmosphere_cloud_water_kg_kg.layer(layer)[cell];
                        if (atmosphere.moist_convection && atmosphere.convection) {
                            cloud_change += cloud_after[layer] - c;
                            c = cloud_after[layer];
                        }
                        cloud_stock += c;
                    }
                    state.forcing().evaporation_kg_m2_s[cell] =
                        static_cast<float>(cell_evaporation_kg_m2_s);
                    state.forcing().precipitation_kg_m2_s[cell] =
                        static_cast<float>(precipitation_kg_m2 / dt_s);
                    state.forcing().convective_precipitation_kg_m2_s[cell] =
                        static_cast<float>(convective_kg_m2_s);
                    state.forcing().runoff_kg_m2_s[cell] =
                        static_cast<float>(land_share * overflow / dt_s);
                    partial.precipitation_kg += area_m2 * precipitation_kg_m2;
                    partial.ocean_precipitation_kg += (area_m2 - land_weight) * precipitation_kg_m2;
                    partial.condensation_latent_J += area_m2 * dt_s * condensation_W_m2;
                    partial.cloud_water_change_kg += area_m2 * bottom_mass[cell] * cloud_change;
                    partial.cloud_water_kg += area_m2 * bottom_mass[cell] * cloud_stock;
                    partial.convective_precipitation_kg += area_m2 * dt_s * convective_kg_m2_s;
                    partial.detrained_kg += area_m2 * dt_s * detrained_kg_m2_s;
                    partial.cloud_evaporation_kg += area_m2 * dt_s * cloud_evaporation_kg_m2_s;
                    partial.rain_evaporation_kg += area_m2 * dt_s * rain_evaporation_kg_m2_s;
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
    for (std::size_t band = 0; band < total.land_band_m2.size(); ++band) {
        diagnostics.land_zonal_mean_surface_temperature_K[band] =
            total.land_band_m2[band] > 0.0 ? total.land_band_K_m2[band] / total.land_band_m2[band]
                                           : 0.0;
        diagnostics.ocean_zonal_mean_surface_temperature_K[band] =
            total.ocean_band_m2[band] > 0.0
                ? total.ocean_band_K_m2[band] / total.ocean_band_m2[band]
                : 0.0;
    }
    diagnostics.land_absorbed_W = total.land_absorbed_W;
    diagnostics.land_source_W = total.land_source_W;
    diagnostics.land_emitted_W = total.land_emitted_W;
    diagnostics.land_evaporation_W = total.land_evaporation_W;
    diagnostics.land_melt_heat_J = total.land_melt_J;
    diagnostics.land_storage_change_J = total.land_storage_change_J;
    diagnostics.land_melt_kg_m2 = std::move(land_melt_kg_m2);
    diagnostics.cell_transport_W_m2.assign(transport.values().begin(), transport.values().end());
    diagnostics.cell_latent_transport_W_m2 = std::move(latent_delivered_W_m2);
    if (!eddy_latent.empty()) {
        const std::vector<std::size_t>* group_map = nullptr;
        static_cast<void>(agglomerated_transport_graph(mesh, group_map));
        diagnostics.cell_eddy_latent_transport_W_m2.resize(cells);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            diagnostics.cell_eddy_latent_transport_W_m2[cell] = eddy_latent[(*group_map)[cell]];
        }
    }
    diagnostics.transport_solve_s = transport_seconds;
    diagnostics.transport_fell_back = fell_back;
    diagnostics.transport_fallback_unconverged = fallback_unconverged;
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
    for (std::size_t boundary = 0; boundary < diagnostics.northward_latent_transport_W.size();
         ++boundary) {
        double north = 0.0;
        for (std::size_t band = boundary + 1U; band < latent_delivered_W.size(); ++band) {
            north += latent_delivered_W[band];
        }
        diagnostics.northward_latent_transport_W[boundary] = north;
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
    diagnostics.cloud_cover_m2 = total.cloud_cover_m2;
    diagnostics.high_cloud_cover_m2 = total.high_cloud_cover_m2;
    diagnostics.vapour_budget = total.vapour_budget;
    for (std::size_t band = 0; band < diagnostics.vapour_budget.size(); ++band) {
        diagnostics.vapour_budget[band].transport_kg = transport_by_band[band];
    }
    diagnostics.cloud_optical_depth_m2 = total.cloud_optical_depth_m2;
    for (std::size_t band = 0; band < total.band_cloud_m2.size(); ++band) {
        diagnostics.zonal_cloud_cover[band] =
            total.band_cell_area_m2[band] > 0.0 ? total.band_cloud_m2[band] / total.band_cell_area_m2[band]
                                                : 0.0;
    }
    diagnostics.cloud_shortwave_effect_W = total.cloud_shortwave_effect_W;
    diagnostics.cloud_longwave_effect_W = total.cloud_longwave_effect_W;
    diagnostics.cloud_water_change_kg = total.cloud_water_change_kg;
    diagnostics.cloud_water_kg = total.cloud_water_kg;
    diagnostics.convective_precipitation_kg = total.convective_precipitation_kg;
    diagnostics.detrained_kg = total.detrained_kg;
    diagnostics.cloud_evaporation_kg = total.cloud_evaporation_kg;
    diagnostics.rain_evaporation_kg = total.rain_evaporation_kg;
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
                summary.northward_latent_transport_W[band] +=
                    dt_s * step.northward_latent_transport_W[band];
            }
            summary.high_cloud_cover_m2 += dt_s * step.high_cloud_cover_m2;
            summary.precipitation_kg += step.precipitation_kg;
            summary.convective_precipitation_kg += step.convective_precipitation_kg;
            summary.detrained_kg += step.detrained_kg;
            summary.cloud_cover_m2 += dt_s * step.cloud_cover_m2;
            summary.cloud_shortwave_effect_W += dt_s * step.cloud_shortwave_effect_W;
            summary.cloud_longwave_effect_W += dt_s * step.cloud_longwave_effect_W;
            summary.cloud_water_kg += dt_s * step.cloud_water_kg;
            summary.cloud_optical_depth_m2 += dt_s * step.cloud_optical_depth_m2;
            for (std::size_t band = 0; band < summary.zonal_cloud_cover.size(); ++band) {
                summary.zonal_cloud_cover[band] += dt_s * step.zonal_cloud_cover[band];
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
        for (auto& value : summary.northward_latent_transport_W) {
            value /= total_s;
        }
        summary.high_cloud_cover_m2 /= total_s;
        summary.cloud_cover_m2 /= total_s;
        summary.cloud_shortwave_effect_W /= total_s;
        summary.cloud_longwave_effect_W /= total_s;
        summary.cloud_water_kg /= total_s;
        summary.cloud_optical_depth_m2 /= total_s;
        for (auto& value : summary.zonal_cloud_cover) {
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
    // The vapour path where the water cycle runs (ADR-0021 §4.6).
    const bool water = surface.water_cycle &&
                       slow.atmosphere_specific_humidity_kg_kg.layer_count() == layers;
    validate_atmosphere_parameters(atmosphere);
    // The clouds' shortwave and longwave, as the step sees them (ADR-0023
    // §4.4).
    const bool clouds = water && atmosphere.moist_convection && atmosphere.convection &&
                        slow.atmosphere_cloud_water_kg_kg.layer_count() == layers;
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
                const CloudOptics optics =
                    clouds ? cell_cloud_optics(slow, atmosphere, cell, gravity) : CloudOptics{};
                const double insolation = insolation_W_m2[cell] * optics.transmission;
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

                LayerArray humidity{};
                if (water) {
                    for (std::size_t k = 0; k < layers; ++k) {
                        humidity[k] = slow.atmosphere_specific_humidity_kg_kg.layer(k)[cell];
                    }
                }
                ColumnRadiation column =
                    water ? column_radiation(atmosphere, slow.atmosphere_surface_pressure_Pa[cell],
                                             gravity,
                                             std::span<const double>(humidity.data(), layers))
                          : column_radiation(atmosphere, slow.atmosphere_surface_pressure_Pa[cell],
                                             gravity);
                add_cloud_longwave(column, optics);
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
