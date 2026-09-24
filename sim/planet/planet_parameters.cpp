#include "sim/planet/planet_parameters.hpp"

#include <cmath>
#include <stdexcept>

namespace planetsim {

PlanetParameters PlanetParameters::earth_reference() noexcept {
    return {};
}

void PlanetParameters::validate() const {
    if (!(radius_m > 0.0) || !std::isfinite(radius_m)) {
        throw std::invalid_argument("planet radius must be finite and positive");
    }
}

}  // namespace planetsim
