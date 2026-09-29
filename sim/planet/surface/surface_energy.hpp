#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/surface/surface_materials.hpp"

#include <cstddef>

namespace planetsim {

class PlanetMesh;
class PlanetState;
struct PlanetParameters;
struct SlowState;

// Surface energy of ADR-0007: a land tile and an ocean tile per cell, each a
// two-layer column, radiating εσT⁴ to space through an optional single-layer
// grey atmosphere of longwave emissivity g.
struct SurfaceEnergyParameters {
    SurfaceMaterial land_material = SurfaceMaterial::dry_soil;
    double grey_emissivity = 0.0;   // g in [0, 1)
};

// Earth-like grey-layer emissivity: a calibration constant (ADR-0007 §3.2 C,
// specification §24), refitted when clouds (M8) and the atmosphere (M5)
// arrive. See earth_like_grey_emissivity_fit for its record.
inline constexpr double earth_like_grey_emissivity = 0.39;

// dead_rock: rock, g = 0. aqua_planet: g = 0 (only the ocean tile has area).
// earth_like: dry soil (until hydrology supplies moisture, M9) and the
// calibrated g.
[[nodiscard]] SurfaceEnergyParameters surface_energy_parameters_for(PlanetPreset preset) noexcept;

// Annual-mean insolation of every cell: the length-weighted mean of the
// twelve sub-step means of orbital year 0, in double.
void compute_annual_mean_insolation(const PlanetMesh& mesh, const PlanetParameters& parameters,
                                    Field2D<double>& annual_mean_W_m2,
                                    std::size_t worker_count = 1U);

// ADR-0007 §4.5: both tiles of every cell at the closed-form radiative
// equilibrium of their annual-mean insolation, both layers equal.
void initialise_surface_temperatures(const PlanetMesh& mesh, SlowState& slow,
                                     const PlanetParameters& parameters,
                                     const SurfaceEnergyParameters& surface,
                                     std::size_t worker_count = 1U);

// The PSNAP schema 1 -> 2 migration (ADR-0007 §4.6): initialises the four
// temperatures with initialise_surface_temperatures.
[[nodiscard]] SnapshotMigration surface_energy_migration(const PlanetParameters& parameters,
                                                         const SurfaceEnergyParameters& surface);

}  // namespace planetsim
