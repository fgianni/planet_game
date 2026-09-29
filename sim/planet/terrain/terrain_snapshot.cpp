#include "sim/planet/terrain/terrain_snapshot.hpp"

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <stdexcept>

namespace planetsim {

TerrainSnapshot make_terrain_snapshot(const PlanetState& state, const GeologyState& geology,
                                      const DrainageState& drainage,
                                      std::size_t worker_count) {
    const PlanetMesh& mesh = state.mesh();
    const std::size_t cell_count = mesh.cell_count();
    if (geology.plate_id.size() != cell_count || geology.crust_type.size() != cell_count ||
        geology.crust_age_s.size() != cell_count ||
        geology.nearest_boundary_class.size() != cell_count ||
        geology.nearest_boundary_distance_m.size() != cell_count) {
        throw std::invalid_argument("terrain snapshot requires a generated geology");
    }
    if (drainage.downstream.size() != cell_count ||
        drainage.basin_id.size() != cell_count ||
        drainage.surface.depression_id.size() != cell_count ||
        drainage.catchment_area_m2.size() != cell_count) {
        throw std::invalid_argument("terrain snapshot requires generated drainage");
    }
    const SlowState& slow = state.slow();
    const SurfaceFractions fractions =
        compute_surface_fractions(mesh, slow.hypsometry_m, slow.sea_level_m, worker_count);

    TerrainSnapshot snapshot;
    snapshot.sea_level_m = slow.sea_level_m;
    snapshot.land_area_fraction = fractions.land_area_fraction;
    snapshot.plate_count = static_cast<std::uint32_t>(geology.plates.size());
    snapshot.drainage_outlet_count = drainage.surface.outlet_count;
    snapshot.drainage_basin_count = drainage.diagnostics.basin_count;
    snapshot.drainage_depression_count =
        static_cast<std::uint32_t>(drainage.surface.depressions.size());
    snapshot.largest_catchment_area_m2 = drainage.diagnostics.largest_catchment_area_m2;
    snapshot.mean_elevation_m.resize(cell_count);
    snapshot.land_fraction.resize(cell_count);
    snapshot.plate_id.resize(cell_count);
    snapshot.crust_type.resize(cell_count);
    snapshot.crust_age_myr.resize(cell_count);
    snapshot.boundary_class.resize(cell_count);
    snapshot.downstream.resize(cell_count);
    snapshot.basin_id.resize(cell_count);
    snapshot.depression_id.resize(cell_count);
    snapshot.catchment_area_m2.resize(cell_count);
    for (std::size_t index = 0; index < cell_count; ++index) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        snapshot.mean_elevation_m[index] =
            static_cast<float>(mean_elevation_m(cell_hypsometry(slow.hypsometry_m, cell)));
        snapshot.land_fraction[index] = fractions.land_fraction[index];
        snapshot.plate_id[index] = geology.plate_id[index].value();
        snapshot.crust_type[index] = static_cast<std::uint8_t>(geology.crust_type[index]);
        snapshot.crust_age_myr[index] =
            static_cast<float>(geology.crust_age_s[index] / seconds_per_million_years);
        const bool on_boundary = geology.nearest_boundary_distance_m[index] == 0.0F;
        snapshot.boundary_class[index] = static_cast<std::uint8_t>(
            on_boundary ? geology.nearest_boundary_class[index] : BoundaryClass::none);
        snapshot.downstream[index] = drainage.downstream[cell];
        snapshot.basin_id[index] = drainage.basin_id[cell];
        snapshot.depression_id[index] = drainage.surface.depression_id[cell];
        snapshot.catchment_area_m2[index] = drainage.catchment_area_m2[cell];
    }
    return snapshot;
}

}  // namespace planetsim
