#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace planetsim {

enum class FieldId : std::uint32_t {
    top_of_atmosphere_insolation_W_m2 = 0x0001'0001U,
    substep_mean_insolation_W_m2 = 0x0001'0002U,
    hypsometry_m = 0x0002'0001U,
    sea_level_m = 0x0002'0002U,
    land_surface_temperature_K = 0x0003'0001U,
    land_ground_temperature_K = 0x0003'0002U,
    // 0x0003'0003 is retired: the schema 2 float32 mixed layer.
    ocean_deep_temperature_K = 0x0003'0004U,
    ocean_mixed_layer_temperature_K = 0x0003'0005U,
    land_snow_water_equivalent_kg_m2 = 0x0004'0001U,
    sea_ice_mass_kg_m2 = 0x0004'0002U,
    prescribed_precipitation_kg_m2_s = 0x0004'0003U,
    surface_temperature_K = 0x0004'0004U,
    climatology_surface_temperature_mean_K = 0x0004'0005U,
    climatology_surface_temperature_variance_K2 = 0x0004'0006U,
    climatology_land_snow_mean_kg_m2 = 0x0004'0007U,
    climatology_sea_ice_mean_kg_m2 = 0x0004'0008U,
    atmosphere_surface_pressure_Pa = 0x0005'0001U,
    atmosphere_temperature_K = 0x0005'0002U,
    atmosphere_edge_normal_wind_m_s = 0x0006'0001U,
    atmosphere_eastward_wind_m_s = 0x0006'0002U,
    atmosphere_northward_wind_m_s = 0x0006'0003U,
    atmosphere_vertical_mass_flux_kg_m2_s = 0x0006'0004U,
    sea_level_pressure_Pa = 0x0006'0005U,
    surface_wind_stress_east_N_m2 = 0x0006'0006U,
    surface_wind_stress_north_N_m2 = 0x0006'0007U,
    atmosphere_balanced_surface_pressure_Pa = 0x0006'0008U,
    climatology_surface_eastward_wind_mean_m_s = 0x0006'0009U,
    climatology_surface_northward_wind_mean_m_s = 0x0006'000AU,
    climatology_sea_level_pressure_mean_Pa = 0x0006'000BU,
};

enum class FieldDataType : std::uint8_t {
    float32,
    float64,
};

enum class FieldPartition : std::uint8_t {
    slow,
    fast,
    climatology,
    derived,
};

enum class FieldLayout : std::uint8_t {
    cell,
    cell_layers,
    edge,
    global,
    edge_layers,   // layer-major [layer][edge] (ADR-0011 §4.1)
};

inline constexpr std::uint32_t hypsometry_layer_count = 9U;

// A cell_layers or edge_layers field whose layer count is a scenario parameter rather than a
// property of the build (ADR-0010 §4.1): the registry declares 0 layers, each
// snapshot's manifest states the stored count, and every scenario-layered
// field of one snapshot has the same count.
inline constexpr std::uint32_t scenario_layer_count = 0U;
inline constexpr std::uint32_t max_scenario_layer_count = 64U;

struct FieldDescriptor {
    FieldId id;
    std::string_view name;
    FieldPartition partition;
    FieldLayout layout;
    FieldDataType data_type;
    std::uint32_t layers;
    std::string_view units;

    [[nodiscard]] constexpr bool persistent() const noexcept {
        return partition == FieldPartition::slow;
    }
    [[nodiscard]] constexpr bool scenario_layered() const noexcept {
        return (layout == FieldLayout::cell_layers || layout == FieldLayout::edge_layers) &&
               layers == scenario_layer_count;
    }
};

[[nodiscard]] constexpr std::string_view field_partition_name(
    FieldPartition partition) noexcept {
    switch (partition) {
    case FieldPartition::slow:
        return "slow";
    case FieldPartition::fast:
        return "fast";
    case FieldPartition::climatology:
        return "climatology";
    case FieldPartition::derived:
        return "derived";
    }
    return {};
}

[[nodiscard]] constexpr std::string_view field_layout_name(FieldLayout layout) noexcept {
    switch (layout) {
    case FieldLayout::cell:
        return "cell";
    case FieldLayout::cell_layers:
        return "cell_layers";
    case FieldLayout::edge:
        return "edge";
    case FieldLayout::global:
        return "global";
    case FieldLayout::edge_layers:
        return "edge_layers";
    }
    return {};
}

[[nodiscard]] constexpr std::string_view field_data_type_name(
    FieldDataType data_type) noexcept {
    switch (data_type) {
    case FieldDataType::float32:
        return "float32";
    case FieldDataType::float64:
        return "float64";
    }
    return {};
}

