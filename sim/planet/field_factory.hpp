#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/fields/field_registry.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <type_traits>

namespace planetsim {
namespace detail {

template <FieldDataType DataType> struct FieldValueType;

template <> struct FieldValueType<FieldDataType::float32> { using type = float; };
template <> struct FieldValueType<FieldDataType::float64> { using type = double; };

template <FieldLayout Layout, typename Value> struct FieldContainer;

template <typename Value> struct FieldContainer<FieldLayout::cell, Value> {
    using type = Field2D<Value>;
};
template <typename Value> struct FieldContainer<FieldLayout::cell_layers, Value> {
    using type = Field3D<Value>;
};
template <typename Value> struct FieldContainer<FieldLayout::edge, Value> {
    using type = EdgeField<Value>;
};
template <typename Value> struct FieldContainer<FieldLayout::edge_layers, Value> {
    using type = Field3D<Value>;   // layer × edge, indexed by EdgeId
};
template <typename Value> struct FieldContainer<FieldLayout::global, Value> {
    using type = Value;
};

template <FieldId Id> consteval FieldDescriptor registered_field_descriptor() {
    static_assert(is_field_registered(Id), "field_container_t requires a registered FieldId");
    return *find_field(Id);
}

}  // namespace detail

template <FieldId Id>
inline constexpr FieldDescriptor field_descriptor_v = detail::registered_field_descriptor<Id>();

template <FieldId Id>
using field_value_t = typename detail::FieldValueType<field_descriptor_v<Id>.data_type>::type;

template <FieldId Id>
using field_container_t =
    typename detail::FieldContainer<field_descriptor_v<Id>.layout, field_value_t<Id>>::type;

template <FieldId Id> [[nodiscard]] field_container_t<Id> make_field(const PlanetMesh& mesh) {
    constexpr auto descriptor = field_descriptor_v<Id>;
    using Value = field_value_t<Id>;
    using Container = field_container_t<Id>;

    if constexpr (descriptor.layout == FieldLayout::cell) {
        static_assert(descriptor.layers == 1U, "cell fields must have exactly one layer");
        return Container(mesh.cell_count(), Value{});
    } else if constexpr (descriptor.layout == FieldLayout::cell_layers) {
        // A scenario-layered field starts with no layers; its initialiser
        // sizes it (ADR-0010 §4.1).
        return Container(descriptor.layers, mesh.cell_count(), Value{});
    } else if constexpr (descriptor.layout == FieldLayout::edge) {
        static_assert(descriptor.layers == 1U, "edge fields must have exactly one layer");
        return Container(mesh.edge_count(), Value{});
    } else if constexpr (descriptor.layout == FieldLayout::edge_layers) {
        // Scenario-layered: sized by its initialiser, like cell_layers.
        return Container(descriptor.layers, mesh.edge_count(), Value{});
    } else {
        static_assert(descriptor.layout == FieldLayout::global);
        static_assert(descriptor.layers == 1U, "global fields must have exactly one layer");
        static_assert(std::is_arithmetic_v<Container>);
        return Container{};
    }
}

}  // namespace planetsim
