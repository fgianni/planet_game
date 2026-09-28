#include "sim/core/random/noise.hpp"

#include "sim/core/random/counter_rng.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {
namespace {

[[nodiscard]] std::uint64_t lattice_hash(std::uint64_t seed, std::uint64_t salt, std::int64_t ix,
                                         std::int64_t iy, std::int64_t iz) noexcept {
    std::uint64_t key = mix_random_key(seed);
    key = mix_random_key(key ^ salt);
    key = mix_random_key(key ^ static_cast<std::uint64_t>(ix));
    key = mix_random_key(key ^ static_cast<std::uint64_t>(iy));
    return mix_random_key(key ^ static_cast<std::uint64_t>(iz));
}

// Uniform unit vector: z uniform in [-1, 1], azimuth uniform in [0, 2 pi).
[[nodiscard]] Vec3d lattice_gradient(std::uint64_t hash) noexcept {
    constexpr double inverse_32_bits = 1.0 / 4'294'967'296.0;
    const double u = static_cast<double>(hash >> 32U) * inverse_32_bits;
    const double v = static_cast<double>(hash & 0xFFFF'FFFFULL) * inverse_32_bits;
    const double z = 2.0 * u - 1.0;
    const double radial = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double azimuth = 2.0 * std::numbers::pi * v;
    return {radial * std::cos(azimuth), radial * std::sin(azimuth), z};
}

[[nodiscard]] constexpr double fade(double t) noexcept {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] constexpr double lerp(double from, double to, double t) noexcept {
    return from + t * (to - from);
}

}  // namespace

double gradient_noise(std::uint64_t seed, std::uint64_t salt, const Vec3d& position) {
    const double floor_x = std::floor(position.x);
    const double floor_y = std::floor(position.y);
    const double floor_z = std::floor(position.z);
    const auto ix = static_cast<std::int64_t>(floor_x);
    const auto iy = static_cast<std::int64_t>(floor_y);
    const auto iz = static_cast<std::int64_t>(floor_z);
    const double fx = position.x - floor_x;
    const double fy = position.y - floor_y;
    const double fz = position.z - floor_z;

    double corner_values[8];
    for (int corner = 0; corner < 8; ++corner) {
        const int dx = corner & 1;
        const int dy = (corner >> 1) & 1;
        const int dz = (corner >> 2) & 1;
        const Vec3d gradient = lattice_gradient(lattice_hash(seed, salt, ix + dx, iy + dy, iz + dz));
        const Vec3d offset{fx - static_cast<double>(dx), fy - static_cast<double>(dy),
                           fz - static_cast<double>(dz)};
        corner_values[corner] = dot(gradient, offset);
    }

    const double ux = fade(fx);
    const double uy = fade(fy);
    const double uz = fade(fz);
    const double x00 = lerp(corner_values[0], corner_values[1], ux);
    const double x10 = lerp(corner_values[2], corner_values[3], ux);
    const double x01 = lerp(corner_values[4], corner_values[5], ux);
    const double x11 = lerp(corner_values[6], corner_values[7], ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

double fbm(std::uint64_t seed, std::uint64_t salt, const Vec3d& position,
           const FbmParameters& parameters) {
    if (parameters.octaves == 0U || !(parameters.base_frequency > 0.0) ||
        !(parameters.lacunarity > 0.0) || !(parameters.gain > 0.0)) {
        throw std::invalid_argument("fbm requires positive octaves, frequency, lacunarity, gain");
    }
    double sum = 0.0;
    double amplitude_sum = 0.0;
    double amplitude = 1.0;
    double frequency = parameters.base_frequency;
    for (std::uint32_t octave = 0; octave < parameters.octaves; ++octave) {
        sum += amplitude * gradient_noise(seed, salt + octave, position * frequency);
        amplitude_sum += amplitude;
        amplitude *= parameters.gain;
        frequency *= parameters.lacunarity;
    }
    return sum / amplitude_sum;
}

}  // namespace planetsim
