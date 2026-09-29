#include "sim/core/serialization/xxh3.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Reference values from the xxHash 0.8 library (python-xxhash 3.5.0,
// xxh3_64_intdigest). The lengths cover every branch: empty, 1-3, 4-8, 9-16,
// 17-128, 129-240, and the long path with one and several 1024-byte blocks.
// Changing any of them changes every recorded state_hash (ADR-0003 §3.3).
int main() {
    planetsim::test::Context test;

    constexpr std::array<std::pair<std::size_t, std::uint64_t>, 15> vectors{{
        {0U, 0x2D06800538D394C2ULL},    {1U, 0x13E608BC156DEFEDULL},
        {3U, 0xA9088DDA485B481CULL},    {4U, 0x6D9253B16C8B1ED3ULL},
        {8U, 0x60539DB630471163ULL},    {9U, 0xFEFF668361D723A8ULL},
        {16U, 0xB8C859B0F030B585ULL},   {17U, 0x714A04408E79B80FULL},
        {128U, 0x67425A03650261BFULL},  {129U, 0xC664BF3311C6ABC4ULL},
        {240U, 0x64556DC6B462A6CFULL},  {241U, 0x8BEADD3A8874FE17ULL},
        {1024U, 0x9B81661C641C72B1ULL}, {1025U, 0x806C2072ED713576ULL},
        {5000U, 0x799AADDD7339581DULL},
    }};
    std::vector<std::byte> bytes(5000U);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>((index * 7U + 3U) & 0xFFU);
    }
    for (const auto& [length, expected] : vectors) {
        PLANETSIM_EXPECT(test, planetsim::xxh3_64({bytes.data(), length}) == expected);
    }

    constexpr std::string_view abc = "abc";
    PLANETSIM_EXPECT(test, planetsim::xxh3_64(std::as_bytes(std::span{abc.data(), abc.size()})) ==
                               0x78AF5F94892F3950ULL);
    return test.result();
}
