#include "sim/presentation/coast_page.hpp"

#include <algorithm>
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

CoastPage make_coast_page(const TileKey& key, const CoastWaterField& field) {
    validate(key);
    CoastPage page;
    page.key = key;
    page.signed_water_le.resize(static_cast<std::size_t>(coast_page_nodes) * coast_page_nodes * 2);
    const double lines = static_cast<double>(coast_page_intervals) * (1U << key.level);
    CellId hint{0};
    for (std::uint32_t y = 0; y < coast_page_nodes; ++y) {
        for (std::uint32_t x = 0; x < coast_page_nodes; ++x) {
            // Construct global face coordinates in integer units first;
            // adjacent and parent/child pages obtain bit-identical doubles.
            const double u = -1.0 + 2.0 * (key.x * coast_page_intervals + x) / lines;
            const double v = -1.0 + 2.0 * (key.y * coast_page_intervals + y) / lines;
            const Vec3d direction = tile_face_direction(key.face, u, v);
            const CoastWaterSample water = field.sample(direction, hint);
            const double signed_water = water.signed_water_m;
            if (!std::isfinite(signed_water))
                throw std::runtime_error("non-finite canonical coast sample");
            // Quantization cannot change the sign: keep small nonzero values
            // at +/-1. Zero is dry by the field's strict >0 water rule.
            const auto rounded = static_cast<std::int32_t>(std::llround(
                std::clamp(signed_water, -32767.0, 32767.0)));
            const auto quantized = static_cast<std::int16_t>(signed_water > 0.0 ?
                std::max(1, rounded) : signed_water < 0.0 ? std::min(-1, rounded) : 0);
            const auto bits = static_cast<std::uint16_t>(quantized);
            const std::size_t offset = 2 * (static_cast<std::size_t>(y) * coast_page_nodes + x);
            page.signed_water_le[offset] = static_cast<std::uint8_t>(bits & 0xffU);
            page.signed_water_le[offset + 1] = static_cast<std::uint8_t>(bits >> 8U);
            hint = water.cell;
        }
    }
    return page;
}

double CoastPage::signed_water_m(std::uint32_t x, std::uint32_t y) const {
    if (x >= coast_page_nodes || y >= coast_page_nodes ||
        signed_water_le.size() != static_cast<std::size_t>(coast_page_nodes) * coast_page_nodes * 2)
        throw std::out_of_range("invalid coast page sample");
    const std::size_t offset = 2 * (static_cast<std::size_t>(y) * coast_page_nodes + x);
    const auto bits = static_cast<std::uint16_t>(signed_water_le[offset] |
        (static_cast<std::uint16_t>(signed_water_le[offset + 1]) << 8U));
    return static_cast<double>(bits <= 32767U ? static_cast<std::int32_t>(bits) :
                               static_cast<std::int32_t>(bits) - 65536);
}

double CoastPage::bilinear_signed_water_m(double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
        x > coast_page_intervals || y > coast_page_intervals)
        throw std::out_of_range("invalid coast page coordinates");
    const auto x0 = static_cast<std::uint32_t>(x);
    const auto y0 = static_cast<std::uint32_t>(y);
    const auto x1 = std::min(x0 + 1U, coast_page_intervals);
    const auto y1 = std::min(y0 + 1U, coast_page_intervals);
    const double tx = x - x0, ty = y - y0;
    return (1 - ty) * ((1 - tx) * signed_water_m(x0, y0) + tx * signed_water_m(x1, y0)) +
           ty * ((1 - tx) * signed_water_m(x0, y1) + tx * signed_water_m(x1, y1));
}

} // namespace planetsim::presentation
