#include "sim/core/serialization/crc32c.hpp"

#include <array>

namespace planetsim {
namespace {

constexpr std::uint32_t reflected_castagnoli_polynomial = 0x82F63B78U;

consteval std::array<std::uint32_t, 256> make_crc32c_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        std::uint32_t remainder = index;
        for (std::uint32_t bit = 0; bit < 8U; ++bit) {
            remainder = (remainder >> 1U) ^
                        ((remainder & 1U) != 0U ? reflected_castagnoli_polynomial : 0U);
        }
        table[index] = remainder;
    }
    return table;
}

inline constexpr auto crc32c_table = make_crc32c_table();

}  // namespace

std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept {
    std::uint32_t remainder = 0xFFFF'FFFFU;
    for (const std::byte byte : bytes) {
        const auto table_index =
            static_cast<std::uint8_t>(remainder ^ static_cast<std::uint8_t>(byte));
        remainder = crc32c_table[table_index] ^ (remainder >> 8U);
    }
    return ~remainder;
}

}  // namespace planetsim
