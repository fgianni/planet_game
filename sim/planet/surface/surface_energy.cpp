#include "sim/planet/surface/surface_energy.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/column_step.hpp"

#include <cstdint>
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
    slow.ocean_mixed_layer_temperature_K = Field2D<float>(cells, 0.0F);
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
                slow.ocean_mixed_layer_temperature_K[cell] = static_cast<float>(ocean_K);
                slow.ocean_deep_temperature_K[cell] = ocean_K;
            }
        });
}

SnapshotMigration surface_energy_migration(const PlanetParameters& parameters,
                                           const SurfaceEnergyParameters& surface) {
    SnapshotMigration migration;
    migration.initialise_schema_2_fields = [parameters, surface](const PlanetMesh& mesh,
                                                                 SlowState& staged) {
        initialise_surface_temperatures(mesh, staged, parameters, surface);
    };
    return migration;
}

}  // namespace planetsim
