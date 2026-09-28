#pragma once

#include "sim/planet/field_factory.hpp"
#include "sim/planet/orbit/orbit_state.hpp"

#include <cstddef>
#include <memory>

namespace planetsim {

struct SlowState {
    field_container_t<FieldId::hypsometry_m> hypsometry_m;
    field_container_t<FieldId::sea_level_m> sea_level_m = 0.0;
};

struct FastState {};

struct Climatology {};

struct ForcingState {
    OrbitState orbit;
    double incident_solar_flux_W_m2 = 0.0;
    field_container_t<FieldId::top_of_atmosphere_insolation_W_m2>
        top_of_atmosphere_insolation_W_m2;
};

class PlanetState {
  public:
    explicit PlanetState(std::shared_ptr<const PlanetMesh> mesh);

    [[nodiscard]] const PlanetMesh& mesh() const noexcept { return *mesh_; }
    [[nodiscard]] const std::shared_ptr<const PlanetMesh>& mesh_handle() const noexcept {
        return mesh_;
    }

    [[nodiscard]] SlowState& slow() noexcept { return slow_; }
    [[nodiscard]] const SlowState& slow() const noexcept { return slow_; }

    [[nodiscard]] bool has_fast_state() const noexcept {
        return static_cast<bool>(fast_state_);
    }
    [[nodiscard]] FastState* fast_state() noexcept { return fast_state_.get(); }
    [[nodiscard]] const FastState* fast_state() const noexcept { return fast_state_.get(); }
    FastState& open_fast_state();
    void release_fast_state() noexcept;

    [[nodiscard]] Climatology& climatology() noexcept { return climatology_; }
    [[nodiscard]] const Climatology& climatology() const noexcept { return climatology_; }

    [[nodiscard]] ForcingState& forcing() noexcept { return forcing_; }
    [[nodiscard]] const ForcingState& forcing() const noexcept { return forcing_; }

  private:
    std::shared_ptr<const PlanetMesh> mesh_;
    SlowState slow_;
    std::unique_ptr<FastState> fast_state_;
    Climatology climatology_;
    ForcingState forcing_;
};

}  // namespace planetsim
