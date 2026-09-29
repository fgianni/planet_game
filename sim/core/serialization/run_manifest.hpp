#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace planetsim {

// ADR-0003 §3.3: a run is recorded as its inputs (scenario, seed, build and
// the commands) plus periodic state hashes that locate a divergence.
//
// A command takes effect at the start of the first step that begins at or
// after its tick. The manifest records the tick at which it took effect, a
// step boundary, so a replay applies it at the same boundary.
struct RunCommand {
    SimulationTick tick = 0;
    std::string actor;     // who issued it: "user", "cli", "test", ...
    std::string type;      // e.g. "set_mode"; the run validates it
    std::string payload;   // the exact text parsed by the run; replay reparses it
};

struct RunCheckpoint {
    SimulationTick tick = 0;
    std::uint64_t state_hash = 0;   // slow_state_hash (snapshot_file.hpp)
};

// The scenario is an ordered list of key/value pairs owned by the domain
// layer; the core stores and hashes them without interpreting them.
using ScenarioEntries = std::vector<std::pair<std::string, std::string>>;

struct RunManifest {
    std::string engine_version;
    std::uint32_t mesh_generator_version = 0;
    ScenarioEntries scenario;
    std::vector<RunCommand> commands;          // in effective-tick order
    std::vector<RunCheckpoint> checkpoints;    // in tick order
    std::optional<SimulationTick> end_tick;    // absent while the run is open
};

// XXH3-64 of the scenario's canonical text: one "key\tvalue\n" line per entry
// in order.
[[nodiscard]] std::uint64_t scenario_hash(const ScenarioEntries& scenario);

// The value of `key`, or nullopt.
[[nodiscard]] std::optional<std::string_view> scenario_value(const ScenarioEntries& scenario,
                                                             std::string_view key);

// Text format "PRUNv1", one tab-separated record per line, append-friendly:
//
//   format  PRUNv1
//   engine_version  <text>
//   mesh_generator_version  <n>
//   scenario  <key>  <value>          (repeated, in order)
//   scenario_hash  <16 hex digits>
//   command  <tick>  <actor>  <type>  <payload>
//   checkpoint  <tick>  <16 hex digits>
//   end_tick  <tick>
//
// Commands and checkpoints may interleave in tick order. Fields may not
// contain tabs, newlines or carriage returns; actor and type may not be empty.
[[nodiscard]] std::string format_run_manifest(const RunManifest& manifest);
[[nodiscard]] RunManifest parse_run_manifest(std::string_view text);

// Writes through a .partial file renamed into place.
void write_run_manifest(const std::filesystem::path& path, const RunManifest& manifest);
[[nodiscard]] RunManifest read_run_manifest(const std::filesystem::path& path);

}  // namespace planetsim
