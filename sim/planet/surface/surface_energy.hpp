#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "sim/planet/surface/surface_materials.hpp"

#include <array>
#include <cstddef>
#include <functional>

namespace planetsim {

class PlanetMesh;
class PlanetState;
class Scheduler;
struct PlanetParameters;
struct SlowState;
struct SurfaceFractions;

// Surface energy of ADR-0007: a land tile and an ocean tile per cell, each a
// two-layer column, radiating εσT⁴ to space through an optional single-layer
// grey atmosphere of longwave emissivity g, or, with ADR-0010's layered
// atmosphere (N > 0, g = 0), into the cell's atmospheric column.
struct SurfaceEnergyParameters {
    SurfaceMaterial land_material = SurfaceMaterial::dry_soil;
    double grey_emissivity = 0.0;   // g in [0, 1)
    // ADR-0009: North's unit-sphere diffusion coefficient D (W/m²/K) of the
    // cells' air temperature (§11); the conductance is K = D R². Zero
    // switches horizontal transport off; non-zero needs air exchange.
    double transport_coefficient_W_m2_K = 0.0;
    // ADR-0009 §10: exchange γ (W/m²/K) of every tile with its cell's air,
    // or with N > 0 with the surface air of its column (ADR-0010 §4.4).
    // Zero: no shared air.
    double air_exchange_W_m2_K = 0.0;
    // ADR-0010: the layered atmosphere; N = 0 keeps the grey layer.
    AtmosphereParameters atmosphere;
};

// ADR-0009 §10: the bulk sensible-heat exchange ρ c_p C_H U with
// ρ = 1.2 kg/m³, c_p = 1005 J/kg/K, C_H = 1.2e-3 and U = 7 m/s.
inline constexpr double bulk_air_exchange_W_m2_K = 10.0;

// Earth-like optical depth τ₀ (earth_like_longwave_optical_depth, in
// sim/planet/atmosphere/atmosphere.hpp) and transport coefficient D:
// calibration constants (ADR-0010 §4.6, §11; ADR-0009 §4.4 and §10–12;
// specification §24), refitted when clouds (M8) and ocean transport (M11)
// arrive.
//
// Fit (2026-10-02, task M5-04): alternating bisection over the last of 150
// spin-up years, earth_like preset with three atmosphere layers, L4, seed 1,
// sea ice as floes and leads (ADR-0008 §10), no precipitation, transported
// heat entering the bottom layer (ADR-0010 §11), to a 288 K length-weighted
// global mean surface temperature and a 42 K equator-to-pole difference of
// the P2 fit (North et al., 1981) (`planet_cli thermal --subdivision 4
// --years 150 --layers 3 --calibrate 288 --calibrate-gradient 42`). The fit
// alternates between (τ₀, D) = (1.3581, 0.6371): 287.92 K, 41.96 K and
// (1.3624, 0.6458): 288.02 K, 41.67 K; the first is kept. Peak poleward
// transport 3.82 PW; sea ice 6.1–9.5 million km² in the north and 24.4–25.6
// in the south. At L5: 288.52 K, 40.9 K, 3.71 PW, sea ice 4.6–9.9 million
// km² in the north and 22.5–24.0 in the south, relative imbalance −7.5e-3
// after 150 years (perennial ice still thickening). Since M6-05 (ADR-0011
// §17.4) D is only the climate circulation's fallback, with the coupled τ₀.
//
// Earlier fits of the grey layer (ADR-0007 §3.2 C), retired at M5:
// (g, D) = (0.4965, 0.6400) with sea ice (2026-10-01, task M4-04), and
// (0.4455, 0.1999) before it (2026-09-30).
inline constexpr double earth_like_transport_coefficient_W_m2_K = 0.6371;

// dead_rock: rock, g = 0, no transport. aqua_planet: g = 0, no transport
// (experiments A and B have no atmosphere and no currents; only the ocean
// tile has area). earth_like: dry soil (until hydrology supplies moisture,
// M9), the three-layer atmosphere with the calibrated τ₀ (the grey layer is
// retired, g = 0), and the calibrated D.
[[nodiscard]] SurfaceEnergyParameters surface_energy_parameters_for(PlanetPreset preset) noexcept;

// Annual-mean insolation of every cell: the length-weighted mean of the
// twelve sub-step means of orbital year 0, in double.
void compute_annual_mean_insolation(const PlanetMesh& mesh, const PlanetParameters& parameters,
                                    Field2D<double>& annual_mean_W_m2,
                                    std::size_t worker_count = 1U);

// ADR-0007 §4.5: both tiles of every cell at the closed-form radiative
// equilibrium of their annual-mean insolation, both layers equal. Under a
// layered atmosphere the grey layer of that equilibrium has g = 1 − exp(−τ₀)
// (at most 0.95), a warm start (ADR-0010 §4.6).
void initialise_surface_temperatures(const PlanetMesh& mesh, SlowState& slow,
                                     const PlanetParameters& parameters,
                                     const SurfaceEnergyParameters& surface,
                                     std::size_t worker_count = 1U);

// A new planet's climate state: the surface temperatures, the cryosphere and
// the atmosphere with the surface parameters' layer count (ADR-0010 §4.3).
void initialise_climate(const PlanetMesh& mesh, SlowState& slow,
                        const PlanetParameters& parameters,
                        const SurfaceEnergyParameters& surface, std::size_t worker_count = 1U);

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
    // The coupled transport (ADR-0011 §17.1): whether the circulation carried
    // the heat this step, and its eddy and advective parts (Σ A |H|).
    bool transport_coupled = false;
    double transport_eddy_absolute_W = 0.0;
    double transport_advective_absolute_W = 0.0;
    double transport_solve_s = 0.0;   // wall-clock of the transport solve, records only
    // The atmosphere (ADR-0010 §4.4), zero without one. emitted_W is then the
    // outgoing longwave at the top, storage_change_J and stored_energy_J
    // include the layers, and transport_W is the heat the columns received.
    std::size_t atmosphere_layers = 0;
    double atmosphere_storage_change_J = 0.0;   // Σ A C Σ_k ΔT_k
    double surface_upward_longwave_W = 0.0;     // emission and reflection into the base
    double downward_longwave_W = 0.0;           // Σ A D at the surface
    double sensible_heat_W = 0.0;               // Σ A H, surface to air
    double max_column_residual_W_m2 = 0.0;      // of the layer equations
    double max_column_correction_K = 0.0;       // ColumnSolveResult::max_correction_K
    int max_column_iterations = 0;
    std::size_t unconverged_columns = 0;
    double convective_area_m2 = 0.0;            // columns the adjustment changed
    // Area-weighted mean temperature of each layer after the step, bottom
    // first (layers beyond N are 0).
    std::array<double, max_atmosphere_layer_count> mean_layer_temperature_K{};
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
                                             std::size_t worker_count = 1U,
                                             const CirculationTransport* circulation = nullptr);

