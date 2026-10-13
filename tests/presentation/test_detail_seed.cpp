#include "sim/presentation/detail_seed.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cstddef>

namespace {

using planetsim::presentation::DetailKind;
using planetsim::presentation::detail_kinds;
using planetsim::presentation::detail_noise_offset;
using planetsim::presentation::detail_noise_offset_range;

// Specification §31.5: the same place always looks the same, and detail
// differs between worlds and between kinds.
void check_detail_offsets(planetsim::test::Context& test) {
    constexpr std::uint64_t seed = 20260928U;
    for (const DetailKind kind : detail_kinds) {
        const auto offset = detail_noise_offset(seed, kind);
        PLANETSIM_EXPECT(test, offset == detail_noise_offset(seed, kind));
        PLANETSIM_EXPECT(test, offset != detail_noise_offset(seed + 1U, kind));
        for (const double value : offset) {
            PLANETSIM_EXPECT(test, value >= 0.0 && value < detail_noise_offset_range);
        }
    }
    for (std::size_t a = 0; a < detail_kinds.size(); ++a) {
        for (std::size_t b = a + 1U; b < detail_kinds.size(); ++b) {
            PLANETSIM_EXPECT(test, detail_noise_offset(seed, detail_kinds[a]) !=
                                       detail_noise_offset(seed, detail_kinds[b]));
        }
    }
    // Frozen values: a change here changes how every saved world looks.
    const std::array<double, 3> frozen{398.85923034268887, 26.934878912663919, 93.90623240048069};
    PLANETSIM_EXPECT(test, detail_noise_offset(seed, DetailKind::coast) == frozen);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_detail_offsets(test);
    return test.result();
}
