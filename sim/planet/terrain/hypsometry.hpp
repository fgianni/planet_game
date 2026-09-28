#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/fields/field_registry.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace planetsim {

// Sub-cell hypsometry (ADR-0005 §4.1): nine elevation quantiles per cell at
// cumulative area fractions 0, 1/8, ..., 1, in metres relative to the
// reference radius, non-decreasing.
inline constexpr std::size_t hypsometry_quantile_count = hypsometry_layer_count;
using HypsometryQuantiles = std::array<float, hypsometry_quantile_count>;

// Sub-cell sample points per fan triangle (cell centre, corner k, corner
// k+1): the permutations of (2/3, 1/6, 1/6) and (1/6, 5/12, 5/12), each
// weighted by one sixth of the triangle's spherical area (task M2-02 §4.10).
inline constexpr std::size_t hypsometry_points_per_triangle = 6U;

struct ElevationSample {
    double elevation_m = 0.0;
    double weight = 0.0;  // area, any consistent unit
};

// Area-weighted quantiles at 0, 1/8, ..., 1. Samples are sorted by elevation
// (stable on ties); each sits at the midpoint of its cumulative weight, and
// quantiles interpolate linearly between those positions, so fraction 0 is the
// minimum and fraction 1 the maximum. Reorders `samples`.
[[nodiscard]] HypsometryQuantiles weighted_elevation_quantiles(std::span<ElevationSample> samples);

// Area fraction of a cell strictly below `level_m`, by linear interpolation
// of the quantile curve: 0 at or below the lowest quantile, 1 above the
// highest, non-decreasing in level_m (ADR-0005 §4.1). It is continuous except
// where quantiles coincide: a flat stretch of the curve is area at one
// elevation, which counts as below only once level_m exceeds it.
[[nodiscard]] double below_fraction(std::span<const float, hypsometry_quantile_count> quantiles,
                                    double level_m) noexcept;

// Mean elevation by trapezoidal integration of the quantile curve.
[[nodiscard]] double mean_elevation_m(
    std::span<const float, hypsometry_quantile_count> quantiles) noexcept;

// The quantiles of one cell gathered from the layer-major field.
[[nodiscard]] HypsometryQuantiles cell_hypsometry(const Field3D<float>& hypsometry_m, CellId cell);

// Area of the spherical triangle spanned by three unit vectors (steradians).
[[nodiscard]] double spherical_triangle_area_unit(const Vec3d& first, const Vec3d& second,
                                                  const Vec3d& third) noexcept;

// Samples the structural elevation into the hypsometry field (layer-major,
// hypsometry_quantile_count layers). At each sub-cell point the elevation is
// the barycentric interpolation of the structural elevation at the cell
// centre and the two corners (a corner's value is the mean of its three
// cells) plus unit-RMS noise times the cell's crust-dependent sub-cell
// roughness amplitude.
void sample_hypsometry(const PlanetMesh& mesh, std::uint64_t world_seed,
                       const GeologyParameters& parameters, const GeologyState& geology,
                       Field3D<float>& hypsometry_m, std::size_t worker_count = 1U);

}  // namespace planetsim
