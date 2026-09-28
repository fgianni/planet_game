#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/mesh_dijkstra.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace planetsim {

// Along-mesh distance (m, centroid-to-centroid path length) from the sources.
[[nodiscard]] DijkstraResult along_mesh_distance(
    const PlanetMesh& mesh, std::span<const DijkstraSource> sources,
    double maximum_distance_m = std::numeric_limits<double>::infinity());

// Both cells of every boundary edge accepted by the predicate, at distance
// zero, labelled with the boundary's index in geology.boundaries.
[[nodiscard]] std::vector<DijkstraSource>
boundary_sources(const GeologyState& geology,
                 const std::function<bool(const PlateBoundaryEdge&)>& accept);

// Gaussian profile with half-width at half-maximum w: exp(-ln 2 (d / w)^2).
[[nodiscard]] double boundary_profile(double distance_m, double half_width_m) noexcept;

// Structures scale with the boundary's relevant relative speed (closing for
// convergent, opening for divergent, shear for transform) divided by the
// median for its class, clamped to [0, boundary_speed_scale_max].
[[nodiscard]] std::vector<double> boundary_speed_scales(const GeologyState& geology,
                                                        const GeologyParameters& parameters);

// Largest explicit diffusion number kappa dt for which one step of
// h += kappa dt lap(h) is stable on this mesh with a factor-two margin:
// 0.5 min_i A_i / sum_e (l_e / d_e).
[[nodiscard]] double stable_diffusion_step_m2(const PlanetMesh& mesh, std::size_t worker_count = 1U);

// Structural (cell-centre) elevation, task M2-02 §4.8--4.9: crust base,
// passive margins, boundary structures, roughness and diffusive erosion.
// Requires plates, boundaries, crust type and crust age. Fills
// structural_elevation_m, nearest_boundary_class and
// nearest_boundary_distance_m.
//
// Distance convention: cells adjacent to a boundary edge (or, for passive
// margins, to a crust-type change within a plate) are at distance zero, so a
// structure narrower than a cell is carried by the first row of cells rather
// than aliased away.
void compute_structural_elevation(const PlanetMesh& mesh, std::uint64_t world_seed,
                                  const GeologyParameters& parameters, GeologyState& geology,
                                  std::size_t worker_count = 1U);

}  // namespace planetsim
