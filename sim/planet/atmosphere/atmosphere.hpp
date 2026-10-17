#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/geology/geology_parameters.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace planetsim {

class PlanetMesh;
struct PlanetParameters;
struct SlowState;
struct SurfaceFractions;

// The layered atmosphere of ADR-0010: every cell carries a column of N
// equal-mass sigma layers (σ = p / p_s, model top at p = 0) over its surface
// pressure p_s. Layer 0 is the bottom one. N is a scenario parameter; 0 means
// no atmosphere.
struct AtmosphereParameters {
    std::uint32_t layer_count = 0U;
    double reference_pressure_Pa = 101'325.0;   // p₀, sea-level pressure at rest
    double critical_lapse_rate_K_m = 6.5e-3;    // Γ_c (ADR-0010 §3.3)
    // Grey longwave (ADR-0010 §3.2 B): the optical depth τ₀ of the column at
    // p₀ (a calibration constant, §4.6; the diffusivity factor is folded
    // in) and the share f_l of its well-mixed, linear-in-pressure part.
    double longwave_optical_depth = 0.0;
    double linear_optical_depth_fraction = 0.1;
    // With the water cycle (ADR-0021 §4.6): τ(p) = τ_d p/p₀ + κ_v W(p), W
    // the column's vapour mass above p, replacing τ₀'s profile; τ_d and κ_v
    // are calibration constants (§4.8).
    double dry_optical_depth = 0.0;           // τ_d
    double vapour_absorption_m2_kg = 0.0;     // κ_v
    // Convective adjustment to Γ_c (§3.3 B); off only in tests of pure
    // radiative equilibrium (V2, V3).
    bool convection = true;
    // Moist convection (ADR-0023 §4.2): with the water cycle, a dry
    // adjustment to the dry adiabat and a Betts–Miller relaxation towards
    // the moist adiabat over τ_bm, the reference at RH_bm, in place of the
    // adjustment to Γ_c and of the condensation time τ_c. Off until the
    // refit (task M8-05) turns it on with the clouds.
    bool moist_convection = false;
    double moist_convection_time_s = 2.0 * 3600.0;     // τ_bm (Betts and Miller, 1986)
    double moist_convection_relative_humidity = 0.8;   // RH_bm
    // Cloud water and precipitation (ADR-0023 §4.3, §12), with moist
    // convection: condensate becomes cloud water, whose excess over q_l0
    // converts to precipitation over τ_a, integrated exactly with the
    // step's condensation as a constant source; precipitation evaporates
    // into the layers it falls through at ε (1 − RH) over τ_e, and cloud
    // water in a subsaturated layer over τ_e, never beyond saturation.
    // q_l0 is 0 (§12: Kessler's 0.3 g/kg, a threshold for resolved clouds,
    // held 1 kg/m² in every cloudy L3 layer); the cloud water then settles
    // at the condensation rate times τ_a. Calibration constants (§4.6).
    double cloud_water_threshold_kg_kg = 0.0;      // q_l0
    double autoconversion_time_s = 3600.0;         // τ_a
    double rain_evaporation_efficiency = 0.5;      // ε
    double rain_evaporation_time_s = 3600.0;       // τ_e
    // Cloud radiation (ADR-0023 §4.4), with moist convection: the fraction
    // from relative humidity above RH_c counting cloud water (Sundqvist et
    // al. 1989), at least f_conv where convection triggers; the in-cloud
    // optical depth 3 LWP / (2 ρ_w r_e) (Stephens 1978); shortwave
    // reflection by the non-absorbing two-stream limit with asymmetry g;
    // longwave absorption κ_l per kilogram of cloud water.
    double cloud_critical_relative_humidity = 0.8;    // RH_c (calibration)
    double convective_cloud_fraction = 0.2;           // f_conv
    double cloud_effective_radius_m = 10e-6;          // r_e (calibration)
    double cloud_asymmetry = 0.85;                    // g
    double cloud_longwave_absorption_m2_kg = 130.0;   // κ_l = 0.13 m²/g
};

