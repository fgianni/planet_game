#include "sim/presentation/coast_page.hpp"

#include "sim/planet/terrain/hypsometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace planetsim::presentation {

CoastField::CoastField(const TerrainRelief& relief, const TerrainSnapshot& terrain)
    : relief_(&relief), sea_level_m_(terrain.sea_level_m) {
    const PlanetMesh& mesh = relief.mesh();
    const std::size_t cells = mesh.cell_count();
    if (terrain.land_fraction.size() != cells || terrain.mean_elevation_m.size() != cells) {
        throw std::invalid_argument("coast field: the terrain does not match the relief's mesh");
    }
    constexpr std::size_t q = hypsometry_quantile_count;
    const bool curves = terrain.hypsometry_m.size() == cells * q;
    cell_kappa_.assign(cells, 1.0);
    double deepest_m = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        // ADR-0005: an all-land cell with ground below sea level is not
        // connected to the ocean (an ocean-connected one has ocean fraction).
        const double lowest_m = curves ? static_cast<double>(terrain.hypsometry_m[cell * q])
                                       : static_cast<double>(terrain.mean_elevation_m[cell]);
        if (terrain.land_fraction[cell] >= 1.0F - 1e-6F && lowest_m < sea_level_m_) {
            cell_kappa_[cell] = 0.0;
            deepest_m = std::max(deepest_m, sea_level_m_ - lowest_m);
            ++depressions_;
        }
    }
    depth_m_ = depressions_ > 0U ? deepest_m + 1.0 : 0.0;
    corner_kappa_.assign(mesh.corner_count(), 0.0);
    std::vector<int> count(mesh.corner_count(), 0);
    for (const auto& cell : mesh.cells()) {
        for (const CornerIndex corner : mesh.cell_corners(cell.id)) {
            corner_kappa_[corner] += cell_kappa_[cell.id.to_index()];
            ++count[corner];
        }
    }
    for (std::size_t corner = 0; corner < corner_kappa_.size(); ++corner) {
        corner_kappa_[corner] /= std::max(count[corner], 1);
    }
}

double CoastField::connectivity(const Vec3d& direction_unit, CellId hint) const {
    const ReliefWeights w = relief_->weights(direction_unit, hint);
    // Non-negative weights sum to one mathematically, but their floating
    // sum can round to 1 + 1 ulp. Keep the physical connectedness in [0,1]
    // before it shifts the sea-level contour by the depression depth.
    return std::clamp(w.own * cell_kappa_[w.cell.to_index()] + w.first_weight * corner_kappa_[w.first] +
                          w.second_weight * corner_kappa_[w.second], 0.0, 1.0);
}

CoastField::Sample CoastField::sample(const Vec3d& direction_unit, CellId hint) const {
    const ReliefSample relief = relief_->sample(direction_unit, hint);
    double water = sea_level_m_ - relief.elevation_m;
    if (depth_m_ > 0.0) water -= (1.0 - connectivity(direction_unit, relief.cell)) * depth_m_;
    return {water, relief.elevation_m, relief.cell};
}

// F. Giesen's round-to-nearest-even conversion.
std::uint16_t half_from_float(float value) noexcept {
    std::uint32_t f = std::bit_cast<std::uint32_t>(value);
    constexpr std::uint32_t infinity = 255U << 23U;
    constexpr std::uint32_t largest = (127U + 16U) << 23U;
    constexpr std::uint32_t denormal_magic = ((127U - 15U) + (23U - 10U) + 1U) << 23U;
    const std::uint32_t sign = f & 0x80000000U;
    f ^= sign;
    std::uint16_t out = 0;
    if (f >= largest) {
        out = f > infinity ? 0x7e00U : 0x7c00U;
    } else if (f < (113U << 23U)) {
        const float shifted = std::bit_cast<float>(f) + std::bit_cast<float>(denormal_magic);
        out = static_cast<std::uint16_t>(std::bit_cast<std::uint32_t>(shifted) - denormal_magic);
    } else {
        const std::uint32_t odd = (f >> 13U) & 1U;
        f += (static_cast<std::uint32_t>(15 - 127) << 23U) + 0xfffU;
        f += odd;
        out = static_cast<std::uint16_t>(f >> 13U);
    }
    return static_cast<std::uint16_t>(out | (sign >> 16U));
}

float half_to_float(std::uint16_t bits) noexcept {
    const std::uint32_t sign = (static_cast<std::uint32_t>(bits) & 0x8000U) << 16U;
    const std::uint32_t exponent = (bits >> 10U) & 0x1fU;
    const std::uint32_t mantissa = bits & 0x3ffU;
    if (exponent == 0U) {
        const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
        return sign != 0U ? -magnitude : magnitude;
    }
    if (exponent == 31U) return std::bit_cast<float>(sign | 0x7f800000U | (mantissa << 13U));
    return std::bit_cast<float>(sign | ((exponent + 112U) << 23U) | (mantissa << 13U));
}

float CoastPage::water_m(std::uint32_t i, std::uint32_t j) const { return half_to_float(bits(i, j)); }

double CoastPage::interpolated_water_m(double i, double j) const {
    constexpr std::uint32_t last = coast_page_size - 1U;
    const auto x = std::min(static_cast<std::uint32_t>(std::max(i, 0.0)), last);
    const auto y = std::min(static_cast<std::uint32_t>(std::max(j, 0.0)), last);
    const double u = std::clamp(i - static_cast<double>(x), 0.0, 1.0);
    const double v = std::clamp(j - static_cast<double>(y), 0.0, 1.0);
    return (1.0 - u) * (1.0 - v) * water_m(x, y) + u * (1.0 - v) * water_m(x + 1U, y) +
           (1.0 - u) * v * water_m(x, y + 1U) + u * v * water_m(x + 1U, y + 1U);
}

double coast_page_coordinate(std::uint32_t tile, std::uint8_t level, std::uint32_t i) {
    const double lines = static_cast<double>(coast_page_size) * std::ldexp(1.0, level);
    return -1.0 + 2.0 * (static_cast<double>(tile) * coast_page_size + static_cast<double>(i)) / lines;
}

Vec3d coast_page_direction(const TileKey& key, std::uint32_t i, std::uint32_t j) {
    return tile_face_direction(key.face, coast_page_coordinate(key.x, key.level, i),
                               coast_page_coordinate(key.y, key.level, j));
}

CoastPage make_coast_page(const TileKey& key, const CoastField& field) {
    CoastPage page;
    page.key = key;
    page.water_half.resize(static_cast<std::size_t>(CoastPage::points()) * CoastPage::points());
    CellId hint = field.relief().nearest_cell(tile_center_direction(key), CellId{0});
    constexpr double largest_half = 65504.0;
    for (std::uint32_t j = 0; j < CoastPage::points(); ++j) {
        for (std::uint32_t i = 0; i < CoastPage::points(); ++i) {
            const CoastField::Sample s = field.sample(coast_page_direction(key, i, j), hint);
            hint = s.cell;
            const double clamped = std::clamp(s.water_m, -largest_half, largest_half);
            page.water_half[static_cast<std::size_t>(j) * CoastPage::points() + i] =
                half_from_float(static_cast<float>(clamped));
        }
    }
    return page;
}

}  // namespace planetsim::presentation
