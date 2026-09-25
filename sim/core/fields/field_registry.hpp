#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace planetsim {

enum class FieldId : std::uint32_t {
    top_of_atmosphere_insolation_W_m2 = 0x0001'0001U,
};

enum class FieldKind : std::uint8_t {
    diagnostic,
    prognostic,
    reservoir,
};

enum class FieldDataType : std::uint8_t {
    float32,
    float64,
};

struct FieldDescriptor {
    FieldId id;
    std::string_view name;
    FieldKind kind;
    FieldDataType data_type;
    std::string_view units;
    bool persistent;
};

inline constexpr std::array<FieldDescriptor, 1> field_registry{{
    {FieldId::top_of_atmosphere_insolation_W_m2, "top_of_atmosphere_insolation_W_m2",
     FieldKind::diagnostic, FieldDataType::float32, "W/m2", false},
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

static_assert(field_registry_ids_are_unique(), "field registry IDs must be unique");

[[nodiscard]] constexpr const FieldDescriptor* find_field(FieldId id) noexcept {
    for (const auto& descriptor : field_registry) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

}  // namespace planetsim
