#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/serialization/snapshot_file.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace planetsim {

class PlanetState;

// One snapshot of a history (ADR-0003 §3.5).
struct HistoryEntry {
    std::string id;
    std::string parent_id;        // empty for a root
    bool delta = false;
    SimulationTick tick = 0;
    std::uint32_t depth = 0;      // deltas since the last full snapshot on its chain
    std::uint64_t file_bytes = 0;
};

// A history: a DAG of snapshots in one directory, as `<id>.psnap` files and an
// appendable `history.txt` index (ADR-0003 §3.5, task M4-05).
//
// A save with a parent writes a delta (only the fields whose bytes changed,
// XOR against the parent) unless the parent's chain already holds
// `base_interval` deltas since its last full snapshot, in which case a full
// snapshot bounds the reconstruction cost. A fork is a save whose parent is
// not the latest snapshot. Ids are 16 hex digits of XXH3-64 over the kind,
// the parent id, the tick and the state_hash, so equal histories have equal
// ids. Loading any id walks its chain from the full snapshot and applies the
// deltas in order; the result is the saved state bit for bit (V6).
class HistoryStore {
  public:
    // Opens or creates the directory and reads its index.
    explicit HistoryStore(std::filesystem::path directory, std::uint32_t base_interval = 8U);

    // Saves `state` at `tick` as a child of `parent_id` (empty: a new root).
    // Returns the id; saving the same state at the same tick under the same
    // parent again returns the existing id without writing.
    std::string save(const PlanetState& state, SimulationTick tick,
                     std::string_view parent_id = {});

    // Reconstructs a snapshot into the target (migrating as read_snapshot
    // does); the target is left untouched on failure.
    SnapshotManifest load(std::string_view id, PlanetState& target,
                          const SnapshotMigration& migration = {}) const;

    [[nodiscard]] const std::vector<HistoryEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const HistoryEntry& entry(std::string_view id) const;
    [[nodiscard]] std::filesystem::path path_of(std::string_view id) const;
    [[nodiscard]] std::uint32_t base_interval() const noexcept { return base_interval_; }

  private:
    [[nodiscard]] const HistoryEntry* find(std::string_view id) const noexcept;
    [[nodiscard]] SnapshotChunks chunks_of(std::string_view id, SnapshotManifest& manifest) const;
    void append_index(const HistoryEntry& entry) const;

    std::filesystem::path directory_;
    std::uint32_t base_interval_;
    std::vector<HistoryEntry> entries_;
    // The chunks of the last snapshot saved or loaded, the usual parent of
    // the next save.
    mutable std::optional<std::pair<std::string, SnapshotChunks>> cache_;
};

}  // namespace planetsim