inline constexpr std::uint32_t max_atmosphere_layer_count = 8U;

// Dry air (ADR-0010 §4.3).
inline constexpr double dry_air_gas_constant_J_kg_K = 287.04;
inline constexpr double dry_air_heat_capacity_J_kg_K = 1004.64;
inline constexpr double gravitational_constant_m3_kg_s2 = 6.67430e-11;

// The Earth-like column's longwave optical depth τ₀: a calibration
// constant, fitted with the coupled climate circulation carrying the heat
// (ADR-0011 §17.7, task M6-05 step E) to a 288 K mean at L4, seed 1, three
// layers, after 150 coupled years (`planet_cli thermal --subdivision 4
// --years 150 --layers 3 --coupled --optical-depth τ₀`): 1.440 → 287.97 K,
// 1.448 → 288.09 K, 1.456 → 288.20 K; 1.442 is interpolated. c_E stays
// reference mode's fit, and the P2 equator-to-pole difference is 52.2 K
// (peak transport 2.83 PW): the 42 K target waits for latent and ocean
// transport (M7, M11). M5's diffusive fit was 1.3581 with D (below).
inline constexpr double earth_like_longwave_optical_depth = 1.442;

// The vapour path's constants (ADR-0021 §4.6, §4.8, §12): calibration
// constants. Fit (2026-10-09, task M7-07): with the water cycle, the coupled
// circulation, L4, seed 1, three layers, 150 years
// (`planet_cli thermal --subdivision 4 --years 150 --layers 3 --coupled
// --water --dry-depth 1.1536 --vapour-absorption 0.005768 --calibrate 288`):
// the split 0.8 / 0.2 of τ₀ (κ_v's at 50 kg/m²; decided with the user) scaled
// together by bisection to 287.98 K (scale 0.8859). The P2 equator-to-pole
// difference is 53.3 K and the peak poleward transport 3.60 PW: the 42 K
// target waits for M8's clouds and M11's ocean (even inert vapour gives
// about 46 K).
inline constexpr double earth_like_dry_optical_depth = 1.0220175;
inline constexpr double earth_like_vapour_absorption_m2_kg = 0.0051100875;

// dead_rock and aqua_planet: no atmosphere (experiments A and B,
// specification §13.1). earth_like: three layers (design record §15.1) of
// the calibrated optical depth.
[[nodiscard]] AtmosphereParameters atmosphere_parameters_for(PlanetPreset preset) noexcept;

// Throws std::invalid_argument for N above max_atmosphere_layer_count, a
// non-positive or non-finite p₀ or Γ_c, a negative or non-finite τ₀, or f_l
// outside [0, 1].
void validate_atmosphere_parameters(const AtmosphereParameters& parameters);

// g = G M / R².
[[nodiscard]] double surface_gravity_m_s2(const PlanetParameters& parameters);

// κ_c = R_d Γ_c / g: θ_c = T (p₀ / p)^κ_c is uniform on the Γ_c profile.
[[nodiscard]] double critical_lapse_exponent(const AtmosphereParameters& parameters,
                                             double gravity_m_s2) noexcept;

// σ at the mass centre of layer k of N: 1 − (k + ½) / N.
[[nodiscard]] double layer_sigma(std::size_t layer, std::size_t layer_count) noexcept;

// The cell's surface height above sea level (ADR-0010 §4.3): its tiles'
// area-weighted height, the ocean tile at sea level and the land tile at the
// mean of its hypsometry above sea level (the whole curve for a cell outside
// the world ocean, ADR-0005).
void compute_surface_height(const PlanetMesh& mesh, const SlowState& slow,
                            const SurfaceFractions& fractions, Field2D<double>& height_m);

