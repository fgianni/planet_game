#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace planetsim {

// XXH3-64 with seed 0 and the default secret, as specified by xxHash 0.8
// (XXH3_64bits). ADR-0003 §3.3 names it for the run manifest's state_hash.
// A portable scalar implementation: little-endian reads, no SIMD, so the
// value is the same on every platform and build.
[[nodiscard]] std::uint64_t xxh3_64(std::span<const std::byte> bytes) noexcept;

}  // namespace planetsim
