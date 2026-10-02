#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>

namespace planetsim {

// The orography the dynamics sees (ADR-0011 §13): the cells' surface height
// after `passes` conservative two-point averaging passes,
//   z_i ← z_i + (1/A_i) Σ_e β l_e d_e (z_j − z_i),  β = 1/8.
// Each pass is a convex combination of a cell and its neighbours, so it
// creates no new extremes; the edge coefficients are symmetric, so the
// area-weighted mean height is preserved exactly. The filter's length grows
// with the cell spacing: it removes grid-scale steps, not resolved relief.
// The column physics and the surface tiles keep the true heights.
[[nodiscard]] Field2D<double> smooth_dynamics_orography(const PlanetMesh& mesh,
                                                        const Field2D<double>& height_m,
                                                        std::size_t passes,
                                                        std::size_t worker_count = 1U);

// The ADR-0011 §13 rule: the fewest passes (at most `max_passes`) after
// which no two neighbouring cells differ by more than `max_step_m`. Throws
// std::runtime_error if `max_passes` do not suffice.
inline constexpr double dynamics_orography_max_step_m = 800.0;
inline constexpr std::size_t dynamics_orography_max_passes = 256U;

struct DynamicsOrography {
    Field2D<double> height_m;
    std::size_t passes = 0;
    double max_step_m = 0.0;   // achieved
};

[[nodiscard]] DynamicsOrography limit_dynamics_orography_steps(
    const PlanetMesh& mesh, const Field2D<double>& height_m,
    double max_step_m = dynamics_orography_max_step_m,
    std::size_t max_passes = dynamics_orography_max_passes, std::size_t worker_count = 1U);

}  // namespace planetsim
