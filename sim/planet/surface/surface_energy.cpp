#include "sim/planet/surface/surface_energy.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "sim/planet/surface/land_snow.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace planetsim {

SurfaceEnergyParameters surface_energy_parameters_for(PlanetPreset preset) noexcept {
    switch (preset) {
    case PlanetPreset::dead_rock:
        return {SurfaceMaterial::rock, 0.0};
    case PlanetPreset::aqua_planet:
        return {SurfaceMaterial::dry_soil, 0.0};
    case PlanetPreset::earth_like:
        return {SurfaceMaterial::dry_soil, earth_like_grey_emissivity,
                earth_like_transport_coefficient_W_m2_K, bulk_air_exchange_W_m2_K};
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
                const double land_K = column_equilibrium_temperature_K(
                    land, annual_mean[cell], surface.grey_emissivity);
                const double ocean_K = column_equilibrium_temperature_K(
                    ocean, annual_mean[cell], surface.grey_emissivity);
                slow.land_surface_temperature_K[cell] = static_cast<float>(land_K);
                slow.land_ground_temperature_K[cell] = static_cast<float>(land_K);
                slow.ocean_mixed_layer_temperature_K[cell] = ocean_K;
                slow.ocean_deep_temperature_K[cell] = ocean_K;
            }
        });
}

void initialise_cryosphere(const PlanetMesh& mesh, SlowState& slow) {
    slow.land_snow_water_equivalent_kg_m2 = Field2D<double>(mesh.cell_count(), 0.0);
    slow.sea_ice_mass_kg_m2 = Field2D<double>(mesh.cell_count(), 0.0);
}

SnapshotMigration surface_energy_migration(const PlanetParameters& parameters,
                                           const SurfaceEnergyParameters& surface) {
    SnapshotMigration migration;
    migration.initialise_schema_2_fields = [parameters, surface](const PlanetMesh& mesh,
                                                                 SlowState& staged) {
        initialise_surface_temperatures(mesh, staged, parameters, surface);
    };
    migration.initialise_schema_4_fields = [](const PlanetMesh& mesh, SlowState& staged) {
        initialise_cryosphere(mesh, staged);
    };
    return migration;
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
    double weighted_temperature_K_m2 = 0.0;
    double area_m2 = 0.0;
    std::array<double, 18> band_temperature_K_m2{};
    std::array<double, 18> band_area_m2{};
    double land_temperature_K_m2 = 0.0;
    double land_area_m2 = 0.0;
    double ocean_temperature_K_m2 = 0.0;
    double ocean_area_m2 = 0.0;
    double min_K = std::numeric_limits<double>::infinity();
    double max_K = -std::numeric_limits<double>::infinity();
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
    a.weighted_temperature_K_m2 += b.weighted_temperature_K_m2;
    a.area_m2 += b.area_m2;
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
    return a;
}

void accumulate_tile(BudgetPartial& partial, const ColumnProperties& column,
                     const ColumnStepResult& result, double weight_m2) {
    if (!(weight_m2 > 0.0)) {
        return;
    }
    partial.absorbed_W += weight_m2 * result.absorbed_W_m2;
    partial.emitted_W += weight_m2 * result.emitted_W_m2;
    partial.transport_W += weight_m2 * result.source_W_m2;
    partial.storage_change_J += weight_m2 * result.storage_change_J_m2;
    partial.stored_energy_J +=
        weight_m2 * (column.surface_heat_capacity_J_m2_K * result.state.surface_K +
                     column.lower_heat_capacity_J_m2_K * result.state.lower_K);
    partial.max_newton_residual_W_m2 =
        std::max(partial.max_newton_residual_W_m2, std::abs(result.newton_residual_W_m2));
    partial.weighted_temperature_K_m2 += weight_m2 * result.state.surface_K;
    partial.area_m2 += weight_m2;
    partial.min_K = std::min(partial.min_K, result.state.surface_K);
    partial.max_K = std::max(partial.max_K, result.state.surface_K);
}

