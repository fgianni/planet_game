#pragma once

namespace planetsim {

// Saturation and latent heat (ADR-0021 §4.2): one set of functions for
// evaporation, condensation and the presentation.

inline constexpr double water_vapour_gas_constant_J_kg_K = 461.5;
inline constexpr double vapour_molar_ratio = 287.04 / water_vapour_gas_constant_J_kg_K;   // ε
inline constexpr double freezing_point_K = 273.15;
inline constexpr double latent_heat_vaporisation_J_kg = 2.501e6;   // L_v at 0 °C
// L_s = L_v + L_f exactly, with ADR-0008's L_f = 3.34e5 J/kg
// (cryosphere_constants.hpp; checked in column_step.cpp), so that a
// sublimation and its condensate's freezing conserve energy.
inline constexpr double latent_heat_sublimation_J_kg = latent_heat_vaporisation_J_kg + 3.34e5;

// e_s (Pa) over liquid water at and above 0 °C (Bolton, 1980) and over ice
// below it (Murphy and Koop, 2005); both are continuous at 0 °C to 0.01%.
[[nodiscard]] double saturation_vapour_pressure_Pa(double temperature_K) noexcept;
// de_s/dT (Pa/K), on the same branch.
[[nodiscard]] double saturation_vapour_pressure_slope_Pa_K(double temperature_K) noexcept;

// q_sat = ε e_s / (p − (1 − ε) e_s), at most 1 (where e_s reaches p).
[[nodiscard]] double saturation_specific_humidity(double temperature_K, double pressure_Pa) noexcept;
// dq_sat/dT (1/K) at fixed p.
[[nodiscard]] double saturation_specific_humidity_slope(double temperature_K,
                                                        double pressure_Pa) noexcept;

// L_v above 0 °C, L_s at and below it: the phase the saturation branch
// condenses to or evaporates from.
[[nodiscard]] double latent_heat_J_kg(double temperature_K) noexcept;

}  // namespace planetsim
