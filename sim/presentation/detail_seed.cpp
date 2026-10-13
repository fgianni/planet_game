#include "sim/presentation/detail_seed.hpp"

#include "sim/core/random/counter_rng.hpp"

namespace planetsim::presentation {

std::array<double, 3> detail_noise_offset(std::uint64_t world_seed, DetailKind kind) noexcept {
    std::array<double, 3> offset{};
    for (std::uint32_t axis = 0; axis < offset.size(); ++axis) {
        offset[axis] = detail_noise_offset_range *
                       keyed_random_unit_double(world_seed, RandomStreamId::presentation_detail, 0,
                                                static_cast<std::uint32_t>(kind), axis);
    }
    return offset;
}

}  // namespace planetsim::presentation
