#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"
#include "tests/test_support.hpp"

#include <memory>

int main() {
    planetsim::test::Context test;
    const auto mesh =
        std::make_shared<const planetsim::PlanetMesh>(planetsim::make_icosphere(3, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    const auto generation =
        planetsim::generate_terrain(state, 11U, planetsim::GeologyParameters{}, 2U);
    const auto snapshot =
        planetsim::make_terrain_snapshot(state, generation.geology, generation.drainage, 2U);
    const auto fractions = planetsim::compute_surface_fractions(
        *mesh, state.slow().hypsometry_m, state.slow().sea_level_m);

    const std::size_t cells = mesh->cell_count();
    PLANETSIM_EXPECT(test, snapshot.schema_version == planetsim::terrain_snapshot_schema_version);
    PLANETSIM_EXPECT(test, snapshot.sea_level_m == state.slow().sea_level_m);
    PLANETSIM_EXPECT(test, snapshot.land_area_fraction == fractions.land_area_fraction);
    PLANETSIM_EXPECT(test, snapshot.plate_count == generation.geology.plates.size());
    PLANETSIM_EXPECT(test,
                     snapshot.drainage_outlet_count == generation.drainage.surface.outlet_count);
    PLANETSIM_EXPECT(test,
                     snapshot.drainage_basin_count == generation.drainage.diagnostics.basin_count);
    PLANETSIM_EXPECT(test, snapshot.drainage_depression_count ==
                               generation.drainage.surface.depressions.size());
    PLANETSIM_EXPECT(test, snapshot.largest_catchment_area_m2 ==
                               generation.drainage.diagnostics.largest_catchment_area_m2);
    PLANETSIM_EXPECT(test, snapshot.mean_elevation_m.size() == cells &&
                               snapshot.land_fraction.size() == cells &&
                               snapshot.plate_id.size() == cells &&
                               snapshot.crust_type.size() == cells &&
                               snapshot.crust_age_myr.size() == cells &&
                               snapshot.boundary_class.size() == cells &&
                               snapshot.downstream.size() == cells &&
                               snapshot.basin_id.size() == cells &&
                               snapshot.depression_id.size() == cells &&
                               snapshot.catchment_area_m2.size() == cells);
    std::size_t boundary_cells = 0;
    for (const auto& cell : mesh->cells()) {
        const std::size_t index = cell.id.to_index();
        const auto quantiles = planetsim::cell_hypsometry(state.slow().hypsometry_m, cell.id);
        PLANETSIM_EXPECT(test, snapshot.mean_elevation_m[index] ==
                                   static_cast<float>(planetsim::mean_elevation_m(quantiles)));
        PLANETSIM_EXPECT(test, snapshot.land_fraction[index] == fractions.land_fraction[index]);
        PLANETSIM_EXPECT(test, snapshot.plate_id[index] == generation.geology.plate_id[index].value());
        const bool adjacent = generation.geology.nearest_boundary_distance_m[index] == 0.0F;
        PLANETSIM_EXPECT(test, adjacent == (snapshot.boundary_class[index] != 0U));
        PLANETSIM_EXPECT(test,
                         snapshot.downstream[index] == generation.drainage.downstream[cell.id]);
        PLANETSIM_EXPECT(test,
                         snapshot.basin_id[index] == generation.drainage.basin_id[cell.id]);
        PLANETSIM_EXPECT(test, snapshot.depression_id[index] ==
                                   generation.drainage.surface.depression_id[cell.id]);
        PLANETSIM_EXPECT(test, snapshot.catchment_area_m2[index] ==
                                   generation.drainage.catchment_area_m2[cell.id]);
        boundary_cells += adjacent ? 1U : 0U;
    }
    PLANETSIM_EXPECT(test, boundary_cells > 0U);

    planetsim::GeologyState empty;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::make_terrain_snapshot(state, empty, generation.drainage));
    planetsim::DrainageState empty_drainage;
    PLANETSIM_EXPECT_THROWS(
        test, std::invalid_argument,
        planetsim::make_terrain_snapshot(state, generation.geology, empty_drainage));
    return test.result();
}
