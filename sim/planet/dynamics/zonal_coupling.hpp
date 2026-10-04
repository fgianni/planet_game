#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/dynamics/zonal_circulation.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// The planet seen by the climate mode's zonal circulation (ADR-0011 §4.4
// step 1, §14): the bands' means of the slow state and of the column
// physics' heating.

// c_E for the Earth-like planet (ADR-0011 §15): E = c_E (1 − L²∇²)⁻¹
// [χ |∂θ̄/∂y|²] fitted at L = 2,000 km to reference mode's zonal means
// (tools/zonal_mean_prototype/data/ref_L4_N3.csv). Provisional: §4.8 fits
// it jointly with τ₀ once the circulation carries the climate's heat.
inline constexpr double earth_like_eddy_generation_m4_s2_K2 = 1.39e12;

// §14's model for a planet and its atmosphere: the planet's radius,
// gravity and rotation, dry air, the atmosphere's layers and Γ_c, and the
// Earth-like c_E.
[[nodiscard]] ZonalCirculationParameters zonal_circulation_parameters(
    const PlanetParameters& planet, const AtmosphereParameters& atmosphere,
    std::size_t bands = 36U);

// The band of each cell: latitude bands of equal width from the south.
[[nodiscard]] std::vector<std::size_t> zonal_band_of_cells(const PlanetMesh& mesh,
                                                           std::size_t bands);

struct ZonalDragCoefficients {
    // Bulk drag of the bottom layer's wind, as reference mode's
    // (AtmosphereDynamicsParameters).
    double ocean = 1.5e-3;
    double land = 4.0e-3;
};

// Band means: p_s and the dynamics' surface height by area; T, Q⁰ and Λ by
// mass (area × p_s); C_D from the land fraction by area. No Rayleigh
// friction. Sequential sums in cell order, so the result does not depend on
// how the heating was computed in parallel.
[[nodiscard]] ZonalForcing zonal_forcing_from_state(const PlanetMesh& mesh,
                                                    const SlowState& slow,
                                                    const AtmosphereHeating& heating,
                                                    const Field2D<double>& dynamics_height_m,
                                                    const SurfaceFractions& fractions,
                                                    std::size_t bands,
                                                    const ZonalDragCoefficients& drag = {});

}  // namespace planetsim
