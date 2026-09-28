#pragma once

#include "sim/core/fields/field_registry.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace planetsim {

class PlanetState;

inline constexpr std::uint32_t persistent_snapshot_schema_version = 1U;

struct SnapshotFieldInfo {
    std::uint32_t field_id = 0;
    std::string name;
    std::string partition;
    std::string layout;
    std::string dtype;
    std::uint32_t layers = 0;
    std::string compression;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_length = 0;
    std::uint32_t checksum = 0;
};

struct SnapshotManifest {
    std::string format;
    std::uint32_t schema_version = 0;
    std::string engine_version;
    SimulationTick tick = 0;
    std::uint32_t mesh_level = 0;
    std::uint64_t cell_count = 0;
    std::string parent_snapshot_id;
    std::vector<SnapshotFieldInfo> fields;
};

void write_snapshot(const std::filesystem::path& path,
                    const PlanetState& state,
                    SimulationTick tick,
                    std::string parent_snapshot_id = {});

[[nodiscard]] SnapshotManifest inspect_snapshot(const std::filesystem::path& path);
[[nodiscard]] SnapshotManifest read_snapshot(const std::filesystem::path& path,
                                             PlanetState& target_state);

}  // namespace planetsim
