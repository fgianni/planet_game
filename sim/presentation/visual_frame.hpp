#pragma once

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"
#include "sim/presentation/channel_registry.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace planetsim::presentation {

inline constexpr std::size_t surface_class_weight_count = 5U;
inline constexpr std::size_t wind_vector_component_count = 2U;
inline constexpr float surface_class_sand_max_relief_m = 200.0F;
inline constexpr float surface_class_rock_min_relief_m = 1'500.0F;
// Surface pressure is proportional to atmospheric column mass at fixed
// gravity, which is the density relevant to the R3 scattering channel.
inline constexpr float atmosphere_density_reference_pressure_Pa = 101'325.0F;

struct ChannelData {
    std::vector<float> values;
};

struct VisualEvent {
    // Events are deliberately unpopulated in R1 (ADR-0018 §4.1).
    std::uint32_t kind = 0U;
    std::uint32_t cell = 0U;
    float magnitude = 0.0F;
    SimulationTick begin_tick = 0;
    SimulationTick end_tick = 0;
};

struct VisualFrame {
    std::uint32_t channel_set_version = presentation::channel_set_version;
    SimulationTick tick = 0;
    std::array<ChannelData, channel_count> channels;
    std::vector<VisualEvent> events;
};

// Built from one complete recorded orbital year. The annual maximum is the
// maximum of the sampled TOA insolation frames; this keeps presentation on
// the snapshot boundary and does not require mesh or solver access.
struct PresentationReference {
    std::vector<float> annual_maximum_insolation_W_m2;
    std::vector<float> climatology_surface_temperature_mean_K;
    std::vector<float> climatology_surface_temperature_variance_K2;
};

[[nodiscard]] PresentationReference make_presentation_reference(
    std::span<const StateSnapshot> orbital_year_frames);
[[nodiscard]] VisualFrame make_visual_frame(const StateSnapshot& snapshot,
                                             const TerrainSnapshot& terrain,
                                             const PresentationReference& reference);

}  // namespace planetsim::presentation
