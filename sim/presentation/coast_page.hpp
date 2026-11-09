#pragma once

#include "sim/presentation/coast_water_field.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

// 256 dyadic intervals require 257 endpoint-inclusive nodes per side.
// Signed fixed-point metres, nearest metre; saturated values retain their
// sign. Page bytes are explicitly little-endian and independent of route.
inline constexpr std::uint32_t coast_page_intervals = 256;
inline constexpr std::uint32_t coast_page_nodes = coast_page_intervals + 1;

struct CoastPage {
    TileKey key;
    std::vector<std::uint8_t> signed_water_le; // row-major int16, 2 bytes/node
    [[nodiscard]] double signed_water_m(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] double bilinear_signed_water_m(double x, double y) const;
};

[[nodiscard]] CoastPage make_coast_page(const TileKey& key, const CoastWaterField& field);

} // namespace planetsim::presentation
