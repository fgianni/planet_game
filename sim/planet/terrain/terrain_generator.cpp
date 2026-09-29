#include "sim/planet/terrain/terrain_generator.hpp"

#include "sim/planet/geology/geology_generator.hpp"
#include "sim/planet/terrain/hypsometry.hpp"

#include <chrono>

namespace planetsim {

TerrainGeneration generate_terrain(PlanetState& state, std::uint64_t world_seed,
                                   const GeologyParameters& parameters, std::size_t worker_count) {
    const PlanetMesh& mesh = state.mesh();
    TerrainGeneration generation;
    generation.geology = generate_geology(mesh, world_seed, parameters, worker_count);
    SlowState& slow = state.slow();
    sample_hypsometry(mesh, world_seed, parameters, generation.geology, slow.hypsometry_m,
                      worker_count);
    generation.sea_level =
        solve_sea_level(mesh, slow.hypsometry_m, parameters.target_land_fraction, worker_count);
    slow.sea_level_m = generation.sea_level.sea_level_m;
    const auto drainage_start = std::chrono::steady_clock::now();
    generation.drainage =
        generate_drainage(mesh, slow.hypsometry_m, slow.sea_level_m, worker_count);
    const auto drainage_finish = std::chrono::steady_clock::now();
    generation.drainage_generation_time_ms =
        std::chrono::duration<double, std::milli>(drainage_finish - drainage_start).count();
    return generation;
}

}  // namespace planetsim