// Registers the surface as a climate-mode process (sub-step mean forcing,
// ADR-0006 §4.3) and a reference-mode process (instantaneous forcing at the
// step's midpoint tick). The references must outlive the scheduler; `last`,
// if given, receives each step's diagnostics. With `reference_diffusion`
// false the reference-mode steps run without ADR-0009's diffusion, because
// resolved winds carry the transport there (ADR-0011 §4.3).
void register_surface_energy(Scheduler& scheduler, PlanetState& state,
                             const PlanetParameters& parameters,
                             const SurfaceEnergyParameters& surface,
                             const SurfaceFractions& fractions, std::size_t worker_count = 1U,
                             SurfaceEnergyDiagnostics* last = nullptr,
                             bool reference_diffusion = true,
                             const CirculationTransport* circulation = nullptr);

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
    // The worst step of the year against the V2 closure gate (residual/gate).
    double worst_closure_ratio = 0.0;
    // The atmosphere (ADR-0010), time means over the year: layer
    // temperatures (bottom first), the surface's longwave streams and
    // sensible heat (W), and the share of the planet's area whose columns
    // convective adjustment changed; the year's largest column residual and
    // iteration count, and the number of unconverged column solves.
    std::array<double, max_atmosphere_layer_count> mean_layer_temperature_K{};
    double surface_upward_longwave_W = 0.0;
    double downward_longwave_W = 0.0;
    double sensible_heat_W = 0.0;
    double convective_area_fraction = 0.0;
    double max_column_residual_W_m2 = 0.0;
    double max_column_correction_K = 0.0;
    int max_column_iterations = 0;
    std::size_t unconverged_columns = 0;

    // (absorbed − emitted) / absorbed
    [[nodiscard]] double relative_imbalance() const noexcept;
    // The rate of change of stored sensible heat, relative to the absorbed:
    // (absorbed − emitted − latent) / absorbed. Zero once the columns are in
    // balance even while ice keeps growing.
    [[nodiscard]] double relative_storage_rate() const noexcept {
        return (absorbed_W - emitted_W - latent_W) / absorbed_W;
    }
};

