#include "sim/planet/terrain/terrain_generator.hpp"

#include "sim/planet/geology/geology_generator.hpp"
#include "sim/planet/terrain/hypsometry.hpp"

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

    // Re-datum at the generated sea level so that the stored sea level is
    // 0 m and every elevation is a height above sea level (ADR-0005 §9.5).
    // The shift is uniform, so fractions, ocean connectivity and drainage are
    // unchanged; the land fraction is re-evaluated at 0 m because shifting the
    // float quantiles re-rounds them.
    const double datum_m = generation.sea_level.sea_level_m;
    const auto shift = [datum_m](float& value) {
        value = static_cast<float>(static_cast<double>(value) - datum_m);
    };
    for (std::size_t layer = 0; layer < slow.hypsometry_m.layer_count(); ++layer) {
        for (float& value : slow.hypsometry_m.layer(layer)) {
            shift(value);
        }
    }
    for (float& value : generation.geology.structural_elevation_m.values()) {
        shift(value);
    }
    generation.sea_level.datum_shift_m = datum_m;
    generation.sea_level.sea_level_m = 0.0;
    generation.sea_level.achieved_land_fraction =
        land_area_fraction(mesh, slow.hypsometry_m, 0.0, worker_count);
    slow.sea_level_m = 0.0;
    generation.drainage =
        generate_drainage(mesh, slow.hypsometry_m, slow.sea_level_m, worker_count);
    return generation;
}

}  // namespace planetsim
