#pragma once

#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/drainage.hpp"

#include <cstdint>
#include <vector>

namespace planetsim {

inline constexpr std::uint32_t terrain_snapshot_schema_version = 2;

// Read-oriented copy of the generated terrain for presentation clients
// (specification §7): per-cell values derived from the ADR-0005 slow state
// plus the in-memory plate and crust structure. Terrain changes only when a
// planet is generated, so clients request it once rather than per frame. It
// is never written back into PlanetState.
struct TerrainSnapshot {
    std::uint32_t schema_version = terrain_snapshot_schema_version;
    double sea_level_m = 0.0;
    double land_area_fraction = 0.0;
    std::uint32_t plate_count = 0;
    std::uint32_t drainage_outlet_count = 0;
    std::uint32_t drainage_basin_count = 0;
    std::uint32_t drainage_depression_count = 0;
    double largest_catchment_area_m2 = 0.0;
    std::vector<float> mean_elevation_m;      // trapezoidal mean of the hypsometry
    std::vector<float> land_fraction;         // ADR-0005 §4.1, at sea_level_m
    std::vector<std::uint16_t> plate_id;
    std::vector<std::uint8_t> crust_type;     // CrustType
    std::vector<float> crust_age_myr;
    std::vector<std::uint8_t> boundary_class; // BoundaryClass of an adjacent plate boundary, or none
    std::vector<std::uint32_t> downstream;    // no_downstream marks outlets/sink
    std::vector<std::uint32_t> basin_id;      // terminal outlet CellId
    std::vector<std::uint32_t> depression_id; // no_depression outside raised components
    std::vector<float> catchment_area_m2;      // upstream land area
};

[[nodiscard]] TerrainSnapshot make_terrain_snapshot(const PlanetState& state,
                                                    const GeologyState& geology,
                                                    const DrainageState& drainage,
                                                    std::size_t worker_count = 1U);

}  // namespace planetsim
