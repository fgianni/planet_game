#pragma once

#include "sim/core/serialization/state_snapshot.hpp"

#include <filesystem>
#include <vector>

namespace planetsim::presentation {

// Non-authoritative frame sidecar for playback. It is deliberately distinct
// from PSNAP and is never read by a solver or included in state hashes.
void write_presentation_record(const std::filesystem::path& path,
                               const std::vector<StateSnapshot>& frames);
[[nodiscard]] std::vector<StateSnapshot> read_presentation_record(const std::filesystem::path& path);

}  // namespace planetsim::presentation
