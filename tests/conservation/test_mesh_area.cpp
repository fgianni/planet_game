#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"
#include "tests/test_support.hpp"

#include <cstdint>

int main() {
    planetsim::test::Context test;
    constexpr double earth_radius_m = 6'371'000.0;
    constexpr double relative_area_tolerance = 5.0e-14;

    double worst_relative_error = 0.0;
    for (std::uint32_t subdivision = 0; subdivision <= 6; ++subdivision) {
        const auto mesh = planetsim::make_icosphere(subdivision, earth_radius_m);
        const auto diagnostics = planetsim::analyze_mesh(mesh);
        PLANETSIM_EXPECT(test, diagnostics.total_area_m2 > 0.0);
        PLANETSIM_EXPECT(test, diagnostics.min_cell_area_m2 > 0.0);
        PLANETSIM_EXPECT(test,
                         diagnostics.max_cell_area_m2 >= diagnostics.min_cell_area_m2);
        PLANETSIM_EXPECT(test,
                         diagnostics.relative_area_error <= relative_area_tolerance);
        if (diagnostics.relative_area_error > worst_relative_error) {
            worst_relative_error = diagnostics.relative_area_error;
        }
    }

    const auto unit_mesh = planetsim::make_icosphere(3, 1.0);
    const auto earth_mesh = planetsim::make_icosphere(3, earth_radius_m);
    const auto unit_diagnostics = planetsim::analyze_mesh(unit_mesh);
    const auto earth_diagnostics = planetsim::analyze_mesh(earth_mesh);
    PLANETSIM_EXPECT_NEAR(
        test,
        earth_diagnostics.total_area_m2 / unit_diagnostics.total_area_m2,
        earth_radius_m * earth_radius_m,
        earth_radius_m * earth_radius_m * 5.0e-15);
    PLANETSIM_EXPECT(test, worst_relative_error <= relative_area_tolerance);

    return test.result();
}
