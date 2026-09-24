#include "sim/planet/planet_state.hpp"

#include <stdexcept>
#include <utility>

namespace planetsim {

PlanetState::PlanetState(std::shared_ptr<const PlanetMesh> mesh,
                         double initial_debug_scalar)
    : mesh_(std::move(mesh)) {
    if (!mesh_) {
        throw std::invalid_argument("PlanetState requires an immutable mesh");
    }
    surface_.debug_scalar = Field<double>(mesh_->cell_count(), initial_debug_scalar);
}

}  // namespace planetsim
