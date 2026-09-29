#pragma once

#include "sim/core/fields/field_registry.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace planetsim {

class PlanetMesh;
class PlanetState;
struct SlowState;

// Initialisers for slow fields that an older schema lacks (ADR-0003 §3.6:
// missing data is never silently zero). Core code stays domain-agnostic: the
// planet layer supplies the functions. Each receives the target mesh and the
// staged slow state with every field the file did contain already decoded.
struct SnapshotMigration {
    // Schema 1 -> 2: the four surface-energy temperatures, written in the
    // current (schema 3) types.
    std::function<void(const PlanetMesh&, SlowState&)> initialise_schema_2_fields;
};

// Schema 1 (M2): hypsometry and sea level. Schema 2 (M3, ADR-0007 §4.6) adds
// the four surface-energy temperatures. Schema 3 (ADR-0007 §10) stores the
// ocean mixed layer as float64 under a new field ID; the schema 2 -> 3 step
// widens the float32 values exactly and needs no initialiser. Schema 1 loads
// through SnapshotMigration, whose initialiser writes the current types.
inline constexpr std::uint32_t persistent_snapshot_schema_version = 3U;
inline constexpr std::uint32_t oldest_readable_snapshot_schema_version = 1U;

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
    std::uint32_t mesh_generator_version = 0;
    std::uint32_t mesh_checksum = 0;
    std::string parent_snapshot_id;
    std::vector<SnapshotFieldInfo> fields;
};

// Engine version recorded in every snapshot manifest (the CMake project version).
[[nodiscard]] std::string_view snapshot_engine_version() noexcept;

// CRC-32C over the cell centres in mesh order, as little-endian IEEE-754
// doubles. Identifies the mesh geometry a snapshot was written against.
[[nodiscard]] std::uint32_t mesh_geometry_checksum(const PlanetMesh& mesh);

// Writes to a temporary file beside `path` and renames it into place, so an
// interrupted write never replaces an existing snapshot with a partial one.
void write_snapshot(const std::filesystem::path& path,
                    const PlanetState& state,
                    SimulationTick tick,
                    std::string parent_snapshot_id = {});

[[nodiscard]] SnapshotManifest inspect_snapshot(const std::filesystem::path& path);
// Reads any schema from oldest_readable_snapshot_schema_version to the
// current one. An older file is migrated in the staged copy; reading one
// without the initialiser it needs throws, and the target is left untouched.
[[nodiscard]] SnapshotManifest read_snapshot(const std::filesystem::path& path,
                                             PlanetState& target_state,
                                             const SnapshotMigration& migration = {});

}  // namespace planetsim
