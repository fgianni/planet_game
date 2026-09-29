#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/drainage.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

struct TerrainGeneration {
    GeologyState geology;  // in memory only (task M2-02 §4.14)
    SeaLevelSolution sea_level;
    DrainageState drainage;  // derived; regenerated after load
    double drainage_generation_time_ms = 0.0;  // diagnostic only; never state
};

// Generates geology, sub-cell hypsometry, sea level and the static drainage
// graph. Only hypsometry and sea level are authoritative slow state; geology
// and drainage remain derived in memory. The result is deterministic and
// identical for every worker count.
[[nodiscard]] TerrainGeneration generate_terrain(PlanetState& state, std::uint64_t world_seed,
                                                 const GeologyParameters& parameters,
                                                 std::size_t worker_count = 1U);

}  // namespace planetsim
