#pragma once

#include "sim/presentation/coast_water_field.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

// A coast page (ADR-0030 Amendment 1, §A1.3): one value a node on one tile
// key's lattice, at the global dyadic face coordinates, so neighbouring
// pages share their edge nodes. 256 dyadic intervals need 257
// endpoint-inclusive nodes per side.
//
// Two kinds, by level (ADR-0030 A1.3a):
// - point: the canonical signed water field s at the node. A parent's nodes
//   are every other node of its children's, value for value.
// - coverage: at levels whose nodes are coarser than the relief's finest
//   features, the water coverage of the node's footprint minus 0.5. The
//   footprint is S x S sub-samples across one nominal node spacing, on the
//   tangent plane of a basis that depends on the node's direction alone, so a node on a cube-face edge has one
//   value whichever face computes it. Coverage at two levels is two
//   filters: their handoff is what the screen-space gate measures.
// Water is a value above 0 in both kinds; a pixel's coverage reads a
// coverage page's bilinear value + 0.5.
//
// Each node is an IEEE binary16 (round to nearest even), stored explicitly
// little-endian so page bytes do not depend on the route or the host. A
// half keeps the sign exactly and resolves s to about 5e-4 m near 1 m.
inline constexpr std::uint32_t coast_page_intervals = 256;
inline constexpr std::uint32_t coast_page_nodes = coast_page_intervals + 1;
inline constexpr int coast_footprint_cap = 4;       // sub-samples a footprint side, at most (A1.3a)
inline constexpr int coast_coverage_threshold = 3;  // fewer sub-samples a side: a point page

enum class CoastPageKind : std::uint8_t { point, coverage };

struct CoastPage {
    TileKey key;
    CoastPageKind kind = CoastPageKind::point;
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

// The relief's finest octave wavelength, and the footprint sub-samples a
// side a level needs: its nominal node spacing over half that wavelength,
// at most coast_footprint_cap.
[[nodiscard]] double coast_finest_wavelength_rad(const TerrainRelief& relief);
[[nodiscard]] int coast_footprint_samples(std::uint8_t level, double finest_wavelength_rad,
                                          int cap = coast_footprint_cap);
[[nodiscard]] CoastPageKind coast_page_kind(std::uint8_t level, const TerrainRelief& relief);

// The value a page of `level` holds at a direction: what make_coast_page
// stores there, before rounding (tile geometry reads it for its ring
// points). `hint` is updated to a nearby cell.
[[nodiscard]] double coast_node_value(const CoastWaterField& field, std::uint8_t level, const Vec3d& direction,
                                      CellId& hint, int cap = coast_footprint_cap);

// `workers` threads take whole rows; the bytes do not depend on them.
// `cap` (at least coast_coverage_threshold) bounds the footprint's
// sub-samples a side: a cost against fidelity, measured in R1-05.
[[nodiscard]] CoastPage make_coast_page(const TileKey& key, const CoastWaterField& field,
                                        std::size_t workers = 1, int cap = coast_footprint_cap);

// Round-to-nearest-even IEEE binary16, and back.
[[nodiscard]] std::uint16_t half_from_float(float value) noexcept;
[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept;

} // namespace planetsim::presentation
