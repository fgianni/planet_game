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
    // Schema 3 -> 4: the cryosphere reservoirs (ADR-0008 §4.6).
    std::function<void(const PlanetMesh&, SlowState&)> initialise_schema_4_fields;
};

// Schema 1 (M2): hypsometry and sea level. Schema 2 (M3, ADR-0007 §4.6) adds
// the four surface-energy temperatures. Schema 3 (ADR-0007 §10) stores the
// ocean mixed layer as float64 under a new field ID; the schema 2 -> 3 step
// widens the float32 values exactly and needs no initialiser. Schema 4
// (ADR-0008 §4.6) adds the snow and sea-ice reservoirs. Schemas 1 and 3 load
// through SnapshotMigration, whose initialisers write the current types.
inline constexpr std::uint32_t persistent_snapshot_schema_version = 4U;
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
    // A delta stores only the fields that differ from its parent (ADR-0003
    // §3.5); its manifest carries "kind":"delta".
    bool delta = false;
    std::vector<SnapshotFieldInfo> fields;
};

// Chunk codecs (ADR-0003 §3.4, task M4-05): "none"; "shuffle-zstd", the
// bytes regrouped by position within each 4- or 8-byte element, then zstd at
// level 3; and, in deltas only, "xor-shuffle-zstd", the XOR with the
// parent's bytes, shuffled and compressed.
enum class SnapshotCompression : std::uint8_t { none, shuffle_zstd };

// The canonical, uncompressed bytes of each stored field in ascending field
// ID order: what a snapshot encodes, what state_hash hashes, what a delta
// is taken against.
struct SnapshotChunks {
    std::vector<std::uint32_t> field_ids;
    std::vector<std::vector<std::byte>> bytes;

    [[nodiscard]] const std::vector<std::byte>* find(std::uint32_t field_id) const noexcept;
    [[nodiscard]] std::size_t size_bytes() const noexcept;
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
                    std::string parent_snapshot_id = {},
                    SnapshotCompression compression = SnapshotCompression::shuffle_zstd);

[[nodiscard]] SnapshotChunks encode_snapshot_chunks(const PlanetState& state);

// Writes a delta of `state` against `parent` (the parent's canonical chunks):
// only the fields whose bytes differ, as xor-shuffle-zstd. Returns the
// number of fields stored. Atomic like write_snapshot.
std::size_t write_delta_snapshot(const std::filesystem::path& path,
                                 const PlanetState& state,
                                 SimulationTick tick,
                                 std::string parent_snapshot_id,
                                 const SnapshotChunks& parent);

// Reads a full snapshot, or a delta given its parent's chunks, into the
// canonical chunks of every stored field (a delta's unchanged fields come
// from the parent). `manifest` receives the file's manifest.
[[nodiscard]] SnapshotChunks read_snapshot_chunks(const std::filesystem::path& path,
                                                  const SnapshotChunks* parent,
                                                  SnapshotManifest& manifest);

// Decodes chunks into the target's slow state, migrating older schemas as
// read_snapshot does; checks the mesh identity first. The target is left
// untouched on failure.
void decode_snapshot_chunks(const SnapshotManifest& manifest, const SnapshotChunks& chunks,
                            PlanetState& target_state,
                            const SnapshotMigration& migration = {});

// ADR-0003 §3.3 state_hash: XXH3-64 over the slow state in canonical order,
// the same field chunks a snapshot stores, each preceded by its field ID
// (little-endian u32) and byte length (little-endian u64). Equal slow states
// hash equal; the tick, manifest and mesh are not part of it.
[[nodiscard]] std::uint64_t slow_state_hash(const PlanetState& state);

[[nodiscard]] SnapshotManifest inspect_snapshot(const std::filesystem::path& path);
// Reads a full snapshot of any schema from
// oldest_readable_snapshot_schema_version to the current one, and of any
// codec. An older file is migrated in the staged copy; reading one without
// the initialiser it needs throws, and the target is left untouched. A delta
// needs its chain (HistoryStore) and is refused.
[[nodiscard]] SnapshotManifest read_snapshot(const std::filesystem::path& path,
                                             PlanetState& target_state,
                                             const SnapshotMigration& migration = {});

}  // namespace planetsim
