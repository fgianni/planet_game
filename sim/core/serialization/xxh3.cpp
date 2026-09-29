#include "sim/core/serialization/xxh3.hpp"

#include <array>

namespace planetsim {
namespace {

// Constants and structure follow the xxHash 0.8 reference (xxhash.h), whose
// scalar code paths this reproduces for 64-bit output, seed 0 and the
// default secret.
constexpr std::uint64_t prime32_1 = 0x9E3779B1U;
constexpr std::uint64_t prime32_2 = 0x85EBCA77U;
constexpr std::uint64_t prime32_3 = 0xC2B2AE3DU;
constexpr std::uint64_t prime64_1 = 0x9E3779B185EBCA87ULL;
constexpr std::uint64_t prime64_2 = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t prime64_3 = 0x165667B19E3779F9ULL;
constexpr std::uint64_t prime64_4 = 0x85EBCA77C2B2AE63ULL;
constexpr std::uint64_t prime64_5 = 0x27D4EB2F165667C5ULL;
constexpr std::uint64_t prime_mx1 = 0x165667919E3779F9ULL;
constexpr std::uint64_t prime_mx2 = 0x9FB21C651E98DF25ULL;

constexpr std::size_t secret_size = 192U;
constexpr std::size_t secret_size_min = 136U;
constexpr std::size_t stripe_length = 64U;
constexpr std::size_t secret_consume_rate = 8U;
constexpr std::size_t accumulator_count = 8U;
constexpr std::size_t midsize_max = 240U;
constexpr std::size_t midsize_start_offset = 3U;
constexpr std::size_t midsize_last_offset = 17U;
constexpr std::size_t secret_last_accumulator_start = 7U;
constexpr std::size_t secret_merge_accumulators_start = 11U;

constexpr std::array<std::uint8_t, secret_size> default_secret{
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

[[nodiscard]] std::uint64_t read64(const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8U) | bytes[index];
    }
    return value;
}

[[nodiscard]] std::uint32_t read32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint64_t secret64(std::size_t offset) noexcept {
    return read64(default_secret.data() + offset);
}

[[nodiscard]] std::uint64_t rotl64(std::uint64_t value, unsigned shift) noexcept {
    return (value << shift) | (value >> (64U - shift));
}

[[nodiscard]] std::uint64_t swap64(std::uint64_t value) noexcept {
    std::uint64_t swapped = 0;
    for (int byte = 0; byte < 8; ++byte) {
        swapped = (swapped << 8U) | ((value >> (8U * static_cast<unsigned>(byte))) & 0xFFU);
    }
    return swapped;
}

// The 128-bit product of two 64-bit values, folded by XOR of its halves.
[[nodiscard]] std::uint64_t mul128_fold64(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    const std::uint64_t lo_lo = (lhs & 0xFFFF'FFFFU) * (rhs & 0xFFFF'FFFFU);
    const std::uint64_t hi_lo = (lhs >> 32U) * (rhs & 0xFFFF'FFFFU);
    const std::uint64_t lo_hi = (lhs & 0xFFFF'FFFFU) * (rhs >> 32U);
    const std::uint64_t hi_hi = (lhs >> 32U) * (rhs >> 32U);
    const std::uint64_t cross = (lo_lo >> 32U) + (hi_lo & 0xFFFF'FFFFU) + lo_hi;
    const std::uint64_t upper = (hi_lo >> 32U) + (cross >> 32U) + hi_hi;
    const std::uint64_t lower = (cross << 32U) | (lo_lo & 0xFFFF'FFFFU);
    return lower ^ upper;
}

[[nodiscard]] std::uint64_t xxh64_avalanche(std::uint64_t hash) noexcept {
    hash ^= hash >> 33U;
    hash *= prime64_2;
    hash ^= hash >> 29U;
    hash *= prime64_3;
    hash ^= hash >> 32U;
    return hash;
}

[[nodiscard]] std::uint64_t avalanche(std::uint64_t hash) noexcept {
    hash ^= hash >> 37U;
    hash *= prime_mx1;
    hash ^= hash >> 32U;
    return hash;
}

[[nodiscard]] std::uint64_t rrmxmx(std::uint64_t hash, std::uint64_t length) noexcept {
    hash ^= rotl64(hash, 49U) ^ rotl64(hash, 24U);
    hash *= prime_mx2;
    hash ^= (hash >> 35U) + length;
    hash *= prime_mx2;
    return hash ^ (hash >> 28U);
}

[[nodiscard]] std::uint64_t length_0_to_16(const std::uint8_t* input, std::size_t length) noexcept {
    if (length > 8U) {
        const std::uint64_t bitflip1 = secret64(24U) ^ secret64(32U);
        const std::uint64_t bitflip2 = secret64(40U) ^ secret64(48U);
        const std::uint64_t input_lo = read64(input) ^ bitflip1;
        const std::uint64_t input_hi = read64(input + length - 8U) ^ bitflip2;
        const std::uint64_t accumulator =
            length + swap64(input_lo) + input_hi + mul128_fold64(input_lo, input_hi);
        return avalanche(accumulator);
    }
    if (length >= 4U) {
        const std::uint64_t input1 = read32(input);
        const std::uint64_t input2 = read32(input + length - 4U);
        const std::uint64_t bitflip = secret64(8U) ^ secret64(16U);
        const std::uint64_t input64 = input2 + (input1 << 32U);
        return rrmxmx(input64 ^ bitflip, length);
    }
    if (length > 0U) {
        const std::uint32_t combined = (static_cast<std::uint32_t>(input[0]) << 16U) |
                                       (static_cast<std::uint32_t>(input[length >> 1U]) << 24U) |
                                       static_cast<std::uint32_t>(input[length - 1U]) |
                                       (static_cast<std::uint32_t>(length) << 8U);
        const std::uint64_t bitflip =
            read32(default_secret.data()) ^ read32(default_secret.data() + 4U);
        return xxh64_avalanche(static_cast<std::uint64_t>(combined) ^ bitflip);
    }
    return xxh64_avalanche(secret64(56U) ^ secret64(64U));
}

[[nodiscard]] std::uint64_t mix16(const std::uint8_t* input, std::size_t secret_offset) noexcept {
    return mul128_fold64(read64(input) ^ secret64(secret_offset),
                         read64(input + 8U) ^ secret64(secret_offset + 8U));
}

[[nodiscard]] std::uint64_t length_17_to_128(const std::uint8_t* input,
                                             std::size_t length) noexcept {
    std::uint64_t accumulator = length * prime64_1;
    if (length > 32U) {
        if (length > 64U) {
            if (length > 96U) {
                accumulator += mix16(input + 48U, 96U);
                accumulator += mix16(input + length - 64U, 112U);
            }
            accumulator += mix16(input + 32U, 64U);
            accumulator += mix16(input + length - 48U, 80U);
        }
        accumulator += mix16(input + 16U, 32U);
        accumulator += mix16(input + length - 32U, 48U);
    }
    accumulator += mix16(input, 0U);
    accumulator += mix16(input + length - 16U, 16U);
    return avalanche(accumulator);
}

[[nodiscard]] std::uint64_t length_129_to_240(const std::uint8_t* input,
                                              std::size_t length) noexcept {
    std::uint64_t accumulator = length * prime64_1;
    const std::size_t rounds = length / 16U;
    for (std::size_t round = 0; round < 8U; ++round) {
        accumulator += mix16(input + 16U * round, 16U * round);
    }
    std::uint64_t accumulator_end =
        mix16(input + length - 16U, secret_size_min - midsize_last_offset);
    accumulator = avalanche(accumulator);
    for (std::size_t round = 8U; round < rounds; ++round) {
        accumulator_end += mix16(input + 16U * round, 16U * (round - 8U) + midsize_start_offset);
    }
    return avalanche(accumulator + accumulator_end);
}

using Accumulators = std::array<std::uint64_t, accumulator_count>;

void accumulate_stripe(Accumulators& accumulators, const std::uint8_t* input,
                       std::size_t secret_offset) noexcept {
    for (std::size_t lane = 0; lane < accumulator_count; ++lane) {
        const std::uint64_t data = read64(input + 8U * lane);
        const std::uint64_t key = data ^ secret64(secret_offset + 8U * lane);
        accumulators[lane ^ 1U] += data;
        accumulators[lane] += (key & 0xFFFF'FFFFU) * (key >> 32U);
    }
}

void accumulate(Accumulators& accumulators, const std::uint8_t* input,
                std::size_t stripes) noexcept {
    for (std::size_t stripe = 0; stripe < stripes; ++stripe) {
        accumulate_stripe(accumulators, input + stripe * stripe_length,
                          stripe * secret_consume_rate);
    }
}

void scramble(Accumulators& accumulators) noexcept {
    for (std::size_t lane = 0; lane < accumulator_count; ++lane) {
        std::uint64_t value = accumulators[lane];
        value ^= value >> 47U;
        value ^= secret64(secret_size - stripe_length + 8U * lane);
        value *= prime32_1;
        accumulators[lane] = value;
    }
}

[[nodiscard]] std::uint64_t hash_long(const std::uint8_t* input, std::size_t length) noexcept {
    Accumulators accumulators{prime32_3, prime64_1, prime64_2, prime64_3,
                              prime64_4, prime32_2, prime64_5, prime32_1};
    const std::size_t stripes_per_block = (secret_size - stripe_length) / secret_consume_rate;
    const std::size_t block_length = stripe_length * stripes_per_block;
    const std::size_t blocks = (length - 1U) / block_length;
    for (std::size_t block = 0; block < blocks; ++block) {
        accumulate(accumulators, input + block * block_length, stripes_per_block);
        scramble(accumulators);
    }
    const std::size_t stripes = ((length - 1U) - block_length * blocks) / stripe_length;
    accumulate(accumulators, input + blocks * block_length, stripes);
    accumulate_stripe(accumulators, input + length - stripe_length,
                      secret_size - stripe_length - secret_last_accumulator_start);

    std::uint64_t result = length * prime64_1;
    for (std::size_t pair = 0; pair < 4U; ++pair) {
        const std::size_t offset = secret_merge_accumulators_start + 16U * pair;
        result += mul128_fold64(accumulators[2U * pair] ^ secret64(offset),
                                accumulators[2U * pair + 1U] ^ secret64(offset + 8U));
    }
    return avalanche(result);
}

}  // namespace

std::uint64_t xxh3_64(std::span<const std::byte> bytes) noexcept {
    const auto* input = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t length = bytes.size();
    if (length <= 16U) {
        return length_0_to_16(input, length);
    }
    if (length <= 128U) {
        return length_17_to_128(input, length);
    }
    if (length <= midsize_max) {
        return length_129_to_240(input, length);
    }
    return hash_long(input, length);
}

}  // namespace planetsim