// Called before each spin-up sub-step, after its insolation is set: returns
// the circulation's transport for the step, or null (ADR-0011 §17).
using SpinUpCirculation =
    std::function<const CirculationTransport*(PlanetState&, const ClimateSubstep&)>;

// Spin-up (ADR-0007 §4.5, ADR-0006 §4.2): runs `years` orbital years of
// climate sub-steps outside any scenario run, without touching a clock, and
// returns the summary of the last year. With `circulation`, each step first
// runs it (a coupled spin-up, task M6-05).
AnnualSurfaceSummary spin_up_surface_energy(PlanetState& state,
                                            const PlanetParameters& parameters,
                                            const SurfaceEnergyParameters& surface,
                                            const SurfaceFractions& fractions, int years,
                                            std::size_t worker_count = 1U,
                                            const SpinUpCirculation& circulation = {});


// The atmosphere's diabatic heating at the slow state, for the climate
// mode's circulation (ADR-0011 §14): per layer and cell (layer-major),
//   Q⁰_k = [ε_k (U_k + D_{k+1}) − 2 ε_k σ T_k⁴ + δ_k0 H] / C,
// the column physics' longwave and sensible heating of the step at the
// slow state's layer temperatures, in K/s (C = c_p p_s / (g N)). The surface
// is held: its tiles step over one minute under this insolation, so land and
// open water keep their slow-state temperatures while sea-ice floes, which
// store no heat, take their balance temperature. (A climate step's whole Δt
// would let the surface run ahead of the held air: on the Earth-like planet
// the air then gained a net 33 W/m².) Λ_k = ∂Q_k/∂T_k holds the surface too
// (only its reflection follows the air, and the sensible heat follows only
// the air): the column's radiative and sensible damping, negative
// (ADR-0010 §4.6). Convective adjustment is not included; the
// circulation applies its own. Each cell is independent, so the result is
// the same for any worker count. Throws std::invalid_argument without an
// atmosphere.
struct AtmosphereHeating {
    Field3D<double> rate_K_s;
    Field3D<double> derivative_s;
    // The columns' radiation budget per cell (W/m²), for checks: outgoing
    // longwave, the longwave the surface sends up and receives, and the
    // sensible heat it gives the air.
    Field2D<double> outgoing_W_m2;
    Field2D<double> surface_upward_W_m2;
    Field2D<double> surface_downward_W_m2;
    Field2D<double> sensible_W_m2;
};

void compute_atmosphere_heating(const PlanetState& state, const PlanetParameters& parameters,
                                const SurfaceEnergyParameters& surface,
                                const SurfaceFractions& fractions,
                                const Field2D<float>& insolation_W_m2,
                                AtmosphereHeating& heating, std::size_t worker_count = 1U);

}  // namespace planetsim
