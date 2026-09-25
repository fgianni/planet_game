#include "sim/core/math/vec3d.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

int main() {
    planetsim::test::Context test;
    constexpr double tolerance = 2.0e-15;

    for (const planetsim::Vec3d normal :
         {planetsim::Vec3d{1.0, 0.0, 0.0}, planetsim::Vec3d{0.0, 1.0, 0.0},
          planetsim::Vec3d{0.0, 0.0, 1.0}, planetsim::Vec3d{0.0, 0.0, -1.0},
          planetsim::Vec3d{1.0, 2.0, 3.0}}) {
        const auto basis = planetsim::make_local_tangent_basis(normal);
        PLANETSIM_EXPECT_NEAR(test, planetsim::length(basis.east_unit), 1.0, tolerance);
        PLANETSIM_EXPECT_NEAR(test, planetsim::length(basis.north_unit), 1.0, tolerance);
        PLANETSIM_EXPECT_NEAR(test, planetsim::length(basis.up_unit), 1.0, tolerance);
        PLANETSIM_EXPECT_NEAR(test, planetsim::dot(basis.east_unit, basis.north_unit), 0.0,
                              tolerance);
        PLANETSIM_EXPECT_NEAR(test, planetsim::dot(basis.east_unit, basis.up_unit), 0.0, tolerance);
        PLANETSIM_EXPECT_NEAR(test, planetsim::dot(basis.north_unit, basis.up_unit), 0.0,
                              tolerance);
        const auto handedness = planetsim::cross(basis.east_unit, basis.north_unit);
        PLANETSIM_EXPECT_NEAR(test, planetsim::dot(handedness, basis.up_unit), 1.0, tolerance);
    }

    PLANETSIM_EXPECT_NEAR(test, planetsim::latitude_rad({1.0, 0.0, 0.0}), 0.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, planetsim::longitude_rad({0.0, 1.0, 0.0}),
                          0.5 * std::numbers::pi_v<double>, tolerance);
    PLANETSIM_EXPECT_NEAR(test, planetsim::latitude_rad({0.0, 0.0, 1.0}),
                          0.5 * std::numbers::pi_v<double>, tolerance);

    const auto mesh = planetsim::make_icosphere(4, 6'371'000.0);
    for (const auto& cell : mesh.cells()) {
        PLANETSIM_EXPECT(test, planetsim::is_finite(cell.east_unit));
        PLANETSIM_EXPECT(test, planetsim::is_finite(cell.north_unit));
        PLANETSIM_EXPECT(test, planetsim::is_finite(cell.center_unit));
        PLANETSIM_EXPECT_NEAR(test, planetsim::dot(cell.east_unit, cell.north_unit), 0.0,
                              tolerance);
        PLANETSIM_EXPECT_NEAR(
            test,
            planetsim::dot(planetsim::cross(cell.east_unit, cell.north_unit), cell.center_unit),
            1.0, tolerance);
    }

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::make_local_tangent_basis({0.0, 0.0, 0.0}));

    return test.result();
}
