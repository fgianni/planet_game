#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/surface/surface_materials.hpp"

#include <array>
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
    // ADR-0009: North's unit-sphere diffusion coefficient D (W/m²/K); the
    // conductance is K = D R². Zero switches horizontal transport off.
    double transport_coefficient_W_m2_K = 0.0;
    // ADR-0009 §10: exchange γ (W/m²/K) of every tile with its cell's air,
    // the area-weighted mean surface temperature. Zero: no shared air.
    double air_exchange_W_m2_K = 0.0;
};

// ADR-0009 §10: the bulk sensible-heat exchange ρ c_p C_H U with
// ρ = 1.2 kg/m³, c_p = 1005 J/kg/K, C_H = 1.2e-3 and U = 7 m/s.
inline constexpr double bulk_air_exchange_W_m2_K = 10.0;

// Earth-like grey-layer emissivity: a calibration constant (ADR-0007 §3.2 C,
// specification §24), refitted when clouds (M8) and the atmosphere (M5)
// arrive. Fit: bisection to a 288 K length-weighted global mean surface
// temperature over the last of 60 spin-up years, earth_like preset, L5,
// seed 1 (`planet_cli thermal --calibrate 288`), giving 0.49644. With it,
// seeds 2, 3 and 7 give 288.1, 287.7 and 287.5 K, L6 288.0 K, and a
// 200-year spin-up 288.0 K.
inline constexpr double earth_like_grey_emissivity = 0.4964;

// Earth-like transport coefficient D: a calibration constant (ADR-0009 §4.4)
// fitted together with earth_like_grey_emissivity. See the fit record at
// its definition.
inline constexpr double earth_like_transport_coefficient_W_m2_K = 0.0;

// dead_rock: rock, g = 0, no transport. aqua_planet: g = 0, no transport
// (experiments A and B have no atmosphere and no currents; only the ocean
// tile has area). earth_like: dry soil (until hydrology supplies moisture,
// M9), the calibrated g and D.
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

// ADR-0008 §4.6: no snow and no sea ice, both reservoirs zero.
void initialise_cryosphere(const PlanetMesh& mesh, SlowState& slow);

// The PSNAP migrations: schema 1 -> 2 initialises the four temperatures with
// initialise_surface_temperatures (ADR-0007 §4.6); schema 3 -> 4 the
// cryosphere with initialise_cryosphere (ADR-0008 §4.6).
[[nodiscard]] SnapshotMigration surface_energy_migration(const PlanetParameters& parameters,
                                                         const SurfaceEnergyParameters& surface);

// Global budget of one step (ADR-0007 §4.4, ADR-0008 §4.5): area- and
// tile-weighted sums in double through the deterministic reduction. Energy
// closure compares the stored energy change plus the latent heat taken by
// phase change with the net radiative input over the step; water closure
// compares the change of the snow reservoir with snowfall minus melt.
struct SurfaceEnergyDiagnostics {
    double duration_s = 0.0;
    double absorbed_W = 0.0;           // Σ A f (1 − α) Q
    double emitted_W = 0.0;            // Σ A f β ε σ T_s'⁴
    double storage_change_J = 0.0;     // Σ A f (C_s ΔT_s + C_l ΔT_l)
    double stored_energy_J = 0.0;      // Σ A f (C_s T_s' + C_l T_l'), for the rounding floor
    double latent_heat_J = 0.0;        // Σ A f L_f · melt
    double max_newton_residual_W_m2 = 0.0;
    // Water equivalent (kg), land tiles.
    double snowfall_kg = 0.0;
    double rain_kg = 0.0;
    double melt_kg = 0.0;
    double snow_change_kg = 0.0;       // Σ A f (W' − W)
    double snow_kg = 0.0;              // Σ A f W' after the step
    // Horizontal transport (ADR-0009), zero when it is off.
    double transport_W = 0.0;          // Σ A f (source received): zero to rounding
    double transport_cell_sum_W = 0.0; // Σ A H of the diffusion itself (V1)
    double transport_absolute_W = 0.0; // Σ A |H|
    double transport_dissipation_W_K = 0.0;   // Σ A H T̄ ≤ 0
    double transport_consistency_W_m2 = 0.0;  // max |H − h| of the implicit solve
    int transport_newton_iterations = 0;
    int transport_cg_iterations = 0;
    // Northward transport (W) across 80° S, 70° S, ..., 80° N: the heat the
    // transport delivers north of each latitude.
    std::array<double, 17> northward_transport_W{};
    // Area- and tile-weighted surface temperature of the 10° bands from
    // 90° S (0 for a band with no area).
    std::array<double, 18> zonal_mean_surface_temperature_K{};
    double mean_surface_temperature_K = 0.0;   // area- and tile-weighted
    double land_mean_surface_temperature_K = 0.0;
    double ocean_mean_surface_temperature_K = 0.0;
    double min_surface_temperature_K = 0.0;    // over tiles with area
    double max_surface_temperature_K = 0.0;

    [[nodiscard]] double runoff_kg() const noexcept { return rain_kg + melt_kg; }

    // |storage + latent − Δt (absorbed − emitted + transport)|
    [[nodiscard]] double closure_residual_J() const noexcept;
    // The V2 gate (ADR-0007 §9): 1e-9 of the flux scale plus the rounding
    // floor 4ε of the stored energy.
    [[nodiscard]] double closure_gate_J() const noexcept;
    // |Δ snow − (snowfall − melt)|
    [[nodiscard]] double water_residual_kg() const noexcept;
    // ADR-0008 V2: 1e-12 of the moved and stored water plus the rounding
    // floor 4ε of the stored water.
    [[nodiscard]] double water_gate_kg() const noexcept;
};

// Advances both tiles of every cell by one backward-Euler step of length dt_s
// under the given per-cell insolation and the state's prescribed
// precipitation, with snow on the land tile (ADR-0008 §4.3); writes the new
// temperatures (land float, ocean double) and snow, and returns the global
// budget. Bit-identical for any worker count.
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
    // Water equivalent over the year (kg), and the snow at its end.
    double snowfall_kg = 0.0;
    double rain_kg = 0.0;
    double melt_kg = 0.0;
    double snow_kg = 0.0;
    // Time means over the year (ADR-0009 §4.5).
    std::array<double, 17> northward_transport_W{};
    std::array<double, 18> zonal_mean_surface_temperature_K{};

    // The larger of the two hemispheres' peak poleward transports.
    [[nodiscard]] double peak_poleward_transport_W() const noexcept;
    // Mean of the two equatorward bands minus mean of the two polar bands.
    [[nodiscard]] double equator_to_pole_difference_K() const noexcept;
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
