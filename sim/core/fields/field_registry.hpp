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

inline constexpr std::array<FieldDescriptor, 3> field_registry{{
    {FieldId::top_of_atmosphere_insolation_W_m2, "top_of_atmosphere_insolation_W_m2",
     FieldPartition::derived, FieldLayout::cell, FieldDataType::float32, 1U, "W/m2"},
    {FieldId::hypsometry_m, "hypsometry_m", FieldPartition::slow,
     FieldLayout::cell_layers, FieldDataType::float32, hypsometry_layer_count, "m"},
    {FieldId::sea_level_m, "sea_level_m", FieldPartition::slow,
     FieldLayout::global, FieldDataType::float64, 1U, "m"},
}};

inline constexpr std::array<FieldId, 0> retired_field_ids{};

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
