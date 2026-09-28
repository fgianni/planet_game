#include "sim/core/scheduler/deterministic_executor.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

struct SumBlock {
    float value = 0.0F;
};

}  // namespace

int main() {
    planetsim::test::Context test;

    constexpr std::array blocks{
        SumBlock{1.0e20F}, SumBlock{1.0F}, SumBlock{-1.0e20F}, SumBlock{3.0F},
        SumBlock{-2.0F}, SumBlock{8.0F}, SumBlock{0.25F}, SumBlock{-0.5F},
        SumBlock{16.0F}, SumBlock{-4.0F}, SumBlock{0.125F}, SumBlock{-0.25F},
        SumBlock{32.0F}, SumBlock{-8.0F}, SumBlock{0.0625F}, SumBlock{-0.125F},
    };
    const auto block_value = [](std::size_t, const SumBlock& block) { return block.value; };
    const auto add = [](float accumulated, const float& next) { return accumulated + next; };

    float serial_fold = 0.0F;
    for (const auto& block : blocks) {
        serial_fold += block.value;
    }
    const auto expected_bits = std::bit_cast<std::uint32_t>(serial_fold);
    for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
        const float reduced = planetsim::reduce_deterministic_blocks<float>(
            std::span<const SumBlock>{blocks}, workers, 0.0F, block_value, add);
        PLANETSIM_EXPECT(test, std::bit_cast<std::uint32_t>(reduced) == expected_bits);
    }

    constexpr float identity = 7.25F;
    const float empty_result = planetsim::reduce_deterministic_blocks<float>(
        std::span<const SumBlock>{}, 8U, identity, block_value, add);
    PLANETSIM_EXPECT(test, std::bit_cast<std::uint32_t>(empty_result) ==
                               std::bit_cast<std::uint32_t>(identity));

    return test.result();
}