struct CellTiles {
    LandSnowStepResult land;
    ColumnStepResult ocean;
    double mean_K = 0.0;          // T̄: area-weighted over the tiles with area
    double slope_K_m2_W = 0.0;    // dT̄/dh
};

// One cell's tiles under the transport source h (W/m² of cell area), ADR-0009
// §4.1 and §10. The tiles with area share h per unit area; with air exchange
// γ every tile also receives γ (T̄ − T_t), and T̄ is found by safeguarded
// Newton on G(T̄) = Σ w_t T_t(T̄) − T̄, which decreases with slope in (−1, 0).
// A tile without area receives no transport and follows the cell's air.
CellTiles solve_cell(const LandSnowSystem& land_tile, const ColumnProperties& ocean,
                     const ColumnSystem& ocean_system, ColumnState ocean_before,
                     double land_fraction, double ocean_fraction, double h, double exchange,
                     double first_guess_K) {
    const double share = land_fraction + ocean_fraction;
    const double land_weight = land_fraction / share;
    const double ocean_weight = ocean_fraction / share;
    const double land_source = land_fraction > 0.0 ? h / share : 0.0;
    const double ocean_source = ocean_fraction > 0.0 ? h / share : 0.0;

    CellTiles tiles;
    if (exchange == 0.0) {
        tiles.land = solve_land_tile(land_tile, land_source);
        tiles.ocean = solve_column_step(ocean, ocean_system, ocean_before, ocean_source);
        tiles.mean_K = land_weight * tiles.land.column.state.surface_K +
                       ocean_weight * tiles.ocean.state.surface_K;
        tiles.slope_K_m2_W = (land_weight * tiles.land.column.surface_slope_K_m2_W +
                              ocean_weight * tiles.ocean.surface_slope_K_m2_W) /
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
        if (ocean_fraction > 0.0 && ocean_system.b + ocean_source + exchange * mean_K > 0.0) {
            const auto water = solve_column_step(ocean, ocean_system, ocean_before,
                                                 ocean_source + exchange * mean_K, exchange);
            weighted += ocean_weight * water.state.surface_K;
            weighted_slope += ocean_weight * water.surface_slope_K_m2_W;
        }
        return weighted - mean_K;
    };
    double low = 0.0;        // G(low) ≥ 0
    double high = 1.0e4;     // G(high) < 0
    double mean_K = std::clamp(first_guess_K, low, high);
    double weighted_slope = 0.0;
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

    tiles.land = solve_land_tile(land_tile, land_source + exchange * mean_K, exchange);
    tiles.ocean = solve_column_step(ocean, ocean_system, ocean_before,
                                    ocean_source + exchange * mean_K, exchange);
    tiles.mean_K = mean_K;
    // T̄ = Σ w T_t(h/share + γ T̄)  ⇒  dT̄/dh = (Σ w s_t / share) / (1 − γ Σ w s_t).
    tiles.slope_K_m2_W = weighted_slope / share / (1.0 - exchange * weighted_slope);
    return tiles;
}

}  // namespace

double SurfaceEnergyDiagnostics::closure_residual_J() const noexcept {
    return std::abs(storage_change_J + latent_heat_J -
                    duration_s * (absorbed_W - emitted_W + transport_W));
}

double SurfaceEnergyDiagnostics::closure_gate_J() const noexcept {
    const double scale = duration_s * (absorbed_W + emitted_W + transport_absolute_W) +
                         std::abs(storage_change_J) + std::abs(latent_heat_J);
    return 1e-9 * scale + 4.0 * std::numeric_limits<double>::epsilon() * stored_energy_J;
}

double SurfaceEnergyDiagnostics::water_residual_kg() const noexcept {
    return std::abs(snow_change_kg - (snowfall_kg - melt_kg));
}

