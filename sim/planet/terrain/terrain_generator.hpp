#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

struct TerrainGeneration {
    GeologyState geology;  // in memory only (task M2-02 §4.14)
    SeaLevelSolution sea_level;
};

// Generates a planet from a seed (task M2-02 §2): geology, sub-cell
// hypsometry and the sea level for parameters.target_land_fraction. Writes the
// ADR-0005 slow state (hypsometry_m, sea_level_m) of `state` and returns the
// geology and the sea-level solution. Deterministic and identical for every
// worker count.
[[nodiscard]] TerrainGeneration generate_terrain(PlanetState& state, std::uint64_t world_seed,
                                                 const GeologyParameters& parameters,
                                                 std::size_t worker_count = 1U);

}  // namespace planetsim
