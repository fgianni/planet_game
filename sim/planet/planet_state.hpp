#pragma once

#include "sim/planet/field_factory.hpp"
#include "sim/planet/orbit/orbit_state.hpp"

#include <cstddef>
#include <memory>

namespace planetsim {

struct SlowState {
    field_container_t<FieldId::hypsometry_m> hypsometry_m;
    field_container_t<FieldId::sea_level_m> sea_level_m = 0.0;
    // Surface energy columns (ADR-0007 §4.1). Zero until initialised, which
    // the column solver rejects.
    field_container_t<FieldId::land_surface_temperature_K> land_surface_temperature_K;
    field_container_t<FieldId::land_ground_temperature_K> land_ground_temperature_K;
    field_container_t<FieldId::ocean_mixed_layer_temperature_K> ocean_mixed_layer_temperature_K;
    field_container_t<FieldId::ocean_deep_temperature_K> ocean_deep_temperature_K;
};

struct FastState {};

struct Climatology {};

struct ForcingState {
    OrbitState orbit;
    double incident_solar_flux_W_m2 = 0.0;
    field_container_t<FieldId::top_of_atmosphere_insolation_W_m2>
        top_of_atmosphere_insolation_W_m2;
    // Climate-mode forcing: the time mean over one orbital sub-step (ADR-0006
    // §4.3), written by update_substep_mean_insolation.
    field_container_t<FieldId::substep_mean_insolation_W_m2> substep_mean_insolation_W_m2;
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
