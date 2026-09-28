#pragma once

#include "sim/core/random/counter_rng.hpp"

#include <cstdint>

namespace planetsim {

// Keys of every random quantity drawn by the geological generator. A draw is
// keyed by (world_seed, RandomStreamId::geology, tick 0, index, draw * 256 +
// component), where index is the plate index or a CellId, so every value is
// independent of evaluation order and worker count (ADR-0003). These values
// are part of the replay contract: renumbering one changes every generated
// planet.
enum class GeologyDraw : std::uint32_t {
    plate_seed_point = 1,     // index: plate; components: 2 per placement attempt
    plate_growth_factor = 2,  // index: plate
    plate_rotation_pole = 3,  // index: plate; components 0, 1
    plate_speed = 4,          // index: plate
    continental_bias = 5,     // index: plate
    continental_age = 6,      // index: lowest CellId of the continental region
};

// Salts of the noise fields; octave k of an fbm uses salt + k.
enum class GeologyNoise : std::uint64_t {
    plate_growth = 0x100,
    continental_propensity = 0x200,
    roughness = 0x300,
    transform_fault = 0x400,
    subcell_roughness = 0x500,
};

[[nodiscard]] constexpr double geology_random_unit(std::uint64_t world_seed, GeologyDraw draw,
                                                   std::uint32_t index,
                                                   std::uint32_t component = 0) noexcept {
    return keyed_random_unit_double(world_seed, RandomStreamId::geology, 0, index,
                                    static_cast<std::uint32_t>(draw) * 256U + component);
}

[[nodiscard]] constexpr std::uint64_t geology_noise_salt(GeologyNoise noise) noexcept {
    return static_cast<std::uint64_t>(noise);
}

}  // namespace planetsim
