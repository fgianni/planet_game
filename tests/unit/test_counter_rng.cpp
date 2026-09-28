#include "sim/core/random/counter_rng.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace {

struct GoldenVector {
    std::uint64_t world_seed;
    planetsim::RandomStreamId stream;
    planetsim::SimulationTick tick;
    std::uint32_t cell_key;
    std::uint32_t sample_index;
    std::uint64_t mixed_seed;
    std::uint64_t random_u64;
    std::uint64_t unit_double_bits;
};

// These literals are part of the replay contract. Changing them invalidates
// recorded runs, golden snapshots, and replay, and therefore requires an ADR.
constexpr std::array<GoldenVector, 8> golden_vectors{{
    {0x0000'0000'0000'0000ULL, planetsim::RandomStreamId::validation, 0,
     0x0000'0000U, 0x0000'0000U, 0xE220'A839'7B1D'CDAFULL,
     0xF9AC'6E33'7C2F'0F1EULL, 0x3FEF'358D'C66F'85E1ULL},
    {0xFFFF'FFFF'FFFF'FFFFULL, planetsim::RandomStreamId::weather, 1,
     0x0000'0001U, 0x0000'0001U, 0xE4D9'7177'1B65'2C20ULL,
     0x6855'357F'5CA0'ACE7ULL, 0x3FDA'154D'5FD7'282AULL},
    {0x0000'0000'0000'0001ULL, planetsim::RandomStreamId::hydrology, -1,
     0xFFFF'FFFFU, 0xFFFF'FFFFU, 0x910A'2DEC'8902'5CC1ULL,
     0x997F'76C3'6C15'318EULL, 0x3FE3'2FEE'D86D'82A6ULL},
    {0x0123'4567'89AB'CDEFULL, planetsim::RandomStreamId::validation,
     std::numeric_limits<planetsim::SimulationTick>::max(), 0x8000'0000U,
     0x7FFF'FFFFU, 0x157A'3807'A48F'AA9DULL, 0x9308'C34A'9CA3'4D9AULL,
     0x3FE2'6118'6953'9469ULL},
    {0xFEDC'BA98'7654'3210ULL, planetsim::RandomStreamId::weather,
     std::numeric_limits<planetsim::SimulationTick>::min(), 0x0000'002AU,
     0x0000'0000U, 0x7AE8'93B5'E32F'EE86ULL, 0xBD4F'DBA4'8876'9EA3ULL,
     0x3FE7'A9FB'7491'0ED3ULL},
    {0x9B97'F4A7'C150'0011ULL, planetsim::RandomStreamId::hydrology, 123'456,
     0x0000'007BU, 0x0000'0008U, 0x52E3'85A4'1D40'AD4AULL,
     0x04EC'07CD'D7AA'3A1AULL, 0x3F93'B01F'375E'A8E0ULL},
    {0xAAAA'AAAA'AAAA'AAAAULL, planetsim::RandomStreamId::validation, -123'456,
     0xFFFF'FFFFU, 0x0000'0011U, 0x2B02'C6E0'39F1'B1BFULL,
     0xF7C5'31A2'A41F'0BAAULL, 0x3FEE'F8A6'3454'83E1ULL},
    {0x5555'5555'5555'5555ULL, planetsim::RandomStreamId::weather, 987'654'321,
     0x0000'0007U, 0xFFFF'FFFFU, 0x3D23'DCE4'1C58'8F8CULL,
     0x8C3E'575A'07DC'5172ULL, 0x3FE1'87CA'EB40'FB8AULL},
}};

}  // namespace

int main() {
    planetsim::test::Context test;

    for (const auto& vector : golden_vectors) {
        PLANETSIM_EXPECT(test,
                         planetsim::mix_random_key(vector.world_seed) == vector.mixed_seed);
        PLANETSIM_EXPECT(test,
                         planetsim::keyed_random_u64(vector.world_seed, vector.stream, vector.tick,
                                                     vector.cell_key, vector.sample_index) ==
                             vector.random_u64);
        const double unit = planetsim::keyed_random_unit_double(
            vector.world_seed, vector.stream, vector.tick, vector.cell_key, vector.sample_index);
        PLANETSIM_EXPECT(test, std::bit_cast<std::uint64_t>(unit) == vector.unit_double_bits);
    }

    return test.result();
}
