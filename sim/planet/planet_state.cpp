#include "sim/planet/planet_state.hpp"

#include <stdexcept>
#include <utility>

namespace planetsim {

PlanetState::PlanetState(std::shared_ptr<const PlanetMesh> mesh) : mesh_(std::move(mesh)) {
    if (!mesh_) {
        throw std::invalid_argument("PlanetState requires an immutable mesh");
    }
    forcing_.top_of_atmosphere_insolation_W_m2 = Field<double>(mesh_->cell_count(), 0.0);
}

}  // namespace planetsim
