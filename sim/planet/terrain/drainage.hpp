#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace planetsim {

inline constexpr std::uint32_t no_depression =
    std::numeric_limits<std::uint32_t>::max();

struct DepressionRecord {
    std::uint32_t id = no_depression;
    CellId minimum_cell = CellId::invalid();
    CellId spill_cell = CellId::invalid();
    double spill_level_m = 0.0;
    double raised_area_m2 = 0.0;
    double maximum_fill_depth_m = 0.0;
    std::uint32_t cell_count = 0U;
};

// Intermediate derived terrain used to construct the static routing graph.
// The priority-flood and all comparisons use double internally; elevations
// are retained as float after filling, following the field precision policy.
// This state is regenerated from the authoritative hypsometry and sea level
// and is neither registered nor persisted.
struct DrainageSurface {
    Field2D<float> land_fraction;
    Field2D<std::uint8_t> outlet;
    Field2D<float> drainage_elevation_m;
    Field2D<float> filled_elevation_m;
    Field2D<std::uint32_t> depression_id;
    std::vector<DepressionRecord> depressions;
    CellId terminal_sink = CellId::invalid();  // valid only without ocean outlets
    std::uint32_t outlet_count = 0U;
    double maximum_fill_depth_m = 0.0;
};

// ADR-0005 §4.2 deterministic priority-flood. Ocean-connected cells with
// ocean_fraction > 0 seed the flood. If no such cells exist, the lowest
// drainage-elevation cell (ties by CellId) is the sole terminal sink.
//
// Drainage elevation is the conditional land-part mean in coastal cells and
// the complete hypsometric mean in fully land cells. Raised equal-level
// components receive deterministic depression IDs and one canonical spill
// cell outside the component.
[[nodiscard]] DrainageSurface fill_drainage_depressions(
    const PlanetMesh& mesh,
    const Field3D<float>& hypsometry_m,
    double sea_level_m,
    std::size_t worker_count = 1U);

}  // namespace planetsim
