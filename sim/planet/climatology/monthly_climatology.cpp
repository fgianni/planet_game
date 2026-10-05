#include "sim/planet/climatology/monthly_climatology.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/core/scheduler/orbital_calendar.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace planetsim {

namespace {

void zero(Field3D<float>& field) {
    for (std::size_t layer = 0; layer < field.layer_count(); ++layer) {
        const auto values = field.layer(layer);
        std::fill(values.begin(), values.end(), 0.0F);
    }
}

}  // namespace

void reset_climatology(PlanetState& state) {
    auto& climatology = state.climatology();
    zero(climatology.surface_temperature_mean_K);
    zero(climatology.surface_temperature_variance_K2);
    zero(climatology.land_snow_mean_kg_m2);
    zero(climatology.sea_ice_mean_kg_m2);
    climatology.samples.fill(0U);
    zero(climatology.surface_eastward_wind_mean_m_s);
    zero(climatology.surface_northward_wind_mean_m_s);
    zero(climatology.sea_level_pressure_mean_Pa);
    climatology.circulation_samples.fill(0U);
}

void accumulate_climatology(PlanetState& state, std::int64_t substep_index,
                            std::size_t worker_count) {
    if (substep_index < 0) {
        throw std::invalid_argument("climatology needs a non-negative sub-step index");
    }
    auto& climatology = state.climatology();
    const auto month = static_cast<std::size_t>(substep_index % climate_substeps_per_year);
    const auto samples = static_cast<double>(++climatology.samples[month]);
    auto temperature_mean = climatology.surface_temperature_mean_K.layer(month);
    auto temperature_variance = climatology.surface_temperature_variance_K2.layer(month);
    auto snow_mean = climatology.land_snow_mean_kg_m2.layer(month);
    auto ice_mean = climatology.sea_ice_mean_kg_m2.layer(month);
    const auto& temperature = state.forcing().surface_temperature_K;
    const auto& slow = state.slow();
    for_each_deterministic_block(
        state.mesh().blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double value = temperature[cell];
                const double old_mean = temperature_mean[cell];
                const double mean = old_mean + (value - old_mean) / samples;
                // M2 = n σ²: M2' = M2 + (x − mean) (x − mean').
                const double m2 = static_cast<double>(temperature_variance[cell]) *
                                      (samples - 1.0) +
                                  (value - old_mean) * (value - mean);
                temperature_mean[cell] = static_cast<float>(mean);
                temperature_variance[cell] = static_cast<float>(m2 / samples);
                snow_mean[cell] = static_cast<float>(
                    snow_mean[cell] +
                    (slow.land_snow_water_equivalent_kg_m2[cell] - snow_mean[cell]) / samples);
                ice_mean[cell] = static_cast<float>(
                    ice_mean[cell] + (slow.sea_ice_mass_kg_m2[cell] - ice_mean[cell]) / samples);
            }
        });
}

void accumulate_circulation_climatology(PlanetState& state, std::int64_t substep_index,
                                        std::size_t worker_count) {
    if (substep_index < 0) {
        throw std::invalid_argument("climatology needs a non-negative sub-step index");
    }
    const auto& circulation = state.circulation();
    if (!circulation.available()) {
        throw std::invalid_argument("the circulation climatology needs a solved circulation");
    }
    auto& climatology = state.climatology();
    const auto month = static_cast<std::size_t>(substep_index % climate_substeps_per_year);
    const auto samples = static_cast<double>(++climatology.circulation_samples[month]);
    auto east_mean = climatology.surface_eastward_wind_mean_m_s.layer(month);
    auto north_mean = climatology.surface_northward_wind_mean_m_s.layer(month);
    auto pressure_mean = climatology.sea_level_pressure_mean_Pa.layer(month);
    const auto east = circulation.eastward_wind_m_s.layer(0);
    const auto north = circulation.northward_wind_m_s.layer(0);
    const auto& pressure = circulation.sea_level_pressure_Pa;
    for_each_deterministic_block(
        state.mesh().blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                east_mean[cell] = static_cast<float>(
                    east_mean[cell] +
                    (static_cast<double>(east[cell]) - east_mean[cell]) / samples);
                north_mean[cell] = static_cast<float>(
                    north_mean[cell] +
                    (static_cast<double>(north[cell]) - north_mean[cell]) / samples);
                pressure_mean[cell] = static_cast<float>(
                    pressure_mean[cell] +
                    (static_cast<double>(pressure[cell]) - pressure_mean[cell]) / samples);
            }
        });
}

}  // namespace planetsim
