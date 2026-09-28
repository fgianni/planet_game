#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

// Plate-scale geology from a seed (task M2-02 §2): plates and their motion,
// boundary classification, continental crust, crust age and structural
// elevation. Deterministic for (mesh, world_seed, parameters) and identical
// for every worker count.
[[nodiscard]] GeologyState generate_geology(const PlanetMesh& mesh, std::uint64_t world_seed,
                                            const GeologyParameters& parameters,
                                            std::size_t worker_count = 1U);

}  // namespace planetsim
