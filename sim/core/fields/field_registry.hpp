#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace planetsim {

enum class FieldId : std::uint32_t {
    top_of_atmosphere_insolation_W_m2 = 0x0001'0001U,
    hypsometry_m = 0x0002'0001U,
    sea_level_m = 0x0002'0002U,
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
};

inline constexpr std::uint32_t hypsometry_layer_count = 9U;

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
};

inline constexpr std::array<FieldDescriptor, 3> field_registry{{
    {FieldId::top_of_atmosphere_insolation_W_m2, "top_of_atmosphere_insolation_W_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "W/m2"},
    {FieldId::hypsometry_m, "hypsometry_m", FieldPartition::slow,
     FieldLayout::cell_layers, FieldDataType::float32, hypsometry_layer_count, "m"},
    {FieldId::sea_level_m, "sea_level_m", FieldPartition::slow,
     FieldLayout::global, FieldDataType::float64, 1U, "m"},
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
static_assert(field_registry_persistence_matches_partition(),
              "only slow-state fields may be persistent");

[[nodiscard]] constexpr const FieldDescriptor* find_field(FieldId id) noexcept {
    for (const auto& descriptor : field_registry) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

}  // namespace planetsim
