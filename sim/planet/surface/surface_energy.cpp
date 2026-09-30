#include "sim/planet/surface/surface_energy.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace planetsim {

SurfaceEnergyParameters surface_energy_parameters_for(PlanetPreset preset) noexcept {
    switch (preset) {
    case PlanetPreset::dead_rock:
        return {SurfaceMaterial::rock, 0.0};
    case PlanetPreset::aqua_planet:
        return {SurfaceMaterial::dry_soil, 0.0};
    case PlanetPreset::earth_like:
        return {SurfaceMaterial::dry_soil, earth_like_grey_emissivity};
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
    double max_newton_residual_W_m2 = 0.0;
    double weighted_temperature_K_m2 = 0.0;
    double area_m2 = 0.0;
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
    a.max_newton_residual_W_m2 = std::max(a.max_newton_residual_W_m2, b.max_newton_residual_W_m2);
    a.weighted_temperature_K_m2 += b.weighted_temperature_K_m2;
    a.area_m2 += b.area_m2;
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

}  // namespace

double SurfaceEnergyDiagnostics::closure_residual_J() const noexcept {
    return std::abs(storage_change_J - duration_s * (absorbed_W - emitted_W));
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
    if (insolation_W_m2.size() != cells || fractions.land_fraction.size() != cells ||
        fractions.ocean_fraction.size() != cells) {
        throw std::invalid_argument("surface energy inputs do not match the mesh");
    }
    const ColumnProperties land = column_properties(surface.land_material, parameters);
    const ColumnProperties ocean = column_properties(SurfaceMaterial::ocean, parameters);
    SlowState& slow = state.slow();

    const BudgetPartial total = reduce_deterministic_blocks<BudgetPartial>(
        mesh.blocks(), worker_count, BudgetPartial{},
        [&](std::size_t, const CellBlock& block) {
            BudgetPartial partial;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double area_m2 = mesh.cells()[cell].area_m2;
                const double insolation = insolation_W_m2[cell];
                const ColumnStepResult land_step = step_column(
                    land,
                    {slow.land_surface_temperature_K[cell], slow.land_ground_temperature_K[cell]},
                    insolation, surface.grey_emissivity, dt_s);
                const ColumnStepResult ocean_step =
                    step_column(ocean,
                                {slow.ocean_mixed_layer_temperature_K[cell],
                                 slow.ocean_deep_temperature_K[cell]},
                                insolation, surface.grey_emissivity, dt_s);
                const double land_weight = area_m2 * fractions.land_fraction[cell];
                const double ocean_weight = area_m2 * fractions.ocean_fraction[cell];
                accumulate_tile(partial, land, land_step, land_weight);
                accumulate_tile(partial, ocean, ocean_step, ocean_weight);
                if (land_weight > 0.0) {
                    partial.land_temperature_K_m2 += land_weight * land_step.state.surface_K;
                    partial.land_area_m2 += land_weight;
                }
                if (ocean_weight > 0.0) {
                    partial.ocean_temperature_K_m2 += ocean_weight * ocean_step.state.surface_K;
                    partial.ocean_area_m2 += ocean_weight;
                }
                slow.land_surface_temperature_K[cell] =
                    static_cast<float>(land_step.state.surface_K);
                slow.land_ground_temperature_K[cell] = static_cast<float>(land_step.state.lower_K);
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
            total_s += dt_s;
        }
        summary.absorbed_W /= total_s;
        summary.emitted_W /= total_s;
        summary.mean_surface_temperature_K /= total_s;
        summary.land_mean_surface_temperature_K /= total_s;
        summary.ocean_mean_surface_temperature_K /= total_s;
    }
    return summary;
}

}  // namespace planetsim