double SurfaceEnergyDiagnostics::water_gate_kg() const noexcept {
    return 1e-12 * (snowfall_kg + melt_kg + snow_kg) +
           4.0 * std::numeric_limits<double>::epsilon() * snow_kg;
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
                                             std::size_t worker_count) {
    const PlanetMesh& mesh = state.mesh();
    const std::size_t cells = mesh.cell_count();
    const Field2D<float>& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
    if (insolation_W_m2.size() != cells || fractions.land_fraction.size() != cells ||
        fractions.ocean_fraction.size() != cells || precipitation.size() != cells ||
        state.slow().land_snow_water_equivalent_kg_m2.size() != cells) {
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
    std::vector<ColumnSystem> ocean_tiles(cells);
    std::vector<double> tile_share(cells, 0.0);   // f_land + f_ocean
    std::vector<std::uint8_t> band_of_cell(cells, 0U);   // 10° band from 90° S
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double insolation = insolation_W_m2[cell];
                land_tiles[cell] = prepare_land_tile(
                    land,
                    {slow.land_surface_temperature_K[cell], slow.land_ground_temperature_K[cell]},
                    slow.land_snow_water_equivalent_kg_m2[cell], insolation, precipitation[cell],
                    surface.grey_emissivity, dt_s);
                ocean_tiles[cell] = column_system(
                    ocean,
                    {slow.ocean_mixed_layer_temperature_K[cell],
                     slow.ocean_deep_temperature_K[cell]},
                    insolation, surface.grey_emissivity, dt_s);
                tile_share[cell] = static_cast<double>(fractions.land_fraction[cell]) +
                                   static_cast<double>(fractions.ocean_fraction[cell]);
                const double latitude_deg =
                    latitude_rad(mesh.cells()[cell].center_unit) * 180.0 / std::numbers::pi;
                band_of_cell[cell] = static_cast<std::uint8_t>(
                    std::clamp(std::floor((latitude_deg + 90.0) / 10.0), 0.0, 17.0));
            }
        });
    const double exchange = surface.air_exchange_W_m2_K;
    if (!std::isfinite(exchange) || exchange < 0.0) {
        throw std::invalid_argument("air exchange must be finite and non-negative");
    }
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
    const auto ocean_before = [&](std::size_t cell) {
        return ColumnState{slow.ocean_mixed_layer_temperature_K[cell],
                           slow.ocean_deep_temperature_K[cell]};
    };

    // The cell's source reaches each tile as H / (f_land + f_ocean) per unit
    // area, so the tile-weighted sum is Σ A H exactly (task M4-02 §1.2).
    Field2D<double> transport(cells, 0.0);
    ImplicitTransportResult solve;
    if (coefficient > 0.0) {
        const double conductance = coefficient * mesh.radius_m() * mesh.radius_m();
        const TransportResponse response = [&](const Field2D<double>& source,
                                               Field2D<double>& mean, Field2D<double>& slope) {
            for_each_deterministic_block(
                mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                    for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                        const auto tiles = solve_cell(
                            land_tiles[cell], ocean, ocean_tiles[cell], ocean_before(cell),
                            fractions.land_fraction[cell], fractions.ocean_fraction[cell],
                            source[cell], exchange, first_guess[cell]);
                        mean[cell] = tiles.mean_K;
                        slope[cell] = tiles.slope_K_m2_W;
                        // The next response's sources are close to these: its
                        // cell solve starts here (a deterministic sequence of
                        // calls, one cell per writer).
                        first_guess[cell] = tiles.mean_K;
                    }
                });
        };
        // Below this source a tile's surface root would fall under 100 K.
        Field2D<double> floor(cells, 0.0);
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    constexpr double lowest_K = 100.0;
                    // Only tiles with area take part. With shared air the
                    // floor holds the cell's mean at 100 K (the exchange
                    // terms cancel when all tiles are equal); without it,
                    // every tile with area.
                    const double land_fraction = fractions.land_fraction[cell];
                    const double ocean_fraction = fractions.ocean_fraction[cell];
                    const double land_floor =
                        land_fraction > 0.0 ? -land_tiles[cell].system.surplus_W_m2(lowest_K)
                                            : -std::numeric_limits<double>::infinity();
                    const double ocean_floor =
                        ocean_fraction > 0.0 ? -ocean_tiles[cell].surplus_W_m2(lowest_K)
                                             : -std::numeric_limits<double>::infinity();
                    if (exchange > 0.0) {
                        floor[cell] = (land_fraction > 0.0 ? land_fraction * land_floor : 0.0) +
                                      (ocean_fraction > 0.0 ? ocean_fraction * ocean_floor : 0.0);
                    } else {
                        floor[cell] = std::max(land_floor, ocean_floor) * tile_share[cell];
                    }
                }
            });
        solve = solve_implicit_transport(mesh, conductance, floor, response, {}, worker_count);
        transport = solve.source_W_m2;
    }

    const BudgetPartial total = reduce_deterministic_blocks<BudgetPartial>(
        mesh.blocks(), worker_count, BudgetPartial{},
        [&](std::size_t, const CellBlock& block) {
            BudgetPartial partial;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double area_m2 = mesh.cells()[cell].area_m2;
                const double snow = slow.land_snow_water_equivalent_kg_m2[cell];
                const auto tiles = solve_cell(land_tiles[cell], ocean, ocean_tiles[cell],
                                              ocean_before(cell), fractions.land_fraction[cell],
                                              fractions.ocean_fraction[cell], transport[cell],
                                              exchange, first_guess[cell]);
                const LandSnowStepResult& land_tile = tiles.land;
                const ColumnStepResult& land_step = land_tile.column;
                const ColumnStepResult& ocean_step = tiles.ocean;
                const double land_weight = area_m2 * fractions.land_fraction[cell];
                const double ocean_weight = area_m2 * fractions.ocean_fraction[cell];
                accumulate_tile(partial, land, land_step, land_weight);
                accumulate_tile(partial, ocean, ocean_step, ocean_weight);
                if (land_weight > 0.0) {
                    partial.land_temperature_K_m2 += land_weight * land_step.state.surface_K;
                    partial.land_area_m2 += land_weight;
                    partial.latent_heat_J += land_weight * land_tile.latent_J_m2;
                    partial.snowfall_kg += land_weight * land_tile.snowfall_kg_m2;
                    partial.rain_kg += land_weight * land_tile.rain_kg_m2;
                    partial.melt_kg += land_weight * land_tile.melt_kg_m2;
                    partial.snow_change_kg += land_weight * (land_tile.snow_kg_m2 - snow);
                    partial.snow_kg += land_weight * land_tile.snow_kg_m2;
                }
                if (ocean_weight > 0.0) {
                    partial.ocean_temperature_K_m2 += ocean_weight * ocean_step.state.surface_K;
                    partial.ocean_area_m2 += ocean_weight;
                }
                const std::size_t band = band_of_cell[cell];
                partial.band_temperature_K_m2[band] +=
                    land_weight * land_step.state.surface_K +
                    ocean_weight * ocean_step.state.surface_K;
                partial.band_area_m2[band] += land_weight + ocean_weight;
                slow.land_surface_temperature_K[cell] =
                    static_cast<float>(land_step.state.surface_K);
                slow.land_ground_temperature_K[cell] = static_cast<float>(land_step.state.lower_K);
                slow.land_snow_water_equivalent_kg_m2[cell] = land_tile.snow_kg_m2;
                slow.ocean_mixed_layer_temperature_K[cell] = ocean_step.state.surface_K;
                slow.ocean_deep_temperature_K[cell] = ocean_step.state.lower_K;
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
    for (std::size_t band = 0; band < total.band_area_m2.size(); ++band) {
        diagnostics.zonal_mean_surface_temperature_K[band] =
            total.band_area_m2[band] > 0.0
                ? total.band_temperature_K_m2[band] / total.band_area_m2[band]
                : 0.0;
    }
    diagnostics.transport_cell_sum_W = solve.sum_W;
    diagnostics.transport_absolute_W = solve.absolute_sum_W;
    diagnostics.transport_dissipation_W_K = solve.dissipation_W_K;
    diagnostics.transport_consistency_W_m2 = solve.consistency_residual_W_m2;
    diagnostics.transport_newton_iterations = solve.newton_iterations;
    diagnostics.transport_cg_iterations = solve.cg_iterations;
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
    diagnostics.max_newton_residual_W_m2 = total.max_newton_residual_W_m2;
    diagnostics.mean_surface_temperature_K = total.weighted_temperature_K_m2 / total.area_m2;
    diagnostics.land_mean_surface_temperature_K =
        total.land_area_m2 > 0.0 ? total.land_temperature_K_m2 / total.land_area_m2 : 0.0;
    diagnostics.ocean_mean_surface_temperature_K =
        total.ocean_area_m2 > 0.0 ? total.ocean_temperature_K_m2 / total.ocean_area_m2 : 0.0;
    diagnostics.min_surface_temperature_K = total.min_K;
    diagnostics.max_surface_temperature_K = total.max_K;
    return diagnostics;
}

void register_surface_energy(Scheduler& scheduler, PlanetState& state,
                             const PlanetParameters& parameters,
                             const SurfaceEnergyParameters& surface,
                             const SurfaceFractions& fractions, std::size_t worker_count,
                             SurfaceEnergyDiagnostics* last) {
    scheduler.register_process(
        {"surface_energy_climate", SimulationMode::climate, 0},
        [&state, &parameters, surface, &fractions, worker_count, last](const StepContext& context) {
            update_substep_mean_insolation(state, parameters, *context.substep, worker_count);
            const auto diagnostics = step_surface_energy(
                state, parameters, surface, fractions,
                state.forcing().substep_mean_insolation_W_m2,
                simulation_time_s(context.length_ticks()), worker_count);
            if (last != nullptr) {
                *last = diagnostics;
            }
        });
    scheduler.register_process(
        {"surface_energy_reference", SimulationMode::reference, 0},
        [&state, &parameters, surface, &fractions, worker_count, last](const StepContext& context) {
            update_solar_forcing(state, parameters,
                                 context.begin_tick + context.length_ticks() / 2, worker_count);
            const auto diagnostics = step_surface_energy(
                state, parameters, surface, fractions,
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
                                            std::size_t worker_count) {
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
            const double dt_s = simulation_time_s(substep.length_ticks());
            const auto step = step_surface_energy(state, parameters, surface, fractions,
                                                  state.forcing().substep_mean_insolation_W_m2,
                                                  dt_s, worker_count);
            summary.absorbed_W += dt_s * step.absorbed_W;
            summary.emitted_W += dt_s * step.emitted_W;
            summary.mean_surface_temperature_K += dt_s * step.mean_surface_temperature_K;
            summary.land_mean_surface_temperature_K += dt_s * step.land_mean_surface_temperature_K;
            summary.ocean_mean_surface_temperature_K +=
                dt_s * step.ocean_mean_surface_temperature_K;
            summary.snowfall_kg += step.snowfall_kg;
            summary.rain_kg += step.rain_kg;
            summary.melt_kg += step.melt_kg;
            summary.snow_kg = step.snow_kg;
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
        summary.emitted_W /= total_s;
        summary.mean_surface_temperature_K /= total_s;
        summary.land_mean_surface_temperature_K /= total_s;
        summary.ocean_mean_surface_temperature_K /= total_s;
        for (auto& value : summary.northward_transport_W) {
            value /= total_s;
        }
        for (auto& value : summary.zonal_mean_surface_temperature_K) {
            value /= total_s;
        }
    }
    return summary;
}

}  // namespace planetsim
