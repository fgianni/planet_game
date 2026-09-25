#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>

namespace planetsim {

enum class RandomStreamId : std::uint32_t {
    validation = 0x0000'0001U,
    weather = 0x0001'0001U,
    hydrology = 0x0002'0001U,
};

[[nodiscard]] constexpr std::uint64_t mix_random_key(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] constexpr std::uint64_t keyed_random_u64(std::uint64_t world_seed,
                                                       RandomStreamId stream, SimulationTick tick,
                                                       std::uint32_t cell_key,
                                                       std::uint32_t sample_index = 0) noexcept {
    std::uint64_t key = mix_random_key(world_seed);
    key = mix_random_key(key ^ static_cast<std::uint64_t>(stream));
    key = mix_random_key(key ^ static_cast<std::uint64_t>(tick));
    key = mix_random_key(key ^ static_cast<std::uint64_t>(cell_key));
    return mix_random_key(key ^ static_cast<std::uint64_t>(sample_index));
}

[[nodiscard]] constexpr double keyed_random_unit_double(std::uint64_t world_seed,
                                                        RandomStreamId stream, SimulationTick tick,
                                                        std::uint32_t cell_key,
                                                        std::uint32_t sample_index = 0) noexcept {
    constexpr double inverse_53_bits = 1.0 / 9'007'199'254'740'992.0;
    return static_cast<double>(keyed_random_u64(world_seed, stream, tick, cell_key, sample_index) >>
                               11U) *
           inverse_53_bits;
}

}  // namespace planetsim
