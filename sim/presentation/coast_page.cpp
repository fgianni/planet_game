#include "sim/presentation/coast_page.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace planetsim::presentation {
namespace {

void validate(const TileKey& key) {
    if (key.face >= 6 || key.level > tile_max_level ||
        key.x >= (1U << key.level) || key.y >= (1U << key.level))
        throw std::invalid_argument("invalid coast page key");
}

} // namespace

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

double coast_page_coordinate(std::uint32_t tile, std::uint8_t level, std::uint32_t i) {
    // Integer units first: adjacent and parent/child pages obtain
    // bit-identical doubles.
    const double lines = static_cast<double>(coast_page_intervals) * std::ldexp(1.0, level);
    return -1.0 + 2.0 * (static_cast<double>(tile) * coast_page_intervals + static_cast<double>(i)) / lines;
}

Vec3d coast_page_direction(const TileKey& key, std::uint32_t x, std::uint32_t y) {
    return tile_face_direction(key.face, coast_page_coordinate(key.x, key.level, x),
                               coast_page_coordinate(key.y, key.level, y));
}

CoastPage make_coast_page(const TileKey& key, const CoastWaterField& field) {
    validate(key);
    CoastPage page;
    page.key = key;
    page.signed_water_le.resize(static_cast<std::size_t>(coast_page_nodes) * coast_page_nodes * 2);
    CellId hint{0};
    constexpr double largest_half = 65504.0;
    for (std::uint32_t y = 0; y < coast_page_nodes; ++y) {
        for (std::uint32_t x = 0; x < coast_page_nodes; ++x) {
            const CoastWaterSample water = field.sample(coast_page_direction(key, x, y), hint);
            if (!std::isfinite(water.signed_water_m))
                throw std::runtime_error("non-finite canonical coast sample");
            // Zero is dry by the field's strict > 0 water rule; a half keeps
            // the sign of every nonzero value it does not round to zero.
            const std::uint16_t bits = half_from_float(
                static_cast<float>(std::clamp(water.signed_water_m, -largest_half, largest_half)));
            const std::size_t offset = 2 * (static_cast<std::size_t>(y) * coast_page_nodes + x);
            page.signed_water_le[offset] = static_cast<std::uint8_t>(bits & 0xffU);
            page.signed_water_le[offset + 1] = static_cast<std::uint8_t>(bits >> 8U);
            hint = water.cell;
        }
    }
    return page;
}

std::uint16_t CoastPage::half_bits(std::uint32_t x, std::uint32_t y) const {
    if (x >= coast_page_nodes || y >= coast_page_nodes ||
        signed_water_le.size() != static_cast<std::size_t>(coast_page_nodes) * coast_page_nodes * 2)
        throw std::out_of_range("invalid coast page sample");
    const std::size_t offset = 2 * (static_cast<std::size_t>(y) * coast_page_nodes + x);
    return static_cast<std::uint16_t>(signed_water_le[offset] |
                                      (static_cast<std::uint16_t>(signed_water_le[offset + 1]) << 8U));
}

double CoastPage::signed_water_m(std::uint32_t x, std::uint32_t y) const {
    return static_cast<double>(half_to_float(half_bits(x, y)));
}

double CoastPage::bilinear_signed_water_m(double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
        x > coast_page_intervals || y > coast_page_intervals)
        throw std::out_of_range("invalid coast page coordinates");
    const auto x0 = std::min(static_cast<std::uint32_t>(x), coast_page_intervals - 1U);
    const auto y0 = std::min(static_cast<std::uint32_t>(y), coast_page_intervals - 1U);
    const double tx = x - x0, ty = y - y0;
    return (1 - ty) * ((1 - tx) * signed_water_m(x0, y0) + tx * signed_water_m(x0 + 1U, y0)) +
           ty * ((1 - tx) * signed_water_m(x0, y0 + 1U) + tx * signed_water_m(x0 + 1U, y0 + 1U));
}

} // namespace planetsim::presentation
