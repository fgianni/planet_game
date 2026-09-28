#include "sim/planet/planet_state.hpp"

#include <stdexcept>
#include <utility>

namespace planetsim {

PlanetState::PlanetState(std::shared_ptr<const PlanetMesh> mesh) : mesh_(std::move(mesh)) {
    if (!mesh_) {
        throw std::invalid_argument("PlanetState requires an immutable mesh");
    }
    slow_.hypsometry_m =
        Field3D<float>(hypsometry_layer_count, mesh_->cell_count(), 0.0F);
    forcing_.top_of_atmosphere_insolation_W_m2 = Field2D<float>(mesh_->cell_count(), 0.0F);
}

FastState& PlanetState::open_fast_state() {
    if (!fast_state_) {
        fast_state_ = std::make_unique<FastState>();
    }
    return *fast_state_;
}

void PlanetState::release_fast_state() noexcept { fast_state_.reset(); }

}  // namespace planetsim
