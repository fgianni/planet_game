#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/surface/surface_materials.hpp"

#include <cstddef>

namespace planetsim {

class PlanetMesh;
class PlanetState;
class Scheduler;
struct PlanetParameters;
struct SlowState;
struct SurfaceFractions;

// Surface energy of ADR-0007: a land tile and an ocean tile per cell, each a
// two-layer column, radiating εσT⁴ to space through an optional single-layer
// grey atmosphere of longwave emissivity g.
struct SurfaceEnergyParameters {
    SurfaceMaterial land_material = SurfaceMaterial::dry_soil;
    double grey_emissivity = 0.0;   // g in [0, 1)
};

// Earth-like grey-layer emissivity: a calibration constant (ADR-0007 §3.2 C,
// specification §24), refitted when clouds (M8) and the atmosphere (M5)
// arrive. Fit: bisection to a 288 K length-weighted global mean surface
// temperature over the last of 60 spin-up years, earth_like preset, L5,
// seed 1 (`planet_cli thermal --calibrate 288`), giving 0.49644. With it,
// seeds 2, 3 and 7 give 288.1, 287.7 and 287.5 K, L6 288.0 K, and a
// 200-year spin-up 288.0 K.
inline constexpr double earth_like_grey_emissivity = 0.4964;

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

// Global budget of one step (ADR-0007 §4.4): area- and tile-weighted sums in
// double through the deterministic reduction. Closure compares the stored
// energy change with the net radiative input over the step.
struct SurfaceEnergyDiagnostics {
    double duration_s = 0.0;
    double absorbed_W = 0.0;           // Σ A f (1 − α) Q
    double emitted_W = 0.0;            // Σ A f β ε σ T_s'⁴
    double storage_change_J = 0.0;     // Σ A f (C_s ΔT_s + C_l ΔT_l)
    double stored_energy_J = 0.0;      // Σ A f (C_s T_s' + C_l T_l'), for the rounding floor
    double max_newton_residual_W_m2 = 0.0;
    double mean_surface_temperature_K = 0.0;   // area- and tile-weighted
    double land_mean_surface_temperature_K = 0.0;
    double ocean_mean_surface_temperature_K = 0.0;
    double min_surface_temperature_K = 0.0;    // over tiles with area
    double max_surface_temperature_K = 0.0;

    // |storage − Δt (absorbed − emitted)|
    [[nodiscard]] double closure_residual_J() const noexcept;
};

// Advances both tiles of every cell by one backward-Euler step of length dt_s
// under the given per-cell insolation, writes the new temperatures (float,
// deep ocean double) and returns the global budget. Bit-identical for any
// worker count.
SurfaceEnergyDiagnostics step_surface_energy(PlanetState& state,
                                             const PlanetParameters& parameters,
                                             const SurfaceEnergyParameters& surface,
                                             const SurfaceFractions& fractions,
                                             const Field2D<float>& insolation_W_m2, double dt_s,
                                             std::size_t worker_count = 1U);

// Registers the surface as a climate-mode process (sub-step mean forcing,
// ADR-0006 §4.3) and a reference-mode process (instantaneous forcing at the
// step's midpoint tick). The references must outlive the scheduler; `last`,
// if given, receives each step's diagnostics.
void register_surface_energy(Scheduler& scheduler, PlanetState& state,
                             const PlanetParameters& parameters,
                             const SurfaceEnergyParameters& surface,
                             const SurfaceFractions& fractions, std::size_t worker_count = 1U,
                             SurfaceEnergyDiagnostics* last = nullptr);

// Annual summary of a run of climate sub-steps, length-weighted.
struct AnnualSurfaceSummary {
    double absorbed_W = 0.0;
    double emitted_W = 0.0;
    double mean_surface_temperature_K = 0.0;
    double land_mean_surface_temperature_K = 0.0;
    double ocean_mean_surface_temperature_K = 0.0;

    // (absorbed − emitted) / absorbed
    [[nodiscard]] double relative_imbalance() const noexcept;
};

// Spin-up (ADR-0007 §4.5, ADR-0006 §4.2): runs `years` orbital years of
// climate sub-steps outside any scenario run, without touching a clock, and
// returns the summary of the last year.
AnnualSurfaceSummary spin_up_surface_energy(PlanetState& state,
                                            const PlanetParameters& parameters,
                                            const SurfaceEnergyParameters& surface,
                                            const SurfaceFractions& fractions, int years,
                                            std::size_t worker_count = 1U);

}  // namespace planetsim
