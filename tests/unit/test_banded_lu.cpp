#include "sim/core/math/banded_lu.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

// The banded LU with partial pivoting behind the zonal circulation's Newton
// solve (ADR-0011 §14).
int main() {
    planetsim::test::Context test;

    // A non-symmetric band whose diagonal is zero on every third row, so
    // that elimination must pivot (as the circulation's lid rows do).
    constexpr std::size_t n = 40;
    constexpr std::size_t lower = 3;
    constexpr std::size_t upper = 2;
    planetsim::BandedMatrix matrix(n, lower, upper);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i > lower ? i - lower : 0U; j <= std::min(n - 1U, i + upper); ++j) {
            const double value =
                std::sin(0.7 * static_cast<double>(i) + 1.3 * static_cast<double>(j));
            matrix.at(i, j) = i == j ? (i % 3U == 0U ? 0.0 : 4.0 + value) : value;
        }
    }
    PLANETSIM_EXPECT(test, matrix.in_band(10, 7) && matrix.in_band(10, 12));
    PLANETSIM_EXPECT(test, !matrix.in_band(10, 6) && !matrix.in_band(10, 13));
    PLANETSIM_EXPECT_THROWS(test, std::out_of_range, matrix.at(10, 13));

    std::vector<double> exact(n);
    for (std::size_t i = 0; i < n; ++i) {
        exact[i] = std::cos(0.37 * static_cast<double>(i)) + 0.1 * static_cast<double>(i);
    }
    std::vector<double> rhs(n);
    matrix.multiply(exact, rhs);
    const planetsim::BandedLU lu(matrix);
    lu.solve(rhs);
    double error = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        error = std::max(error, std::abs(rhs[i] - exact[i]));
    }
    PLANETSIM_EXPECT_NEAR(test, error, 0.0, 1e-12);

    // The same factorisation solves a second right-hand side.
    std::vector<double> ones(n, 1.0);
    std::vector<double> product(n);
    matrix.multiply(ones, product);
    lu.solve(product);
    for (std::size_t i = 0; i < n; ++i) {
        PLANETSIM_EXPECT_NEAR(test, product[i], 1.0, 1e-12);
    }

    // A singular band is refused rather than solved.
    planetsim::BandedMatrix singular(5, 1, 1);
    for (std::size_t i = 0; i < 5; ++i) {
        singular.at(i, i) = 1.0;
    }
    singular.at(2, 2) = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::runtime_error, planetsim::BandedLU{singular});
    return test.result();
}
