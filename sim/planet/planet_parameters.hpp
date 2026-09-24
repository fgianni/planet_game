#pragma once

#include <cstdint>

namespace planetsim {

struct PlanetParameters {
    double radius_m = 6'371'000.0;
    std::uint32_t mesh_subdivision = 6;

    [[nodiscard]] static PlanetParameters earth_reference() noexcept;
    void validate() const;
};

}  // namespace planetsim
