#include "sim/planet/planet_state.hpp"

#include <stdexcept>
#include <utility>

namespace planetsim {

PlanetState::PlanetState(std::shared_ptr<const PlanetMesh> mesh) : mesh_(std::move(mesh)) {
    if (!mesh_) {
        throw std::invalid_argument("PlanetState requires an immutable mesh");
    }
    slow_.hypsometry_m = make_field<FieldId::hypsometry_m>(*mesh_);
    slow_.sea_level_m = make_field<FieldId::sea_level_m>(*mesh_);
    slow_.land_surface_temperature_K = make_field<FieldId::land_surface_temperature_K>(*mesh_);
    slow_.land_ground_temperature_K = make_field<FieldId::land_ground_temperature_K>(*mesh_);
    slow_.ocean_mixed_layer_temperature_K =
        make_field<FieldId::ocean_mixed_layer_temperature_K>(*mesh_);
    slow_.ocean_deep_temperature_K = make_field<FieldId::ocean_deep_temperature_K>(*mesh_);
    slow_.land_snow_water_equivalent_kg_m2 =
        make_field<FieldId::land_snow_water_equivalent_kg_m2>(*mesh_);
    slow_.sea_ice_mass_kg_m2 = make_field<FieldId::sea_ice_mass_kg_m2>(*mesh_);
    forcing_.top_of_atmosphere_insolation_W_m2 =
        make_field<FieldId::top_of_atmosphere_insolation_W_m2>(*mesh_);
    forcing_.substep_mean_insolation_W_m2 =
        make_field<FieldId::substep_mean_insolation_W_m2>(*mesh_);
    forcing_.prescribed_precipitation_kg_m2_s =
        make_field<FieldId::prescribed_precipitation_kg_m2_s>(*mesh_);
}

FastState& PlanetState::open_fast_state() {
    if (!fast_state_) {
        fast_state_ = std::make_unique<FastState>();
    }
    return *fast_state_;
}

void PlanetState::release_fast_state() noexcept { fast_state_.reset(); }

}  // namespace planetsim
