#include "sim/core/fields/field.hpp"
#include "sim/core/fields/field_registry.hpp"
#include "sim/planet/field_factory.hpp"
#include "sim/planet/mesh/cell_id.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

static_assert(std::is_same_v<
              planetsim::field_container_t<
                  planetsim::FieldId::top_of_atmosphere_insolation_W_m2>,
              planetsim::Field2D<float>>);
static_assert(std::is_same_v<
              planetsim::field_container_t<planetsim::FieldId::hypsometry_m>,
              planetsim::Field3D<float>>);
static_assert(std::is_same_v<
              planetsim::field_container_t<planetsim::FieldId::sea_level_m>, double>);

int main() {
    planetsim::test::Context test;

    planetsim::Field2D<float> temperatures(3, 273.15F);
    PLANETSIM_EXPECT(test, temperatures.size() == 3);
    PLANETSIM_EXPECT(test, !temperatures.empty());
    PLANETSIM_EXPECT_NEAR(test, temperatures[planetsim::CellId{1}], 273.15, 1.0e-5);
    PLANETSIM_EXPECT(test, reinterpret_cast<std::uintptr_t>(temperatures.values().data()) %
                                   planetsim::field_alignment_bytes ==
                               0U);

    temperatures[planetsim::CellId{1}] = 280.0F;
    PLANETSIM_EXPECT_NEAR(test, temperatures[1], 280.0, 0.0);
    PLANETSIM_EXPECT(test, temperatures.values().size() == temperatures.size());

    const planetsim::Field<int> identifiers(std::vector<int>{4, 5, 6});
    PLANETSIM_EXPECT(test, identifiers[0] == 4);
    PLANETSIM_EXPECT(test, identifiers[planetsim::CellId{2}] == 6);
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, identifiers[3]);
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, identifiers[planetsim::CellId::invalid()]);

    planetsim::Field3D<float> atmosphere(2, 3, 0.0F);
    atmosphere.at(0, planetsim::CellId{2}) = 12.0F;
    atmosphere.at(1, planetsim::CellId{0}) = 21.0F;
    PLANETSIM_EXPECT(test, atmosphere.layer_count() == 2);
    PLANETSIM_EXPECT(test, atmosphere.cell_count() == 3);
    PLANETSIM_EXPECT_NEAR(test, atmosphere.layer(0)[2], 12.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, atmosphere.layer(1)[0], 21.0, 0.0);
    PLANETSIM_EXPECT(test, atmosphere.layer(1).data() - atmosphere.layer(0).data() == 3);
    PLANETSIM_EXPECT(test, reinterpret_cast<std::uintptr_t>(atmosphere.layer(0).data()) %
                                   planetsim::field_alignment_bytes ==
                               0U);
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, atmosphere.layer(2));
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, atmosphere.at(0, planetsim::CellId{3}));

    planetsim::EdgeField<float> edge_fluxes(2, 0.0F);
    edge_fluxes[planetsim::EdgeId{1}] = 4.5F;
    PLANETSIM_EXPECT_NEAR(test, edge_fluxes[planetsim::EdgeId{1}], 4.5, 0.0);
    PLANETSIM_EXPECT(test, reinterpret_cast<std::uintptr_t>(edge_fluxes.values().data()) %
                                   planetsim::field_alignment_bytes ==
                               0U);

    constexpr auto insolation_id = planetsim::FieldId::top_of_atmosphere_insolation_W_m2;
    static_assert(planetsim::is_field_registered(insolation_id));
    static_assert(planetsim::is_field_registered(planetsim::FieldId::hypsometry_m));
    static_assert(planetsim::is_field_registered(planetsim::FieldId::sea_level_m));
    static_assert(!planetsim::is_field_registered(static_cast<planetsim::FieldId>(0xFFFF'FFFFU)));
    const auto* descriptor = planetsim::find_field(insolation_id);
    PLANETSIM_EXPECT(test, descriptor != nullptr);
    if (descriptor != nullptr) {
        PLANETSIM_EXPECT(test, descriptor->data_type == planetsim::FieldDataType::float32);
        PLANETSIM_EXPECT(test, descriptor->partition == planetsim::FieldPartition::derived);
        PLANETSIM_EXPECT(test, descriptor->layout == planetsim::FieldLayout::cell);
        PLANETSIM_EXPECT(test, descriptor->layers == 1U);
        PLANETSIM_EXPECT(test, !descriptor->persistent());
    }

    constexpr auto hypsometry_id = planetsim::FieldId::hypsometry_m;
    const auto* hypsometry = planetsim::find_field(hypsometry_id);
    PLANETSIM_EXPECT(test, hypsometry != nullptr);
    if (hypsometry != nullptr) {
        PLANETSIM_EXPECT(test, hypsometry->partition == planetsim::FieldPartition::slow);
        PLANETSIM_EXPECT(test, hypsometry->layout == planetsim::FieldLayout::cell_layers);
        PLANETSIM_EXPECT(test, hypsometry->data_type == planetsim::FieldDataType::float32);
        PLANETSIM_EXPECT(test, hypsometry->layers == 9U);
        PLANETSIM_EXPECT(test, hypsometry->persistent());
    }

    constexpr auto sea_level_id = planetsim::FieldId::sea_level_m;
    const auto* sea_level = planetsim::find_field(sea_level_id);
    PLANETSIM_EXPECT(test, sea_level != nullptr);
    if (sea_level != nullptr) {
        PLANETSIM_EXPECT(test, sea_level->partition == planetsim::FieldPartition::slow);
        PLANETSIM_EXPECT(test, sea_level->layout == planetsim::FieldLayout::global);
        PLANETSIM_EXPECT(test, sea_level->data_type == planetsim::FieldDataType::float64);
        PLANETSIM_EXPECT(test, sea_level->layers == 1U);
        PLANETSIM_EXPECT(test, sea_level->persistent());
    }

    const auto mesh = planetsim::make_icosphere(0U, 1.0);
    const auto insolation = planetsim::make_field<
        planetsim::FieldId::top_of_atmosphere_insolation_W_m2>(mesh);
    PLANETSIM_EXPECT(test, insolation.size() == mesh.cell_count());
    PLANETSIM_EXPECT(test, std::all_of(insolation.values().begin(), insolation.values().end(),
                                      [](float value) { return value == 0.0F; }));

    const auto hypsometry_field =
        planetsim::make_field<planetsim::FieldId::hypsometry_m>(mesh);
    PLANETSIM_EXPECT(test,
                     hypsometry_field.layer_count() == planetsim::hypsometry_layer_count);
    PLANETSIM_EXPECT(test, hypsometry_field.cell_count() == mesh.cell_count());
    for (std::size_t layer = 0; layer < hypsometry_field.layer_count(); ++layer) {
        const auto values = hypsometry_field.layer(layer);
        PLANETSIM_EXPECT(test, std::all_of(values.begin(), values.end(),
                                          [](float value) { return value == 0.0F; }));
    }

    const auto sea_level_field =
        planetsim::make_field<planetsim::FieldId::sea_level_m>(mesh);
    PLANETSIM_EXPECT_NEAR(test, sea_level_field, 0.0, 0.0);

    const planetsim::Field<double> empty;
    PLANETSIM_EXPECT(test, empty.empty());
    PLANETSIM_EXPECT(test, empty.size() == 0);

    return test.result();
}