// The layers' dry static energy s_k = c_p T_k + Φ_k (J/kg) of a column of N
// equal-mass σ layers, with Φ_k at each layer's mean-Exner level (task
// M6-03's vertical structure, as the balance). It does not depend on p_s
// (every π_k scales alike) and is linear in Φ_s and the temperatures, so the
// same call with Φ_s = 0 and the temperatures' slopes gives the slopes of s.
void layer_dry_static_energy(double surface_geopotential_m2_s2,
                             std::span<const double> temperature_K, std::span<double> energy_J_kg,
                             double gas_constant_J_kg_K = dry_air_gas_constant_J_kg_K,
                             double heat_capacity_J_kg_K = dry_air_heat_capacity_J_kg_K);

// The tile-mean surface temperature that starts the column: land surface and
// ocean mixed layer weighted by their fractions.
[[nodiscard]] double tile_mean_surface_temperature_K(const SlowState& slow,
                                                     const SurfaceFractions& fractions,
                                                     std::size_t cell) noexcept;

// p_s = p₀ (T_s / (T_s + Γ_c z_s))^(g / (R_d Γ_c)): sea-level pressure p₀
// reduced along the Γ_c profile to the surface at height z_s, temperature
// T_s.
[[nodiscard]] double hydrostatic_surface_pressure_Pa(const AtmosphereParameters& parameters,
                                                     double gravity_m_s2, double height_m,
                                                     double surface_temperature_K) noexcept;

// ADR-0010 §4.3: sizes the atmosphere to the parameters' N and sets every
// column at rest, p_s from hydrostatic_surface_pressure_Pa and the layers on
// the Γ_c profile from the tile-mean surface temperature, floored at the
// grey skin temperature T_s (1/2)^¼. With N = 0, p_s is zero and there are
// no layers. Needs the surface temperatures.
void initialise_atmosphere(const PlanetMesh& mesh, SlowState& slow,
                           const PlanetParameters& planet,
                           const AtmosphereParameters& parameters);

// The air temperature at the surface: layer 0 extrapolated along Γ_c,
// T_0 (p_s / p_0)^κ_c (ADR-0010 §4.4).
[[nodiscard]] double surface_air_temperature_K(double bottom_layer_K, std::size_t layer_count,
                                               double lapse_exponent) noexcept;

// The pressure at sea level below a surface at height z_s along Γ_c from
// the surface air temperature: the inverse of hydrostatic_surface_pressure_Pa.
[[nodiscard]] double sea_level_pressure_Pa(const AtmosphereParameters& parameters,
                                           double gravity_m_s2, double surface_pressure_Pa,
                                           double height_m, double surface_air_K) noexcept;

// Geopotential heights above sea level of the layers' mass centres, by the
// hypsometric equation with the trapezoidal mean temperature in ln p: from
// the surface (at the surface air temperature) to layer 0, then between
// adjacent centres.
void layer_heights_m(std::span<const double> layer_temperature_K, double surface_air_K,
                     double surface_height_m, double gravity_m_s2, std::span<double> height_m);

// Area-weighted global state of the atmosphere (deterministic reduction).
struct AtmosphereDiagnostics {
    std::uint32_t layer_count = 0;
    double mass_kg = 0.0;                        // Σ A p_s / g
    double mean_surface_pressure_Pa = 0.0;
    double min_surface_pressure_Pa = 0.0;
    double max_surface_pressure_Pa = 0.0;
    double mean_sea_level_pressure_Pa = 0.0;
    double min_sea_level_pressure_Pa = 0.0;
    double max_sea_level_pressure_Pa = 0.0;
    double mean_surface_air_temperature_K = 0.0;
    std::vector<double> mean_layer_temperature_K;   // bottom first
    std::vector<double> mean_layer_pressure_Pa;
    std::vector<double> mean_layer_height_m;
};

[[nodiscard]] AtmosphereDiagnostics diagnose_atmosphere(const PlanetMesh& mesh,
                                                        const SlowState& slow,
                                                        const PlanetParameters& planet,
                                                        const AtmosphereParameters& parameters,
                                                        std::size_t worker_count = 1U);

}  // namespace planetsim
