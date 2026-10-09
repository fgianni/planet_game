#pragma once

#include "sim/core/serialization/snapshot_migration.hpp"

namespace planetsim {

struct AtmosphereParameters;
struct PlanetParameters;
struct SurfaceEnergyParameters;

// The planet layer's initialisers for the snapshot migration chain
// (ADR-0003 §3.6, task M5-01): schema 1 -> 2 the surface temperatures at
// their closed-form equilibrium (ADR-0007 §4.6); 3 -> 4 no snow and no sea
// ice (ADR-0008 §4.6); 4 -> 5 the atmosphere at hydrostatic rest with the
// scenario's layer count (ADR-0010 §4.3); 5 -> 6 the layers' humidity at 60%
// relative humidity and the land's bucket half full (ADR-0021 §4.1).
[[nodiscard]] SnapshotMigration planet_snapshot_migration(const PlanetParameters& planet,
                                                          const SurfaceEnergyParameters& surface,
                                                          const AtmosphereParameters& atmosphere);

}  // namespace planetsim
