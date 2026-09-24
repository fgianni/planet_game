#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <memory>

namespace planetsim {

struct SurfaceState {
    Field<double> debug_scalar;
};

class PlanetState {
public:
    explicit PlanetState(std::shared_ptr<const PlanetMesh> mesh,
                         double initial_debug_scalar = 0.0);

    [[nodiscard]] const PlanetMesh& mesh() const noexcept { return *mesh_; }
    [[nodiscard]] const std::shared_ptr<const PlanetMesh>& mesh_handle() const noexcept {
        return mesh_;
    }
    [[nodiscard]] SurfaceState& surface() noexcept { return surface_; }
    [[nodiscard]] const SurfaceState& surface() const noexcept { return surface_; }

private:
    std::shared_ptr<const PlanetMesh> mesh_;
    SurfaceState surface_;
};

}  // namespace planetsim
