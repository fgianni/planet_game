#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

// ADR-0011 V2 (task M6-01, amendment §11): accuracy of the C-grid operators on
// u = grad(g) + r x grad(h), L3–L6.
int main() {
    planetsim::test::Context test;

    std::vector<planetsim::CGridValidation> levels;
    for (std::uint32_t level = 3; level <= 6U; ++level) {
        levels.push_back(
            planetsim::validate_c_grid_operators(planetsim::make_icosphere(level, 6'371'000.0)));
    }
    const auto order = [](double coarse_error, double fine_error) {
        return std::log2(coarse_error / fine_error);
    };

    for (std::size_t index = 1; index < levels.size(); ++index) {
        const auto& coarse = levels[index - 1U];
        const auto& fine = levels[index];

        // Consistent operators: second order in L2, converging in max.
        for (const auto member : {&planetsim::CGridValidation::reconstruction,
                                  &planetsim::CGridValidation::normal_gradient,
                                  &planetsim::CGridValidation::tangential_gradient}) {
            PLANETSIM_EXPECT(test, order((coarse.*member).relative_l2,
                                         (fine.*member).relative_l2) >= 1.5);
            PLANETSIM_EXPECT(test, order((coarse.*member).relative_max,
                                         (fine.*member).relative_max) >= 0.9);
        }

        // TRiSK's tangential velocity and kinetic energy, and the vorticity:
        // at least first order in L2 apart from the measured 0.93 of the
        // vorticity (amendment §11).
        PLANETSIM_EXPECT(test, order(coarse.tangential_velocity.relative_l2,
                                     fine.tangential_velocity.relative_l2) >= 0.9);
        PLANETSIM_EXPECT(test, order(coarse.kinetic_energy.relative_l2,
                                     fine.kinetic_energy.relative_l2) >= 0.9);
        PLANETSIM_EXPECT(test,
                         order(coarse.vorticity.relative_l2, fine.vorticity.relative_l2) >= 0.9);
    }

    for (std::size_t index = 1; index < levels.size(); ++index) {
        const auto& level = levels[index];
        // From L4 on, their maxima, in the first ring of hexagons around the
        // pentagons, stay bounded without converging (Peixoto, 2016); guard
        // the bounds. (At L3 the tangential maximum is still 1.8e-2 of
        // ordinary truncation error.)
        PLANETSIM_EXPECT(test, level.tangential_velocity.relative_max <= 1.0e-2);
        PLANETSIM_EXPECT(test, level.kinetic_energy.relative_max <= 3.0e-2);
        PLANETSIM_EXPECT(test, level.vorticity.relative_max <= 8.0e-2);
        PLANETSIM_EXPECT(test, level.tangential_velocity.pentagon_relative_max <= 1.0e-2);
        PLANETSIM_EXPECT(test, level.vorticity.pentagon_relative_max <= 8.0e-2);
    }

    // At L6 the errors away from the pentagons are small, and the
    // icosahedron's seams carry no larger error than the interior.
    const auto& finest = levels.back();
    PLANETSIM_EXPECT(test, finest.tangential_velocity.relative_l2 <= 5.0e-4);
    PLANETSIM_EXPECT(test, finest.kinetic_energy.relative_l2 <= 8.0e-4);
    PLANETSIM_EXPECT(test, finest.vorticity.relative_l2 <= 6.0e-3);
    for (const auto member : {&planetsim::CGridValidation::tangential_velocity,
                              &planetsim::CGridValidation::vorticity,
                              &planetsim::CGridValidation::kinetic_energy,
                              &planetsim::CGridValidation::reconstruction}) {
        PLANETSIM_EXPECT(test, (finest.*member).interior_relative_max > 0.0);
        PLANETSIM_EXPECT(test, (finest.*member).seam_relative_max <=
                                   2.0 * (finest.*member).interior_relative_max);
    }

    return test.result();
}
