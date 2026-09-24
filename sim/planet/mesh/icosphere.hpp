#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>

namespace planetsim {

[[nodiscard]] std::size_t expected_icosphere_cell_count(std::uint32_t subdivision);
[[nodiscard]] std::size_t expected_icosphere_vertex_count(std::uint32_t subdivision);

}  // namespace planetsim
