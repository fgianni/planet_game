#include "sim/core/fields/field.hpp"
#include "sim/core/fields/field_registry.hpp"
#include "sim/planet/mesh/cell_id.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "tests/test_support.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

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
    const auto* descriptor = planetsim::find_field(insolation_id);
    PLANETSIM_EXPECT(test, descriptor != nullptr);
    if (descriptor != nullptr) {
        PLANETSIM_EXPECT(test, descriptor->data_type == planetsim::FieldDataType::float32);
        PLANETSIM_EXPECT(test, descriptor->kind == planetsim::FieldKind::diagnostic);
        PLANETSIM_EXPECT(test, !descriptor->persistent);
    }

    const planetsim::Field<double> empty;
    PLANETSIM_EXPECT(test, empty.empty());
    PLANETSIM_EXPECT(test, empty.size() == 0);

    return test.result();
}
