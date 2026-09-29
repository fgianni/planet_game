#pragma once

#include <cstdint>

namespace planetsim {

struct PlanetParameters;

// Surface materials of ADR-0007 §4.2, from standard textbook conductivities
// and volumetric heat capacities (e.g. Oke, Boundary Layer Climates;
// Hartmann, Global Physical Climatology). Values are documented physics, not
// calibration constants.
enum class SurfaceMaterial : std::uint8_t { rock, dry_soil, wet_soil, ocean };

struct MaterialProperties {
    double conductivity_W_m_K = 0.0;           // land only
    double volumetric_heat_capacity_J_m3_K = 0.0;  // land only
    double albedo = 0.0;
    double emissivity = 1.0;
};

[[nodiscard]] MaterialProperties material_properties(SurfaceMaterial material) noexcept;

// The two layers of one tile's column: a surface layer over a ground (land)
// or deep (ocean) layer, coupled by a conductance.
struct ColumnProperties {
    double surface_heat_capacity_J_m2_K = 0.0;
    double lower_heat_capacity_J_m2_K = 0.0;
    double exchange_W_m2_K = 0.0;
    double albedo = 0.0;
    double emissivity = 1.0;
};

// The ocean column: the prototype's calibrated mixed layer (~70 m), deep
// layer and exchange (docs/prototype-oracle.md §3).
inline constexpr double ocean_mixed_layer_heat_capacity_J_m2_K = 2.9e8;
inline constexpr double ocean_deep_heat_capacity_J_m2_K = 2.5e9;
inline constexpr double ocean_deep_exchange_W_m2_K = 0.7;

// Land layers are derived from the material and the planet: the surface layer
// is the diurnal skin depth sqrt(2κ/ω_day) over the synodic day, the ground
// layer the annual one over the orbital year, and the conductance is k over
// the distance between the layer centres.
[[nodiscard]] ColumnProperties column_properties(SurfaceMaterial material,
                                                 const PlanetParameters& parameters);

}  // namespace planetsim
