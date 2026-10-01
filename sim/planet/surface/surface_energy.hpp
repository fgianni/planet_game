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
    // ADR-0009: North's unit-sphere diffusion coefficient D (W/m²/K) of the
    // cells' air temperature (§11); the conductance is K = D R². Zero
    // switches horizontal transport off; non-zero needs air exchange.
    double transport_coefficient_W_m2_K = 0.0;
    // ADR-0009 §10: exchange γ (W/m²/K) of every tile with its cell's air.
    // Zero: no shared air.
    double air_exchange_W_m2_K = 0.0;
};

// ADR-0009 §10: the bulk sensible-heat exchange ρ c_p C_H U with
// ρ = 1.2 kg/m³, c_p = 1005 J/kg/K, C_H = 1.2e-3 and U = 7 m/s.
inline constexpr double bulk_air_exchange_W_m2_K = 10.0;

// Earth-like grey-layer emissivity g and transport coefficient D:
// calibration constants (ADR-0007 §3.2 C, ADR-0008 §4.7, ADR-0009 §4.4 and
// §10–12, specification §24), refitted when the atmosphere (M5), clouds (M8)
// and ocean transport (M11) arrive.
//
// Fit (2026-10-01, task M4-04): alternating bisection over the last of 150
// spin-up years, earth_like preset, L4, seed 1, sea ice active, no
// precipitation, air diffusion on the mesh one level coarser, to a 288 K
// length-weighted global mean surface temperature and a 42 K
// equator-to-pole difference of the P2 fit (North et al., 1981)
// (`planet_cli thermal --subdivision 4 --years 150 --calibrate 288
// --calibrate-gradient 42`). The ice edge moves a cell at a time, so the
// fit alternates between (g, D) = (0.4965, 0.6400): 287.93 K, 42.08 K and
// (0.4971, 0.6559): 288.03 K, 41.58 K; the first is kept. At L5: 288.20 K,
// 40.9 K, peak poleward transport 3.72 PW, sea ice 4.8–10.2 million km² in
// the north and 33.3–33.8 in the south (an open polar ocean), relative
// imbalance −2.2e-3 after 150 years (perennial ice still thickening). The
// fit before sea ice (2026-09-30) was g = 0.4455, D = 0.1999.
inline constexpr double earth_like_grey_emissivity = 0.4965;

// See earth_like_grey_emissivity for the joint fit record.
inline constexpr double earth_like_transport_coefficient_W_m2_K = 0.6400;

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

// ADR-0008 §4.6: no snow and no sea ice, both reservoirs zero; ocean layers
// below the seawater freezing point are raised to it (ADR-0008 §9).
void initialise_cryosphere(const PlanetMesh& mesh, SlowState& slow);

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
    // Sea ice (kg), ocean tiles (ADR-0008 §4.4). The net ocean freshwater
    // flux is ice_melted − ice_frozen.
    double ice_frozen_kg = 0.0;
    double ice_melted_kg = 0.0;
    double ice_change_kg = 0.0;        // Σ A f (m' − m)
    double ice_kg = 0.0;               // Σ A f m' after the step
    double ice_area_north_m2 = 0.0;    // ocean area with ice, by hemisphere
    double ice_area_south_m2 = 0.0;
    double ice_mass_north_kg = 0.0;
    double ice_mass_south_kg = 0.0;
    double snow_area_north_m2 = 0.0;   // land area with snow, by hemisphere
    double snow_area_south_m2 = 0.0;
    // Cover as the radiation sees it (ADR-0008 §4.2): ocean area weighted by
    // min(1, h / h_r) and land area by W / (W + W_m), by hemisphere.
    double ice_cover_north_m2 = 0.0;
    double ice_cover_south_m2 = 0.0;
    double snow_cover_north_m2 = 0.0;
    double snow_cover_south_m2 = 0.0;
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
    // The P2(sin φ) component of the area- and tile-weighted surface
    // temperature, Σ w T P2 / Σ w P2² (K); negative when the poles are cold.
    double p2_surface_temperature_K = 0.0;
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
    // |Δ snow + Δ ice − (snowfall − snow melt + ice frozen − ice melted)|
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
    // Water equivalent over the year (kg), and the snow and ice at its end.
    double snowfall_kg = 0.0;
    double rain_kg = 0.0;
    double melt_kg = 0.0;
    double snow_kg = 0.0;
    double ice_kg = 0.0;
    // Sea-ice area by hemisphere: the year's largest and smallest step-end
    // values (m²).
    double ice_area_north_max_m2 = 0.0;
    double ice_area_north_min_m2 = 0.0;
    double ice_area_south_max_m2 = 0.0;
    double ice_area_south_min_m2 = 0.0;
    // The covers' largest and smallest step-end values, likewise.
    std::array<double, 4> cover_max_m2{};   // ice north, ice south, snow north, snow south
    std::array<double, 4> cover_min_m2{};
    // Snow-covered land area by hemisphere, likewise.
    double snow_area_north_max_m2 = 0.0;
    double snow_area_north_min_m2 = 0.0;
    double snow_area_south_max_m2 = 0.0;
    double snow_area_south_min_m2 = 0.0;
    // Time means over the year (ADR-0009 §4.5).
    std::array<double, 17> northward_transport_W{};
    std::array<double, 18> zonal_mean_surface_temperature_K{};
    double p2_surface_temperature_K = 0.0;

    // The equator-to-pole difference of the P2 fit, −1.5 T₂ (ADR-0009 §10:
    // the calibration target, 42 K for Earth after North et al., 1981).
    [[nodiscard]] double p2_equator_to_pole_K() const noexcept {
        return -1.5 * p2_surface_temperature_K;
    }
    // The larger of the two hemispheres' peak poleward transports.
    [[nodiscard]] double peak_poleward_transport_W() const noexcept;
    // Mean of the two equatorward bands minus mean of the two polar bands.
    [[nodiscard]] double equator_to_pole_difference_K() const noexcept;
    // Latent heat taken by melting minus that released by freezing, as a
    // mean rate over the year (W).
    double latent_W = 0.0;

    // (absorbed − emitted) / absorbed
    [[nodiscard]] double relative_imbalance() const noexcept;
    // The rate of change of stored sensible heat, relative to the absorbed:
    // (absorbed − emitted − latent) / absorbed. Zero once the columns are in
    // balance even while ice keeps growing.
    [[nodiscard]] double relative_storage_rate() const noexcept {
        return (absorbed_W - emitted_W - latent_W) / absorbed_W;
    }
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
