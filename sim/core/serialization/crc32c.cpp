#include "sim/core/serialization/crc32c.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace planetsim {
namespace {

constexpr std::uint32_t reflected_castagnoli_polynomial = 0x82F63B78U;

// Slicing-by-8 (Kounavis and Berry): table k gives the CRC of a byte
// followed by k zero bytes, so eight input bytes advance the remainder in
// one step. Same checksums as the byte-at-a-time form, several times faster.
consteval std::array<std::array<std::uint32_t, 256>, 8> make_crc32c_tables() {
    std::array<std::array<std::uint32_t, 256>, 8> tables{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint32_t remainder = index;
        for (std::uint32_t bit = 0; bit < 8U; ++bit) {
            remainder = (remainder >> 1U) ^
                        ((remainder & 1U) != 0U ? reflected_castagnoli_polynomial : 0U);
        }
        tables[0][index] = remainder;
    }
    for (std::uint32_t index = 0; index < 256U; ++index) {
        for (std::size_t slice = 1; slice < 8U; ++slice) {
            const std::uint32_t previous = tables[slice - 1U][index];
            tables[slice][index] = (previous >> 8U) ^ tables[0][previous & 0xFFU];
        }
    }
    return tables;
}

inline constexpr auto crc32c_tables = make_crc32c_tables();

}  // namespace

std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept {
    std::uint32_t remainder = 0xFFFF'FFFFU;
    std::size_t index = 0;
    const auto byte_at = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index + offset]));
    };
    for (; index + 8U <= bytes.size(); index += 8U) {
        const std::uint32_t low = remainder ^ (byte_at(0) | (byte_at(1) << 8U) |
                                               (byte_at(2) << 16U) | (byte_at(3) << 24U));
        remainder = crc32c_tables[7][low & 0xFFU] ^ crc32c_tables[6][(low >> 8U) & 0xFFU] ^
                    crc32c_tables[5][(low >> 16U) & 0xFFU] ^ crc32c_tables[4][low >> 24U] ^
                    crc32c_tables[3][byte_at(4)] ^ crc32c_tables[2][byte_at(5)] ^
                    crc32c_tables[1][byte_at(6)] ^ crc32c_tables[0][byte_at(7)];
    }
    for (; index < bytes.size(); ++index) {
        remainder = crc32c_tables[0][(remainder ^ byte_at(0)) & 0xFFU] ^ (remainder >> 8U);
    }
    return ~remainder;
}

}  // namespace planetsim
