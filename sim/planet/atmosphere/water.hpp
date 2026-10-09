#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/planet_state.hpp"

namespace planetsim {

// The bucket's capacity, W_max (Manabe, 1969; ADR-0021 §4.3).
inline constexpr double bucket_capacity_kg_m2 = 150.0;
// The declared start of the water reservoirs (ADR-0021 §4.1): each layer at
// this relative humidity of its temperature and pressure (σ_k p_s), and the
// bucket half full.
inline constexpr double initial_relative_humidity = 0.6;

// Sets the layers' humidity and the bucket from an initialised atmosphere;
// without an atmosphere, no humidity layers and an empty bucket. The schema
// 5 → 6 migration's initialiser and every new run's (initialise_climate).
void initialise_water(const PlanetMesh& mesh, SlowState& slow);

}  // namespace planetsim
