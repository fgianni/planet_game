#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace planetsim::presentation {

// Numeric ids are a public, append-only presentation contract (ADR-0018 §4.2).
enum class ChannelId : std::uint16_t {
    relief = 1,
    surface_class = 2,
    daylight = 3,
    snow_cover = 4,
    sea_ice = 5,
    temperature_anomaly = 6,
    atmosphere_density = 7,
    wind = 8,
    cloud_cover = 9,
    cloud_thickness = 10,
    precipitation = 11,
    water_extent = 12,
    vegetation_vigour = 13,
    dryness = 14,
    ocean_tint = 15,
    current = 16,
    haze = 17,
    stratospheric_veil = 18,
    fire = 19,
    urban = 20,
    farmland = 21,
    mining = 22,
    night_lights = 23,
    known = 24,
    guessed = 25,
    knowledge_age = 26,
    observation_uncertainty = 27,
    estimate_uncertainty = 28,
};

inline constexpr std::size_t channel_count = 28U;
inline constexpr std::uint32_t channel_set_version = 28U;

enum class ChannelKind : std::uint8_t { scalar, weights, vector };

struct ChannelDescriptor {
    ChannelId id;
    std::string_view name;
    ChannelKind kind;
    float minimum;
    float maximum;
    std::string_view introducing_track;
    bool required_in_style;
    bool overlay_only;
};

[[nodiscard]] constexpr std::string_view channel_kind_name(ChannelKind kind) noexcept {
    switch (kind) {
    case ChannelKind::scalar: return "scalar";
    case ChannelKind::weights: return "weights";
    case ChannelKind::vector: return "vector";
    }
    return {};
}

inline constexpr std::array<ChannelDescriptor, channel_count> channel_registry{{
    {ChannelId::relief, "relief", ChannelKind::scalar, -1.0e9F, 1.0e9F, "R0", true, false},
    {ChannelId::surface_class, "surface_class", ChannelKind::weights, 0.0F, 1.0F, "R0", true, false},
    {ChannelId::daylight, "daylight", ChannelKind::scalar, 0.0F, 1.0F, "R0", true, false},
    {ChannelId::snow_cover, "snow_cover", ChannelKind::scalar, 0.0F, 1.0F, "R2", true, false},
    {ChannelId::sea_ice, "sea_ice", ChannelKind::scalar, 0.0F, 1.0F, "R2", true, false},
    {ChannelId::temperature_anomaly, "temperature_anomaly", ChannelKind::scalar, -1.0F, 1.0F, "R2", false, true},
    {ChannelId::atmosphere_density, "atmosphere_density", ChannelKind::scalar, 0.0F, 1.0F, "R3", false, false},
    {ChannelId::wind, "wind", ChannelKind::vector, -1.0e9F, 1.0e9F, "R3", false, false},
    {ChannelId::cloud_cover, "cloud_cover", ChannelKind::scalar, 0.0F, 1.0F, "R4", false, false},
    {ChannelId::cloud_thickness, "cloud_thickness", ChannelKind::scalar, 0.0F, 1.0F, "R4", false, false},
    {ChannelId::precipitation, "precipitation", ChannelKind::scalar, 0.0F, 1.0F, "R4", false, false},
    {ChannelId::water_extent, "water_extent", ChannelKind::scalar, 0.0F, 1.0F, "R5", false, false},
    {ChannelId::vegetation_vigour, "vegetation_vigour", ChannelKind::scalar, 0.0F, 1.0F, "R5", false, false},
    {ChannelId::dryness, "dryness", ChannelKind::scalar, 0.0F, 1.0F, "R5", false, false},
    {ChannelId::ocean_tint, "ocean_tint", ChannelKind::weights, 0.0F, 1.0F, "R6", false, false},
    {ChannelId::current, "current", ChannelKind::vector, -1.0e9F, 1.0e9F, "R6", false, false},
    {ChannelId::haze, "haze", ChannelKind::scalar, 0.0F, 1.0F, "R7", false, false},
    {ChannelId::stratospheric_veil, "stratospheric_veil", ChannelKind::scalar, 0.0F, 1.0F, "R7", false, false},
    {ChannelId::fire, "fire", ChannelKind::scalar, 0.0F, 1.0F, "R7", false, false},
    {ChannelId::urban, "urban", ChannelKind::scalar, 0.0F, 1.0F, "R8", false, false},
    {ChannelId::farmland, "farmland", ChannelKind::scalar, 0.0F, 1.0F, "R8", false, false},
    {ChannelId::mining, "mining", ChannelKind::scalar, 0.0F, 1.0F, "R8", false, false},
    {ChannelId::night_lights, "night_lights", ChannelKind::scalar, 0.0F, 1.0F, "R8", false, false},
    {ChannelId::known, "known", ChannelKind::scalar, 0.0F, 1.0F, "R1", true, false},
    {ChannelId::guessed, "guessed", ChannelKind::scalar, 0.0F, 1.0F, "R1", true, false},
    {ChannelId::knowledge_age, "knowledge_age", ChannelKind::scalar, 0.0F, 1.0F, "R1", true, false},
    {ChannelId::observation_uncertainty, "observation_uncertainty", ChannelKind::scalar, 0.0F, 1.0F, "R1", false, true},
    {ChannelId::estimate_uncertainty, "estimate_uncertainty", ChannelKind::scalar, 0.0F, 1.0F, "R1", false, true},
}};

static_assert(static_cast<std::uint16_t>(channel_registry.back().id) == channel_set_version);

}  // namespace planetsim::presentation
