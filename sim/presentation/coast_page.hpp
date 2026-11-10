#pragma once

#include "sim/presentation/coast_water_field.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

// A coast page (ADR-0030 Amendment 1, §A1.3): the canonical signed water
// field s on one tile key's lattice, at the global dyadic face coordinates,
// so neighbouring pages share their edge nodes and a parent's nodes are
// every other node of its children's. 256 dyadic intervals need 257
// endpoint-inclusive nodes per side.
//
// Each node is an IEEE binary16 (round to nearest even), stored explicitly
// little-endian so page bytes do not depend on the route or the host. A
// half keeps s's sign exactly and resolves it to about 5e-4 m near 1 m, so
// quantisation moves the bilinear zero line by millimetres, not metres.
inline constexpr std::uint32_t coast_page_intervals = 256;
inline constexpr std::uint32_t coast_page_nodes = coast_page_intervals + 1;

struct CoastPage {
    TileKey key;
    std::vector<std::uint8_t> signed_water_le; // row-major binary16, 2 bytes a node
    [[nodiscard]] std::uint16_t half_bits(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] double signed_water_m(std::uint32_t x, std::uint32_t y) const;
    // The bilinear interpolation the pixels read, x and y in [0, intervals].
    [[nodiscard]] double bilinear_signed_water_m(double x, double y) const;
};

// The face coordinate of lattice line i of a page at `tile`, `level`, and
// the direction of node (x, y) of a page.
[[nodiscard]] double coast_page_coordinate(std::uint32_t tile, std::uint8_t level, std::uint32_t i);
[[nodiscard]] Vec3d coast_page_direction(const TileKey& key, std::uint32_t x, std::uint32_t y);

[[nodiscard]] CoastPage make_coast_page(const TileKey& key, const CoastWaterField& field);

// Round-to-nearest-even IEEE binary16, and back.
[[nodiscard]] std::uint16_t half_from_float(float value) noexcept;
[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept;

} // namespace planetsim::presentation