inline constexpr std::array<FieldDescriptor, 29> field_registry{{
    {FieldId::top_of_atmosphere_insolation_W_m2, "top_of_atmosphere_insolation_W_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "W/m2"},
    {FieldId::substep_mean_insolation_W_m2, "substep_mean_insolation_W_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "W/m2"},
    {FieldId::hypsometry_m, "hypsometry_m", FieldPartition::slow,
     FieldLayout::cell_layers, FieldDataType::float32, hypsometry_layer_count, "m"},
    {FieldId::sea_level_m, "sea_level_m", FieldPartition::slow,
     FieldLayout::global, FieldDataType::float64, 1U, "m"},
    // Surface energy columns (ADR-0007 §4.1): both tiles of every cell.
    {FieldId::land_surface_temperature_K, "land_surface_temperature_K", FieldPartition::slow,
     FieldLayout::cell, FieldDataType::float32, 1U, "K"},
    {FieldId::land_ground_temperature_K, "land_ground_temperature_K", FieldPartition::slow,
     FieldLayout::cell, FieldDataType::float32, 1U, "K"},
    {FieldId::ocean_deep_temperature_K, "ocean_deep_temperature_K", FieldPartition::slow,
     FieldLayout::cell, FieldDataType::float64, 1U, "K"},
    // float64 since schema 3 (ADR-0007 §10): a float32 mixed layer cannot hold
    // a ten-minute reference step's change without biased rounding.
    {FieldId::ocean_mixed_layer_temperature_K, "ocean_mixed_layer_temperature_K",
     FieldPartition::slow, FieldLayout::cell, FieldDataType::float64, 1U, "K"},
    // Cryosphere (ADR-0008 §4.1): water reservoirs in float64, and the
    // prescribed precipitation that stands in for moisture until M6.
    {FieldId::land_snow_water_equivalent_kg_m2, "land_snow_water_equivalent_kg_m2",
     FieldPartition::slow, FieldLayout::cell, FieldDataType::float64, 1U, "kg/m2"},
    {FieldId::sea_ice_mass_kg_m2, "sea_ice_mass_kg_m2", FieldPartition::slow, FieldLayout::cell,
     FieldDataType::float64, 1U, "kg/m2"},
    {FieldId::prescribed_precipitation_kg_m2_s, "prescribed_precipitation_kg_m2_s",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "kg/m2/s"},
    // The cell's radiating surface temperature, area-weighted over its tiles
    // (the sea-ice surface over ice), written by every surface step.
    {FieldId::surface_temperature_K, "surface_temperature_K", FieldPartition::derived,
     FieldLayout::cell, FieldDataType::float32, 1U, "K"},
    // Monthly climatology (ADR-0001 §4.1, ADR-0006 §7): one layer per
    // sub-step k mod 12, accumulated over the climate steps of a run.
    {FieldId::climatology_surface_temperature_mean_K, "climatology_surface_temperature_mean_K",
     FieldPartition::climatology, FieldLayout::cell_layers, FieldDataType::float32, 12U, "K"},
    {FieldId::climatology_surface_temperature_variance_K2,
     "climatology_surface_temperature_variance_K2", FieldPartition::climatology,
     FieldLayout::cell_layers, FieldDataType::float32, 12U, "K2"},
    {FieldId::climatology_land_snow_mean_kg_m2, "climatology_land_snow_mean_kg_m2",
     FieldPartition::climatology, FieldLayout::cell_layers, FieldDataType::float32, 12U,
     "kg/m2"},
    {FieldId::climatology_sea_ice_mean_kg_m2, "climatology_sea_ice_mean_kg_m2",
     FieldPartition::climatology, FieldLayout::cell_layers, FieldDataType::float32, 12U,
     "kg/m2"},
    // Atmosphere (ADR-0010 §4.1): the column's surface pressure, and the
    // temperature of each of its N equal-mass sigma layers from the bottom,
    // N a scenario parameter (0: no atmosphere). float64 reservoirs.
    {FieldId::atmosphere_surface_pressure_Pa, "atmosphere_surface_pressure_Pa",
     FieldPartition::slow, FieldLayout::cell, FieldDataType::float64, 1U, "Pa"},
    {FieldId::atmosphere_temperature_K, "atmosphere_temperature_K", FieldPartition::slow,
     FieldLayout::cell_layers, FieldDataType::float64, scenario_layer_count, "K"},
    // Winds (ADR-0011 §4.1): the reference-mode velocity normal to each edge,
    // per atmospheric layer, along the edge's normal (first cell to second).
    // Fast state: never persisted, never needed to rebuild the slow state.
    {FieldId::atmosphere_edge_normal_wind_m_s, "atmosphere_edge_normal_wind_m_s",
     FieldPartition::fast, FieldLayout::edge_layers, FieldDataType::float64,
     scenario_layer_count, "m/s"},
    // The climate mode's circulation on the cells (ADR-0011 §4.1, §4.4 step
    // 5): derived each climate sub-step, layers from the bottom. The
    // vertical mass flux is upward through the top of each layer (the
    // column's top: the balance's residual divergence). The stress is the
    // air's on the surface, along the bottom-layer wind.
    {FieldId::atmosphere_eastward_wind_m_s, "atmosphere_eastward_wind_m_s",
     FieldPartition::derived, FieldLayout::cell_layers, FieldDataType::float32,
     scenario_layer_count, "m/s"},
    {FieldId::atmosphere_northward_wind_m_s, "atmosphere_northward_wind_m_s",
     FieldPartition::derived, FieldLayout::cell_layers, FieldDataType::float32,
     scenario_layer_count, "m/s"},
    {FieldId::atmosphere_vertical_mass_flux_kg_m2_s, "atmosphere_vertical_mass_flux_kg_m2_s",
     FieldPartition::derived, FieldLayout::cell_layers, FieldDataType::float32,
     scenario_layer_count, "kg/m2/s"},
    {FieldId::sea_level_pressure_Pa, "sea_level_pressure_Pa", FieldPartition::derived,
     FieldLayout::cell, FieldDataType::float32, 1U, "Pa"},
    {FieldId::surface_wind_stress_east_N_m2, "surface_wind_stress_east_N_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "N/m2"},
    {FieldId::surface_wind_stress_north_N_m2, "surface_wind_stress_north_N_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "N/m2"},
    // The balanced surface pressure (ADR-0011 §4.6) on the cells. Derived
    // until M6-05 writes it into the slow state's p_s every climate step.
    {FieldId::atmosphere_balanced_surface_pressure_Pa, "atmosphere_balanced_surface_pressure_Pa",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float64, 1U, "Pa"},
    // The circulation's monthly climatology (ADR-0011 §4.1, §4.4 step 5):
    // the bottom-layer wind and the sea-level pressure, one layer per
    // sub-step k mod 12, over the months the circulation solved.
    {FieldId::climatology_surface_eastward_wind_mean_m_s,
     "climatology_surface_eastward_wind_mean_m_s", FieldPartition::climatology,
     FieldLayout::cell_layers, FieldDataType::float32, 12U, "m/s"},
    {FieldId::climatology_surface_northward_wind_mean_m_s,
     "climatology_surface_northward_wind_mean_m_s", FieldPartition::climatology,
     FieldLayout::cell_layers, FieldDataType::float32, 12U, "m/s"},
    {FieldId::climatology_sea_level_pressure_mean_Pa, "climatology_sea_level_pressure_mean_Pa",
     FieldPartition::climatology, FieldLayout::cell_layers, FieldDataType::float32, 12U, "Pa"},
}};

// Retired IDs are never registered again. Snapshots of the schemas that
// stored them still load (sim/core/serialization/snapshot_file.cpp).
inline constexpr FieldId retired_ocean_mixed_layer_temperature_float32_K =
    static_cast<FieldId>(0x0003'0003U);  // schema 2 only

inline constexpr std::array<FieldId, 1> retired_field_ids{{
    retired_ocean_mixed_layer_temperature_float32_K,
}};

consteval bool field_registry_ids_are_unique() {
    for (std::size_t first = 0; first < field_registry.size(); ++first) {
        for (std::size_t second = first + 1U; second < field_registry.size(); ++second) {
            if (field_registry[first].id == field_registry[second].id) {
                return false;
            }
        }
    }
    return true;
}

consteval bool field_registry_ids_are_sorted() {
    for (std::size_t index = 1U; index < field_registry.size(); ++index) {
        if (static_cast<std::uint32_t>(field_registry[index - 1U].id) >=
            static_cast<std::uint32_t>(field_registry[index].id)) {
            return false;
        }
    }
    return true;
}

consteval bool field_registry_persistence_matches_partition() {
    for (const auto& descriptor : field_registry) {
        if (descriptor.persistent() != (descriptor.partition == FieldPartition::slow)) {
            return false;
        }
    }
    return true;
}

static_assert(field_registry_ids_are_unique(), "field registry IDs must be unique");
static_assert(field_registry_ids_are_sorted(), "field registry IDs must be sorted");
consteval bool registered_field_ids_are_not_retired() {
    for (const auto retired_id : retired_field_ids) {
        for (const auto& descriptor : field_registry) {
            if (retired_id == descriptor.id) {
                return false;
            }
        }
    }
    return true;
}

consteval bool field_registry_layer_counts_are_valid() {
    for (const auto& descriptor : field_registry) {
        if (descriptor.layers == scenario_layer_count && !descriptor.scenario_layered()) {
            return false;
        }
    }
    return true;
}

static_assert(field_registry_layer_counts_are_valid(),
              "only cell_layers and edge_layers fields may have a scenario layer count");
static_assert(field_registry_persistence_matches_partition(),
              "only slow-state fields may be persistent");
static_assert(registered_field_ids_are_not_retired(),
              "a registered field ID cannot also be retired");

// Compile-time registration checks must use this rather than comparing
// find_field() with nullptr: GCC does not treat an object's address as
// non-null in constant evaluation under -fsanitize=null, nonnull-attribute,
// returns-nonnull-attribute or -fno-delete-null-pointer-checks.
[[nodiscard]] constexpr bool is_field_registered(FieldId id) noexcept {
    for (const auto& descriptor : field_registry) {
        if (descriptor.id == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr const FieldDescriptor* find_field(FieldId id) noexcept {
    for (const auto& descriptor : field_registry) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

}  // namespace planetsim
