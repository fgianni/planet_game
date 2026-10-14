#pragma once

#include <array>
#include <cstdint>

namespace planetsim::presentation {

// The kinds of sub-cell procedural detail a style may draw (specification
// §31.5). Each draws its own noise field, so that, say, snow patches do not
// follow the coastline's noise.
enum class DetailKind : std::uint32_t {
    coast = 1,
    land_mix = 2,
    snow = 3,
    sea_ice = 4,
    texture = 5,
    terrain = 6, // sub-cell relief of the terrain tiles (ADR-0022 §4.9)
};

inline constexpr std::array<DetailKind, 6> detail_kinds{
    DetailKind::coast, DetailKind::land_mix, DetailKind::snow, DetailKind::sea_ice,
    DetailKind::texture, DetailKind::terrain};

// Offsets in noise-lattice units: a style samples its noise at
// position * frequency + offset, so the field differs between worlds and
// kinds but never between runs of the same world.
inline constexpr double detail_noise_offset_range = 512.0;

// ADR-0018 §4.8: drawn through the counter RNG from the reserved presentation
// stream. A detail field is continuous across the planet, so it is keyed by
// (world_seed, detail_kind) and sampled by position; the cell is the one
// under that position, and a per-cell key would put a seam at every edge.
[[nodiscard]] std::array<double, 3> detail_noise_offset(std::uint64_t world_seed,
                                                        DetailKind kind) noexcept;

}  // namespace planetsim::presentation
