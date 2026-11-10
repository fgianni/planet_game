#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"
#include "sim/presentation/terrain_relief.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

// The canonical water field of ADR-0030 Amendment 1 (§A1.2):
//
//   s(x) = (z_sea − h(x)) − (1 − κ(x)) D,  water where s(x) > 0,
//
// h the relief's sample(), κ the relief's own blend of a per-cell κ_c (0 for
// a depression cell: all land, its lowest quantile below sea level; 1
// otherwise; a corner the mean of its three cells'), and D the deepest
// depression below sea level plus 1 m. Where κ = 1 it is the plain contour;
// a depression cell's own ground is always dry. A pure function of the
// position, the world seed and the snapshot.
class CoastField {
public:
    // The relief must outlive the field and be built from the same snapshot.
    CoastField(const TerrainRelief& relief, const TerrainSnapshot& terrain);

    struct Sample {
        double water_m = 0.0;     // s(x): positive is water
        double elevation_m = 0.0; // h(x)
        CellId cell;              // the nearest cell; a hint for the next nearby point
    };

    [[nodiscard]] Sample sample(const Vec3d& direction_unit, CellId hint = CellId{0}) const;
    [[nodiscard]] double connectivity(const Vec3d& direction_unit, CellId hint = CellId{0}) const; // κ(x)

    [[nodiscard]] double sea_level_m() const noexcept { return sea_level_m_; }
    [[nodiscard]] double depression_depth_m() const noexcept { return depth_m_; } // D; 0 without depressions
    [[nodiscard]] std::size_t depression_cell_count() const noexcept { return depressions_; }
    [[nodiscard]] bool is_depression(CellId cell) const { return cell_kappa_.at(cell.to_index()) == 0.0; }
    [[nodiscard]] const TerrainRelief& relief() const noexcept { return *relief_; }

private:
    const TerrainRelief* relief_;
    double sea_level_m_ = 0.0;
    double depth_m_ = 0.0;
    std::size_t depressions_ = 0;
    std::vector<double> cell_kappa_;
    std::vector<double> corner_kappa_;
};

// A coast page (§A1.3): s on the (size + 1)² lattice of one tile key, at the
// global dyadic face coordinates, so neighbouring pages share their edge
// points and a parent's points are every other point of its children's.
// Values are IEEE half floats: exact signs, and steps far below a metre
// near sea level.
inline constexpr std::uint32_t coast_page_size = 256; // intervals per edge: 8 per tile quad

struct CoastPage {
    TileKey key;
    std::vector<std::uint16_t> water_half; // (size + 1)², row j then column i

    [[nodiscard]] static constexpr std::uint32_t points() noexcept { return coast_page_size + 1U; }
    [[nodiscard]] std::uint16_t bits(std::uint32_t i, std::uint32_t j) const {
        return water_half[static_cast<std::size_t>(j) * points() + i];
    }
    [[nodiscard]] float water_m(std::uint32_t i, std::uint32_t j) const;
    // The bilinear interpolation the pixels read, i and j in [0, size].
    [[nodiscard]] double interpolated_water_m(double i, double j) const;
};

// The face coordinate of lattice line i of a page.
[[nodiscard]] double coast_page_coordinate(std::uint32_t tile, std::uint8_t level, std::uint32_t i);
[[nodiscard]] Vec3d coast_page_direction(const TileKey& key, std::uint32_t i, std::uint32_t j);

[[nodiscard]] CoastPage make_coast_page(const TileKey& key, const CoastField& field);

// Round-to-nearest-even IEEE binary16, and back.
[[nodiscard]] std::uint16_t half_from_float(float value) noexcept;
[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept;

}  // namespace planetsim::presentation
