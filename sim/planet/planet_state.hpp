#pragma once

#include "sim/planet/field_factory.hpp"
#include "sim/planet/orbit/orbit_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
    // Cryosphere water reservoirs (ADR-0008 §4.1): snow on the land tile,
    // sea ice on the ocean tile.
    field_container_t<FieldId::land_snow_water_equivalent_kg_m2> land_snow_water_equivalent_kg_m2;
    field_container_t<FieldId::sea_ice_mass_kg_m2> sea_ice_mass_kg_m2;
    // Atmosphere (ADR-0010 §4.1): zero pressure and no layers until
    // initialise_atmosphere, which sets the scenario's layer count.
    field_container_t<FieldId::atmosphere_surface_pressure_Pa> atmosphere_surface_pressure_Pa;
    field_container_t<FieldId::atmosphere_temperature_K> atmosphere_temperature_K;
    // Water (ADR-0021 §4.1): the layers' humidity, and the land's bucket.
    field_container_t<FieldId::atmosphere_specific_humidity_kg_kg>
        atmosphere_specific_humidity_kg_kg;
    field_container_t<FieldId::land_surface_water_kg_m2> land_surface_water_kg_m2;

    [[nodiscard]] std::size_t atmosphere_layer_count() const noexcept {
        return atmosphere_temperature_K.layer_count();
    }
};

// Weather (ADR-0001 §4.1): allocated only in reference mode (or a weather
// window), never needed to rebuild the slow state.
struct FastState {
    // Edge-normal winds per atmospheric layer (ADR-0011 §4.1, §4.3).
    field_container_t<FieldId::atmosphere_edge_normal_wind_m_s> atmosphere_edge_normal_wind_m_s;
};

// Monthly statistics (ADR-0001 §4.1): one layer per sub-step k mod 12,
// derived and never persisted; `samples` counts the climate steps
// accumulated into each month.
struct Climatology {
    field_container_t<FieldId::climatology_surface_temperature_mean_K> surface_temperature_mean_K;
    field_container_t<FieldId::climatology_surface_temperature_variance_K2>
        surface_temperature_variance_K2;
    field_container_t<FieldId::climatology_land_snow_mean_kg_m2> land_snow_mean_kg_m2;
    field_container_t<FieldId::climatology_sea_ice_mean_kg_m2> sea_ice_mean_kg_m2;
    std::array<std::uint32_t, 12> samples{};
    // The circulation's months (ADR-0011 §4.4 step 5), counted apart: a
    // month the circulation did not solve adds nothing to them.
    field_container_t<FieldId::climatology_surface_eastward_wind_mean_m_s> surface_eastward_wind_mean_m_s;
    field_container_t<FieldId::climatology_surface_northward_wind_mean_m_s>
        surface_northward_wind_mean_m_s;
    field_container_t<FieldId::climatology_sea_level_pressure_mean_Pa> sea_level_pressure_mean_Pa;
    std::array<std::uint32_t, 12> circulation_samples{};
};

// The climate mode's circulation on the cells (ADR-0011 §4.1, §4.4 step 5):
// derived, never persisted, and empty (no layers, no cells) until the
// climate circulation has solved a month.
struct CirculationState {
    field_container_t<FieldId::atmosphere_eastward_wind_m_s> eastward_wind_m_s;
    field_container_t<FieldId::atmosphere_northward_wind_m_s> northward_wind_m_s;
    field_container_t<FieldId::atmosphere_vertical_mass_flux_kg_m2_s> vertical_mass_flux_kg_m2_s;
    field_container_t<FieldId::sea_level_pressure_Pa> sea_level_pressure_Pa;
    field_container_t<FieldId::surface_wind_stress_east_N_m2> surface_stress_east_N_m2;
    field_container_t<FieldId::surface_wind_stress_north_N_m2> surface_stress_north_N_m2;
    field_container_t<FieldId::atmosphere_balanced_surface_pressure_Pa> balanced_surface_pressure_Pa;

    [[nodiscard]] bool available() const noexcept { return !balanced_surface_pressure_Pa.empty(); }
};

struct ForcingState {
    OrbitState orbit;
    double incident_solar_flux_W_m2 = 0.0;
    field_container_t<FieldId::top_of_atmosphere_insolation_W_m2>
        top_of_atmosphere_insolation_W_m2;
    // Climate-mode forcing: the time mean over one orbital sub-step (ADR-0006
    // §4.3), written by update_substep_mean_insolation.
    field_container_t<FieldId::substep_mean_insolation_W_m2> substep_mean_insolation_W_m2;
    // ADR-0008 §3.3 B: zero unless a scenario or test sets it; M6 replaces it
    // with model precipitation.
    field_container_t<FieldId::prescribed_precipitation_kg_m2_s> prescribed_precipitation_kg_m2_s;
    // The cells' radiating surface temperature after the last surface step.
    field_container_t<FieldId::surface_temperature_K> surface_temperature_K;
    // The step's mean water fluxes (ADR-0021 §4.1), kg/m²/s of cell.
    field_container_t<FieldId::evaporation_kg_m2_s> evaporation_kg_m2_s;
    field_container_t<FieldId::runoff_kg_m2_s> runoff_kg_m2_s;
    field_container_t<FieldId::precipitation_kg_m2_s> precipitation_kg_m2_s;
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

    [[nodiscard]] CirculationState& circulation() noexcept { return circulation_; }
    [[nodiscard]] const CirculationState& circulation() const noexcept { return circulation_; }

    [[nodiscard]] ForcingState& forcing() noexcept { return forcing_; }
    [[nodiscard]] const ForcingState& forcing() const noexcept { return forcing_; }

  private:
    std::shared_ptr<const PlanetMesh> mesh_;
    SlowState slow_;
    std::unique_ptr<FastState> fast_state_;
    Climatology climatology_;
    CirculationState circulation_;
    ForcingState forcing_;
};

}  // namespace planetsim
