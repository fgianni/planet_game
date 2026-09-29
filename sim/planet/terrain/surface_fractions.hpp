#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

// Land and ocean fractions of every cell for a given sea level (ADR-0005
// §4.1). Below-sea-level area is ocean only in the world ocean: the
// edge-connected component of cells whose lowest quantile is below sea level
// that contains the deepest cell (lowest bottom quantile, ties to the lower
// CellId). Other such components are inland depressions and stay land in M2.
//
// The anchor replaces ADR-0005's "largest by area" (amendment recorded in
// ADR-0005 §9): components only merge as the sea rises, so the anchored ocean
// only grows and the land fraction is non-increasing in sea level, which the
// largest-area rule does not guarantee when two unconnected basins swap rank.
struct SurfaceFractions {
    Field2D<float> land_fraction;
    Field2D<float> ocean_fraction;
    Field2D<std::uint8_t> ocean_connected;  // 1 in the world-ocean component
    double land_area_fraction = 0.0;        // area-weighted, from double values
    double ocean_area_m2 = 0.0;
    double total_area_m2 = 0.0;
    std::uint32_t below_sea_level_components = 0;
    std::uint32_t ocean_cell_count = 0;     // cells in the world-ocean component
};

[[nodiscard]] SurfaceFractions compute_surface_fractions(const PlanetMesh& mesh,
                                                         const Field3D<float>& hypsometry_m,
                                                         double sea_level_m,
                                                         std::size_t worker_count = 1U);

// Only the area-weighted global land fraction; the same computation as
// compute_surface_fractions without storing per-cell fields.
[[nodiscard]] double land_area_fraction(const PlanetMesh& mesh, const Field3D<float>& hypsometry_m,
                                        double sea_level_m, std::size_t worker_count = 1U);

inline constexpr std::uint32_t sea_level_bisection_iterations = 64U;
inline constexpr double sea_level_target_tolerance = 1e-4;

struct SeaLevelSolution {
    double sea_level_m = 0.0;
    double target_land_fraction = 0.0;
    double achieved_land_fraction = 0.0;
    // Set when the land fraction jumps across the target between the final
    // bracket's ends by more than sea_level_target_tolerance: the returned
    // level is just below the jump and achieved_land_fraction exceeds the
    // target. land_fraction_above_jump is the value just above it.
    bool target_in_jump = false;
    bool jump_changes_ocean_connectivity = false;
    double land_fraction_above_jump = 0.0;
    // Set by generate_terrain: the solved level relative to the generator's
    // original reference, removed from every elevation so that the stored sea
    // level is 0 m and elevations are heights above sea level. Zero from
    // solve_sea_level itself.
    double datum_shift_m = 0.0;
};

// Sea level for a target land fraction by fixed-iteration bisection
// (ADR-0005 §4.1). The bracket keeps land(low) >= target > land(high) and the
// solution is `low`. A target of 0 puts the sea 1 m above every cell's highest
// quantile; a target of 1 puts it 1 m below every cell's lowest quantile.
[[nodiscard]] SeaLevelSolution solve_sea_level(const PlanetMesh& mesh,
                                               const Field3D<float>& hypsometry_m,
                                               double target_land_fraction,
                                               std::size_t worker_count = 1U);

}  // namespace planetsim
