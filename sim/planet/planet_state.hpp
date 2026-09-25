#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/orbit/orbit_state.hpp"

#include <memory>

namespace planetsim {

struct ForcingState {
    OrbitState orbit;
    double incident_solar_flux_W_m2 = 0.0;
    Field2D<float> top_of_atmosphere_insolation_W_m2;
};

class PlanetState {
  public:
    explicit PlanetState(std::shared_ptr<const PlanetMesh> mesh);

    [[nodiscard]] const PlanetMesh& mesh() const noexcept { return *mesh_; }
    [[nodiscard]] const std::shared_ptr<const PlanetMesh>& mesh_handle() const noexcept {
        return mesh_;
    }
    [[nodiscard]] ForcingState& forcing() noexcept { return forcing_; }
    [[nodiscard]] const ForcingState& forcing() const noexcept { return forcing_; }

  private:
    std::shared_ptr<const PlanetMesh> mesh_;
    ForcingState forcing_;
};

}  // namespace planetsim
