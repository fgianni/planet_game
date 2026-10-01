#include "sim/core/serialization/history_store.hpp"

#include "sim/core/serialization/xxh3.hpp"
#include "sim/planet/planet_state.hpp"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

constexpr std::string_view index_header = "PHISTv1";
constexpr std::string_view index_name = "history.txt";

[[nodiscard]] std::string hex16(std::uint64_t value) {
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << value;
    return text.str();
}

[[nodiscard]] std::string snapshot_id(bool delta, std::string_view parent_id,
                                      SimulationTick tick, std::uint64_t state_hash) {
    std::string key = delta ? "delta|" : "full|";
    key += parent_id;
    key += '|';
    key += std::to_string(tick);
    key += '|';
    key += hex16(state_hash);
    const auto* bytes = reinterpret_cast<const std::byte*>(key.data());
    return hex16(xxh3_64({bytes, key.size()}));
}

}  // namespace

HistoryStore::HistoryStore(std::filesystem::path directory, std::uint32_t base_interval)
    : directory_(std::move(directory)), base_interval_(base_interval) {
    std::filesystem::create_directories(directory_);
    std::ifstream index(directory_ / index_name);
    if (!index) {
        return;
    }
    std::string line;
    if (!std::getline(index, line) || line != index_header) {
        throw std::runtime_error("history index has no " + std::string(index_header) +
                                 " header: " + (directory_ / index_name).string());
    }
    while (std::getline(index, line)) {
        std::istringstream fields(line);
        HistoryEntry entry;
        std::string parent;
        std::string kind;
        if (!(fields >> entry.id >> parent >> kind >> entry.tick >> entry.depth >>
              entry.file_bytes) ||
            (kind != "full" && kind != "delta")) {
            throw std::runtime_error("malformed history index line: " + line);
        }
        entry.parent_id = parent == "-" ? std::string() : parent;
        entry.delta = kind == "delta";
        if (entry.delta && find(entry.parent_id) == nullptr) {
            throw std::runtime_error("history index names an unknown parent: " + line);
        }
        entries_.push_back(std::move(entry));
    }
}

const HistoryEntry* HistoryStore::find(std::string_view id) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const HistoryEntry& HistoryStore::entry(std::string_view id) const {
    const auto* found = find(id);
    if (found == nullptr) {
        throw std::invalid_argument("unknown history snapshot: " + std::string(id));
    }
    return *found;
}

std::filesystem::path HistoryStore::path_of(std::string_view id) const {
    return directory_ / (std::string(id) + ".psnap");
}

void HistoryStore::append_index(const HistoryEntry& entry) const {
    const auto path = directory_ / index_name;
    const bool fresh = !std::filesystem::exists(path);
    std::ofstream index(path, std::ios::app);
    if (!index) {
        throw std::runtime_error("cannot append to history index: " + path.string());
    }
    if (fresh) {
        index << index_header << '\n';
    }
    index << entry.id << ' ' << (entry.parent_id.empty() ? "-" : entry.parent_id) << ' '
          << (entry.delta ? "delta" : "full") << ' ' << entry.tick << ' ' << entry.depth << ' '
          << entry.file_bytes << '\n';
    if (!index) {
        throw std::runtime_error("failed while appending to history index: " + path.string());
    }
}

SnapshotChunks HistoryStore::chunks_of(std::string_view id, SnapshotManifest& manifest) const {
    if (cache_ && cache_->first == id) {
        manifest = inspect_snapshot(path_of(id));
        return cache_->second;
    }
    // The chain back to its full snapshot, then forward.
    std::vector<const HistoryEntry*> chain;
    for (const auto* entry = &this->entry(id);; entry = &this->entry(entry->parent_id)) {
        chain.push_back(entry);
        if (!entry->delta) {
            break;
        }
    }
    SnapshotChunks chunks;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const bool first = it == chain.rbegin();
        chunks = read_snapshot_chunks(path_of((*it)->id), first ? nullptr : &chunks, manifest);
    }
    return chunks;
}

std::string HistoryStore::save(const PlanetState& state, SimulationTick tick,
                               std::string_view parent_id) {
    const HistoryEntry* parent = parent_id.empty() ? nullptr : &entry(parent_id);
    const bool delta = parent != nullptr && parent->depth < base_interval_;
    const std::string id = snapshot_id(delta, parent_id, tick, slow_state_hash(state));
    if (find(id) != nullptr) {
        return id;
    }

    HistoryEntry entry;
    entry.id = id;
    entry.parent_id = std::string(parent_id);
    entry.delta = delta;
    entry.tick = tick;
    entry.depth = delta ? parent->depth + 1U : 0U;
    const auto path = path_of(id);
    if (delta) {
        SnapshotManifest parent_manifest;
        const SnapshotChunks parent_chunks = chunks_of(parent_id, parent_manifest);
        static_cast<void>(write_delta_snapshot(path, state, tick, entry.parent_id, parent_chunks));
    } else {
        write_snapshot(path, state, tick, entry.parent_id);
    }
    entry.file_bytes = std::filesystem::file_size(path);
    append_index(entry);
    entries_.push_back(entry);
    cache_.emplace(id, encode_snapshot_chunks(state));
    return id;
}

SnapshotManifest HistoryStore::load(std::string_view id, PlanetState& target,
                                    const SnapshotMigration& migration) const {
    SnapshotManifest manifest;
    SnapshotChunks chunks = chunks_of(id, manifest);
    manifest.applied_migrations = decode_snapshot_chunks(manifest, chunks, target, migration);
    cache_.emplace(std::string(id), std::move(chunks));
    return manifest;
}

}  // namespace planetsim
