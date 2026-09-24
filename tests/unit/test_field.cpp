#include "sim/core/fields/field.hpp"
#include "tests/test_support.hpp"

#include <stdexcept>
#include <vector>

int main() {
    planetsim::test::Context test;

    planetsim::Field<double> temperatures(3, 273.15);
    PLANETSIM_EXPECT(test, temperatures.size() == 3);
    PLANETSIM_EXPECT(test, !temperatures.empty());
    PLANETSIM_EXPECT_NEAR(test, temperatures[planetsim::CellId{1}], 273.15, 0.0);

    temperatures[planetsim::CellId{1}] = 280.0;
    PLANETSIM_EXPECT_NEAR(test, temperatures[1], 280.0, 0.0);
    PLANETSIM_EXPECT(test, temperatures.values().size() == temperatures.size());

    const planetsim::Field<int> identifiers(std::vector<int>{4, 5, 6});
    PLANETSIM_EXPECT(test, identifiers[0] == 4);
    PLANETSIM_EXPECT(test, identifiers[planetsim::CellId{2}] == 6);
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, identifiers[3]);
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range,
                            identifiers[planetsim::CellId::invalid()]);

    const planetsim::Field<double> empty;
    PLANETSIM_EXPECT(test, empty.empty());
    PLANETSIM_EXPECT(test, empty.size() == 0);

    return test.result();
}
