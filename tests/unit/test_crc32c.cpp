#include "sim/core/serialization/crc32c.hpp"
#include "tests/test_support.hpp"

#include <cstddef>
#include <span>
#include <string_view>

int main() {
    planetsim::test::Context test;

    constexpr std::string_view standard_vector = "123456789";
    const auto bytes = std::as_bytes(std::span{standard_vector.data(), standard_vector.size()});
    PLANETSIM_EXPECT(test, planetsim::crc32c(bytes) == 0xE3069283U);
    PLANETSIM_EXPECT(test, planetsim::crc32c({}) == 0U);

    return test.result();
}
