#pragma once

#include "sim/core/math/vec3d.hpp"

#include <cstdint>

namespace planetsim {

// Deterministic 3-D gradient noise (Perlin-type) for procedural generation.
//
// Each integer lattice point carries a unit gradient drawn uniformly on the
// sphere from mix_random_key(seed, salt, ix, iy, iz), so the value at a point
// depends only on (seed, salt, position): never on evaluation order, worker
// count or any shared generator state (ADR-0003). The value is zero at lattice
// points and bounded by sqrt(3)/2 in magnitude. Uses the quintic fade
// 6t^5 - 15t^4 + 10t^3, which is C2 across lattice cells. Its sampled
// standard deviation is about 0.19 (tests/unit/test_noise.cpp), so the bound
// is far from typical values; amplitudes built on it should allow for that.
[[nodiscard]] double gradient_noise(std::uint64_t seed, std::uint64_t salt,
                                    const Vec3d& position);

struct FbmParameters {
    double base_frequency = 1.0;  // lattice cells per unit length at octave 0
    std::uint32_t octaves = 5;
    double lacunarity = 2.0;      // frequency ratio between octaves
    double gain = 0.5;            // amplitude ratio between octaves
};

// Nominal standard deviation of gradient_noise over uniformly distributed
// positions (measured 0.1906; pinned within 5 % by tests/unit/test_noise.cpp).
inline constexpr double gradient_noise_standard_deviation = 0.19;

// Fractal sum of gradient noise, normalised by the sum of octave amplitudes so
// the result lies in [-1, 1]. Octave k uses salt + k, so octaves are
// independent lattices. On the planet it is evaluated at unit-sphere positions,
// so base_frequency is in lattice cells per planetary radius.
[[nodiscard]] double fbm(std::uint64_t seed, std::uint64_t salt, const Vec3d& position,
                         const FbmParameters& parameters);

// Nominal standard deviation of fbm(): octaves are independent lattices, so
// sigma = sigma_0 sqrt(sum gain^(2k)) / sum gain^k.
[[nodiscard]] double fbm_standard_deviation(const FbmParameters& parameters);

// fbm() divided by its nominal standard deviation: unit-RMS noise, so an
// amplitude multiplying it is a root-mean-square value. Not bounded.
[[nodiscard]] double normalized_fbm(std::uint64_t seed, std::uint64_t salt, const Vec3d& position,
                                    const FbmParameters& parameters);

}  // namespace planetsim
