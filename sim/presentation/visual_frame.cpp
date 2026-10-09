#include "sim/presentation/visual_frame.hpp"

#include "sim/planet/surface/cover_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace planetsim::presentation {
namespace {

[[nodiscard]] std::size_t index(ChannelId id) {
    return static_cast<std::size_t>(id) - 1U;
}

void require_size(const std::vector<float>& values, std::size_t cells, const char* name) {
    if (values.size() != cells) {
        throw std::invalid_argument(std::string(name) + " must have one value per cell");
    }
}

}  // namespace

PresentationReference make_presentation_reference(std::span<const StateSnapshot> frames) {
    if (frames.empty()) {
        throw std::invalid_argument("presentation reference requires at least one frame");
    }
    const std::size_t cells = frames.front().top_of_atmosphere_insolation_W_m2.size();
    if (cells == 0U) {
        throw std::invalid_argument("presentation reference requires cell insolation");
    }
    PresentationReference reference;
    reference.annual_maximum_insolation_W_m2.assign(cells, 0.0F);
    for (const StateSnapshot& frame : frames) {
        require_size(frame.top_of_atmosphere_insolation_W_m2, cells, "frame insolation");
        for (std::size_t cell = 0; cell < cells; ++cell) {
            reference.annual_maximum_insolation_W_m2[cell] = std::max(
                reference.annual_maximum_insolation_W_m2[cell],
                frame.top_of_atmosphere_insolation_W_m2[cell]);
        }
        if (!frame.climatology_surface_temperature_mean_K.empty()) {
            require_size(frame.climatology_surface_temperature_mean_K, cells, "climatology mean");
            require_size(frame.climatology_surface_temperature_variance_K2, cells,
                         "climatology variance");
            reference.climatology_surface_temperature_mean_K =
                frame.climatology_surface_temperature_mean_K;
            reference.climatology_surface_temperature_variance_K2 =
                frame.climatology_surface_temperature_variance_K2;
        }
    }
    return reference;
}

VisualFrame make_visual_frame(const StateSnapshot& snapshot, const TerrainSnapshot& terrain,
                              const PresentationReference& reference) {
    const std::size_t cells = terrain.mean_elevation_m.size();
    require_size(terrain.land_fraction, cells, "terrain land fraction");
    require_size(snapshot.top_of_atmosphere_insolation_W_m2, cells, "snapshot insolation");
    require_size(reference.annual_maximum_insolation_W_m2, cells, "annual maximum insolation");

    VisualFrame frame;
    frame.tick = snapshot.simulation_tick;
    auto& relief = frame.channels[index(ChannelId::relief)].values;
    auto& surface_class = frame.channels[index(ChannelId::surface_class)].values;
    auto& daylight = frame.channels[index(ChannelId::daylight)].values;
    relief.resize(cells);
    surface_class.resize(cells * surface_class_weight_count);
    daylight.resize(cells);

    const bool snow = snapshot.land_snow_water_equivalent_kg_m2.size() == cells;
    const bool sea_ice = snapshot.sea_ice_mass_kg_m2.size() == cells;
    const bool atmosphere = snapshot.sea_level_pressure_Pa.size() == cells;
    const bool wind = snapshot.surface_eastward_wind_m_s.size() == cells &&
                      snapshot.surface_northward_wind_m_s.size() == cells;
    if (snow) {
        frame.channels[index(ChannelId::snow_cover)].values.resize(cells);
    }
    if (sea_ice) {
        frame.channels[index(ChannelId::sea_ice)].values.resize(cells);
    }
    if (atmosphere) {
        frame.channels[index(ChannelId::atmosphere_density)].values.resize(cells);
    }
    if (wind) {
        frame.channels[index(ChannelId::wind)].values.resize(cells * wind_vector_component_count);
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const float height = terrain.mean_elevation_m[cell] - static_cast<float>(terrain.sea_level_m);
        const float land = std::clamp(terrain.land_fraction[cell], 0.0F, 1.0F);
        const float water = 1.0F - land;
        const float snow_cover = snow ? static_cast<float>(snow_cover_fraction(
            snapshot.land_snow_water_equivalent_kg_m2[cell])) : 0.0F;
        const float ice_cover = sea_ice ? static_cast<float>(sea_ice_cover_fraction(
            snapshot.sea_ice_mass_kg_m2[cell])) : 0.0F;
        relief[cell] = height;
        daylight[cell] = reference.annual_maximum_insolation_W_m2[cell] > 0.0F
            ? std::clamp(snapshot.top_of_atmosphere_insolation_W_m2[cell] /
                         reference.annual_maximum_insolation_W_m2[cell], 0.0F, 1.0F)
            : 0.0F;
        if (snow) frame.channels[index(ChannelId::snow_cover)].values[cell] = snow_cover;
        if (sea_ice) frame.channels[index(ChannelId::sea_ice)].values[cell] = ice_cover;
        if (atmosphere) {
            frame.channels[index(ChannelId::atmosphere_density)].values[cell] = std::clamp(
                snapshot.sea_level_pressure_Pa[cell] / atmosphere_density_reference_pressure_Pa,
                0.0F, 1.0F);
        }
        if (wind) {
            const std::size_t wind_offset = cell * wind_vector_component_count;
            frame.channels[index(ChannelId::wind)].values[wind_offset] =
                snapshot.surface_eastward_wind_m_s[cell];
            frame.channels[index(ChannelId::wind)].values[wind_offset + 1U] =
                snapshot.surface_northward_wind_m_s[cell];
        }
        const float bare_land = land * (1.0F - snow_cover);
        float rock = 0.0F;
        float sand = 0.0F;
        float soil = 0.0F;
        if (height >= surface_class_rock_min_relief_m) rock = bare_land;
        else if (height <= surface_class_sand_max_relief_m) sand = bare_land;
        else soil = bare_land;
        const std::size_t offset = cell * surface_class_weight_count;
        surface_class[offset] = water * (1.0F - ice_cover);
        surface_class[offset + 1U] = rock;
        surface_class[offset + 2U] = sand;
        surface_class[offset + 3U] = soil;
        surface_class[offset + 4U] = land * snow_cover + water * ice_cover;
    }
    if (snapshot.surface_temperature_K.size() == cells &&
        reference.climatology_surface_temperature_mean_K.size() == cells &&
        reference.climatology_surface_temperature_variance_K2.size() == cells) {
        auto& anomaly = frame.channels[index(ChannelId::temperature_anomaly)].values;
        anomaly.resize(cells);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const float sigma = std::sqrt(std::max(0.0F,
                reference.climatology_surface_temperature_variance_K2[cell]));
            anomaly[cell] = sigma > 0.0F ? std::clamp((snapshot.surface_temperature_K[cell] -
                reference.climatology_surface_temperature_mean_K[cell]) / (3.0F * sigma),
                -1.0F, 1.0F) : 0.0F;
        }
    }
    for (const auto id : {ChannelId::known, ChannelId::guessed, ChannelId::knowledge_age,
                          ChannelId::observation_uncertainty, ChannelId::estimate_uncertainty}) {
        frame.channels[index(id)].values.assign(cells, id == ChannelId::known ? 1.0F : 0.0F);
    }
    return frame;
}

}  // namespace planetsim::presentation
