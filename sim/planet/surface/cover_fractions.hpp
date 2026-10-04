#pragma once

#include "sim/planet/surface/cryosphere_constants.hpp"

#include <algorithm>

namespace planetsim {

// ADR-0008 cover definitions shared by the surface physics and presentation.
// Callers validate that their mass inputs are finite and non-negative.
[[nodiscard]] inline double snow_cover_fraction(double swe_kg_m2) noexcept {
    return swe_kg_m2 / (swe_kg_m2 + snow_masking_kg_m2);
}

[[nodiscard]] inline double sea_ice_cover_fraction(double ice_kg_m2) noexcept {
    return std::min(1.0, ice_kg_m2 / (sea_ice_density_kg_m3 * sea_ice_albedo_ramp_m));
}

}  // namespace planetsim
