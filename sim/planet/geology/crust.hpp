#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/mesh_dijkstra.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

// Continental crust (task M2-02 §4.6). The propensity of a cell is
// fbm(continental noise) plus its plate's keyed bias; cells are marked
// continental in order of decreasing propensity (ties to the lower CellId)
// until their area reaches continental_area_fraction of the sphere, so the
// result exceeds the target by less than one cell area. Also fills the plates'
// continental_bias and the crust types of every boundary edge.
void assign_continental_crust(const PlanetMesh& mesh, std::uint64_t world_seed,
                              const GeologyParameters& parameters, GeologyState& geology,
                              std::size_t worker_count = 1U);

// Oceanic structural depth (positive, m) for a crust age in seconds:
// Parsons & Sclater (1977), d = d_ridge + c sqrt(t_Myr), below the transition
// age; above it d = d_inf - (d_inf - d_T) exp(-(t - T) / tau), with tau
// chosen so that value and slope match at T. With the default parameters
// tau is about 46 Myr and the depth is continuous and C1.
[[nodiscard]] double oceanic_depth_m(double crust_age_s, const GeologyParameters& parameters);

// Crust age (task M2-02 §4.7). Oceanic age is the along-mesh distance to the
// nearest divergent boundary of the cell's own plate divided by that
// boundary's half-spreading rate, capped; plates without a divergent boundary
// get the cap. Continental age is a keyed value per edge-connected continental
// region. Returns the spreading-path Dijkstra (cost in metres, label =
// boundary index) for diagnostics and tests.
DijkstraResult assign_crust_age(const PlanetMesh& mesh, std::uint64_t world_seed,
                                const GeologyParameters& parameters, GeologyState& geology);

}  // namespace planetsim
