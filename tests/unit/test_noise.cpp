#include "sim/core/random/counter_rng.hpp"
#include "sim/core/random/noise.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr std::uint64_t test_seed = 0x6E01'5E00'0000'0001ULL;
constexpr std::uint64_t test_salt = 17U;

[[nodiscard]] planetsim::Vec3d random_point(std::uint32_t index, double extent) {
    using planetsim::keyed_random_unit_double;
    using planetsim::RandomStreamId;
    return {extent * (2.0 * keyed_random_unit_double(1U, RandomStreamId::validation, 0, index, 0) - 1.0),
            extent * (2.0 * keyed_random_unit_double(1U, RandomStreamId::validation, 0, index, 1) - 1.0),
            extent * (2.0 * keyed_random_unit_double(1U, RandomStreamId::validation, 0, index, 2) - 1.0)};
}

// Sample autocorrelation of the noise at a fixed displacement.
[[nodiscard]] double autocorrelation(const planetsim::Vec3d& displacement, std::uint32_t samples) {
    double product_sum = 0.0;
    double square_sum = 0.0;
    for (std::uint32_t index = 0; index < samples; ++index) {
        const planetsim::Vec3d point = random_point(index, 64.0);
        const double here = planetsim::gradient_noise(test_seed, test_salt, point);
        const double there = planetsim::gradient_noise(test_seed, test_salt, point + displacement);
        product_sum += here * there;
        square_sum += here * here;
    }
    return product_sum / square_sum;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    constexpr std::uint32_t sample_count = 100'000U;
    const double noise_bound = std::sqrt(3.0) / 2.0;

    // Determinism and seed/salt sensitivity.
    std::uint32_t seed_differences = 0;
    std::uint32_t salt_differences = 0;
    for (std::uint32_t index = 0; index < 1'000U; ++index) {
        const planetsim::Vec3d point = random_point(index, 10.0);
        const double first = planetsim::gradient_noise(test_seed, test_salt, point);
        const double second = planetsim::gradient_noise(test_seed, test_salt, point);
        PLANETSIM_EXPECT(test, std::bit_cast<std::uint64_t>(first) ==
                                   std::bit_cast<std::uint64_t>(second));
        if (planetsim::gradient_noise(test_seed + 1U, test_salt, point) != first) {
            ++seed_differences;
        }
        if (planetsim::gradient_noise(test_seed, test_salt + 1U, point) != first) {
            ++salt_differences;
        }
    }
    PLANETSIM_EXPECT(test, seed_differences == 1'000U);
    PLANETSIM_EXPECT(test, salt_differences == 1'000U);

    // Zero at lattice points, including negative coordinates.
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            const planetsim::Vec3d lattice_point{static_cast<double>(x), static_cast<double>(y),
                                                 static_cast<double>(x - y)};
            PLANETSIM_EXPECT(test, planetsim::gradient_noise(test_seed, test_salt,
                                                             lattice_point) == 0.0);
        }
    }

    // Bounded range, near-zero mean, non-trivial variance.
    const planetsim::FbmParameters fbm_parameters{2.0, 6U, 2.0, 0.5};
    double sum = 0.0;
    double square_sum = 0.0;
    double maximum_magnitude = 0.0;
    double maximum_fbm_magnitude = 0.0;
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const planetsim::Vec3d point = random_point(index, 50.0);
        const double value = planetsim::gradient_noise(test_seed, test_salt, point);
        sum += value;
        square_sum += value * value;
        maximum_magnitude = std::max(maximum_magnitude, std::abs(value));
        const double fractal = planetsim::fbm(test_seed, test_salt, point, fbm_parameters);
        maximum_fbm_magnitude = std::max(maximum_fbm_magnitude, std::abs(fractal));
    }
    const double mean = sum / static_cast<double>(sample_count);
    const double variance = square_sum / static_cast<double>(sample_count) - mean * mean;
    std::cout << "noise_mean: " << mean << "\nnoise_variance: " << variance
              << "\nnoise_max_magnitude: " << maximum_magnitude
              << "\nfbm_max_magnitude: " << maximum_fbm_magnitude << '\n';
    PLANETSIM_EXPECT(test, maximum_magnitude <= noise_bound);
    PLANETSIM_EXPECT(test, maximum_fbm_magnitude <= 1.0);
    PLANETSIM_EXPECT(test, std::abs(mean) < 0.01);
    PLANETSIM_EXPECT(test, variance > 0.01);

    // No axis-aligned lattice artefact: the autocorrelation at a fixed lag is
    // the same along the lattice axes and along face and body diagonals.
    constexpr double lag = 0.5;
    const double diagonal_2 = lag / std::sqrt(2.0);
    const double diagonal_3 = lag / std::sqrt(3.0);
    const std::array<planetsim::Vec3d, 6> displacements{{
        {lag, 0.0, 0.0},
        {0.0, lag, 0.0},
        {0.0, 0.0, lag},
        {diagonal_2, diagonal_2, 0.0},
        {diagonal_3, diagonal_3, diagonal_3},
        {diagonal_3, -diagonal_3, diagonal_3},
    }};
    double minimum_correlation = 1.0;
    double maximum_correlation = -1.0;
    for (const auto& displacement : displacements) {
        const double correlation = autocorrelation(displacement, sample_count);
        std::cout << "autocorrelation(" << displacement.x << ',' << displacement.y << ','
                  << displacement.z << "): " << correlation << '\n';
        minimum_correlation = std::min(minimum_correlation, correlation);
        maximum_correlation = std::max(maximum_correlation, correlation);
    }
    PLANETSIM_EXPECT(test, maximum_correlation - minimum_correlation < 0.05);

    // Continuity: a tiny displacement gives a tiny change.
    for (std::uint32_t index = 0; index < 1'000U; ++index) {
        const planetsim::Vec3d point = random_point(index, 10.0);
        const double here = planetsim::gradient_noise(test_seed, test_salt, point);
        const double near = planetsim::gradient_noise(test_seed, test_salt,
                                                      point + planetsim::Vec3d{1e-7, 1e-7, 1e-7});
        PLANETSIM_EXPECT(test, std::abs(here - near) < 1e-5);
    }

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::fbm(test_seed, test_salt, planetsim::Vec3d{},
                                           planetsim::FbmParameters{1.0, 0U, 2.0, 0.5}));

    return test.result();
}
