#pragma once

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace planetsim::presentation {

// Non-authoritative sidecar for presentation playback. It is deliberately
// distinct from PSNAP and is never read by a solver or included in state
// hashes. PFRAME02 includes the immutable data needed to make a recorded run
// independently playable; PFRAME01 records remain readable as frame-only
// legacy input.
inline constexpr std::uint32_t presentation_record_schema_version = 2U;

struct PresentationMeshDescriptor {
    std::uint32_t subdivision = 0U;
    double radius_m = 0.0;
};

struct PresentationRecord {
    std::uint32_t schema_version = presentation_record_schema_version;
    std::optional<PresentationMeshDescriptor> mesh;
    std::optional<TerrainSnapshot> terrain;
    std::vector<StateSnapshot> frames;

    [[nodiscard]] bool is_playback_ready() const noexcept {
        return schema_version >= presentation_record_schema_version && mesh.has_value() &&
               terrain.has_value() && !frames.empty();
    }
};

void write_presentation_record(const std::filesystem::path& path,
                               const PresentationRecord& record);
[[nodiscard]] PresentationRecord read_presentation_record(const std::filesystem::path& path);

// Compatibility overload for the PFRAME01 frame-only sidecar. New callers
// that need bridge playback must write a PresentationRecord instead.
void write_presentation_record(const std::filesystem::path& path,
                               const std::vector<StateSnapshot>& frames);

}  // namespace planetsim::presentation
