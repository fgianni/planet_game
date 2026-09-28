#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

// ADR-0002 V3: operator accuracy on analytic spherical-harmonic fields.
int main() {
    planetsim::test::Context test;

    std::vector<planetsim::OperatorValidation> levels;
    for (std::uint32_t level = 3; level <= 6U; ++level) {
        levels.push_back(
            planetsim::validate_operators(planetsim::make_icosphere(level, 6'371'000.0)));
    }

    for (std::size_t index = 1; index < levels.size(); ++index) {
        const auto& coarse = levels[index - 1U];
        const auto& fine = levels[index];
        const auto order = [](double coarse_error, double fine_error) {
            return std::log2(coarse_error / fine_error);
        };

        // Gate: at least 1.5 order in the area-weighted L2 norm.
        PLANETSIM_EXPECT(test, order(coarse.gradient.relative_l2, fine.gradient.relative_l2) >= 1.5);
        PLANETSIM_EXPECT(test,
                         order(coarse.divergence.relative_l2, fine.divergence.relative_l2) >= 1.5);
        PLANETSIM_EXPECT(test, order(coarse.poisson_solution.relative_l2,
                                     fine.poisson_solution.relative_l2) >= 1.5);

        // Maximum norms must still converge; Poisson solutions do so at 1.5+.
        PLANETSIM_EXPECT(test,
                         order(coarse.gradient.relative_max, fine.gradient.relative_max) >= 0.9);
        PLANETSIM_EXPECT(test,
                         order(coarse.divergence.relative_max, fine.divergence.relative_max) >= 0.9);
        PLANETSIM_EXPECT(test, order(coarse.poisson_solution.relative_max,
                                     fine.poisson_solution.relative_max) >= 1.5);

        // Pentagons are excluded from the norms but must converge too.
        PLANETSIM_EXPECT(test, fine.gradient.pentagon_relative_max <
                                   coarse.gradient.pentagon_relative_max);
        PLANETSIM_EXPECT(test, fine.divergence.pentagon_relative_max <
                                   coarse.divergence.pentagon_relative_max);
        PLANETSIM_EXPECT(test, fine.poisson_solution.pentagon_relative_max <
                                   coarse.poisson_solution.pentagon_relative_max);
    }

    for (const auto& level : levels) {
        // The two-point Laplacian's pointwise truncation error stays bounded
        // near pentagons without converging (ADR-0002 §9); guard the bound.
        PLANETSIM_EXPECT(test, level.laplacian.relative_max <= 2.5e-2);
    }

    // Error-map pattern check at L5 and L6, where both regions are populated:
    // no visible seam along the icosahedron's edges in the gated measures.
    for (std::size_t index = 2; index < levels.size(); ++index) {
        const auto& level = levels[index];
        PLANETSIM_EXPECT(test, level.divergence.interior_relative_max > 0.0);
        PLANETSIM_EXPECT(test, level.divergence.seam_relative_max <=
                                   1.25 * level.divergence.interior_relative_max);
        PLANETSIM_EXPECT(test, level.poisson_solution.seam_relative_max <=
                                   1.25 * level.poisson_solution.interior_relative_max);
        PLANETSIM_EXPECT(test, level.gradient.seam_relative_max <=
                                   2.0 * level.gradient.interior_relative_max);
        PLANETSIM_EXPECT(test, level.laplacian.seam_relative_max <= 2.0e-3);
    }

    // Regression bounds at the L5 development resolution.
    const auto& development = levels[2];
    PLANETSIM_EXPECT(test, development.cell_count == 10'242U);
    PLANETSIM_EXPECT(test, development.gradient.relative_l2 <= 2.0e-3);
    PLANETSIM_EXPECT(test, development.divergence.relative_l2 <= 6.0e-4);
    PLANETSIM_EXPECT(test, development.poisson_solution.relative_l2 <= 1.0e-3);

    return test.result();
}
