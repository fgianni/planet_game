#pragma once

namespace planetsim {

// ADR-0008 §4.2: documented starting values, not fits.
inline constexpr double latent_heat_of_fusion_J_kg = 3.34e5;
inline constexpr double melting_point_K = 273.15;             // fresh snow and ice surface
inline constexpr double seawater_freezing_point_K = 271.35;   // −1.8 °C at salinity 35
inline constexpr double sea_ice_density_kg_m3 = 917.0;
inline constexpr double sea_ice_conductivity_W_m_K = 2.03;    // Maykut and Untersteiner (1971)
inline constexpr double snow_albedo = 0.75;
inline constexpr double sea_ice_albedo = 0.55;
inline constexpr double snow_masking_kg_m2 = 10.0;            // cover W / (W + W_m)
inline constexpr double sea_ice_albedo_ramp_m = 0.5;

}  // namespace planetsim
