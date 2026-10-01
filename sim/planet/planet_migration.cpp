#include "sim/planet/planet_migration.hpp"

#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"

namespace planetsim {

SnapshotMigration planet_snapshot_migration(const PlanetParameters& planet,
                                            const SurfaceEnergyParameters& surface,
                                            const AtmosphereParameters& atmosphere) {
    validate_atmosphere_parameters(atmosphere);
    SnapshotMigration migration;
    migration.set_initialiser(2U, [planet, surface](const PlanetMesh& mesh, SlowState& staged) {
        initialise_surface_temperatures(mesh, staged, planet, surface);
    });
    migration.set_initialiser(4U, [](const PlanetMesh& mesh, SlowState& staged) {
        initialise_cryosphere(mesh, staged);
    });
    migration.set_initialiser(5U, [planet, atmosphere](const PlanetMesh& mesh, SlowState& staged) {
        initialise_atmosphere(mesh, staged, planet, atmosphere);
    });
    return migration;
}

}  // namespace planetsim
