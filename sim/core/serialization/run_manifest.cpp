#include "sim/core/serialization/run_manifest.hpp"

#include "sim/core/serialization/xxh3.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace planetsim {
namespace {

constexpr std::string_view run_manifest_format = "PRUNv1";

void require_plain(std::string_view value, std::string_view what, bool allow_empty) {
    if (!allow_empty && value.empty()) {
        throw std::invalid_argument("run manifest " + std::string(what) + " is empty");
    }
    if (value.find_first_of("\t\n\r") != std::string_view::npos) {
        throw std::invalid_argument("run manifest " + std::string(what) +
                                    " contains a tab or line break");
    }
}

[[nodiscard]] std::string hex16(std::uint64_t value) {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string text(16U, '0');
    for (std::size_t index = 0; index < 16U; ++index) {
        text[15U - index] = digits[(value >> (4U * index)) & 0xFU];
    }
    return text;
}

[[nodiscard]] std::uint64_t parse_hex16(std::string_view text, std::size_t line) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.size() != 16U || result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        throw std::runtime_error("run manifest line " + std::to_string(line) +
                                 ": expected 16 hex digits");
    }
    return value;
}

template <typename Integer>
[[nodiscard]] Integer parse_integer(std::string_view text, std::size_t line) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("run manifest line " + std::to_string(line) +
                                 ": invalid integer '" + std::string(text) + "'");
    }
    return value;
}

[[nodiscard]] std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1U;
    }
}

void validate(const RunManifest& manifest) {
    require_plain(manifest.engine_version, "engine_version", false);
    for (const auto& [key, value] : manifest.scenario) {
        require_plain(key, "scenario key", false);
        require_plain(value, "scenario value", true);
    }
    for (std::size_t index = 0; index < manifest.commands.size(); ++index) {
        const auto& command = manifest.commands[index];
        require_plain(command.actor, "command actor", false);
        require_plain(command.type, "command type", false);
        require_plain(command.payload, "command payload", true);
        if (index > 0U && command.tick < manifest.commands[index - 1U].tick) {
            throw std::invalid_argument("run manifest commands are not in tick order");
        }
    }
    for (std::size_t index = 1; index < manifest.checkpoints.size(); ++index) {
        if (manifest.checkpoints[index].tick <= manifest.checkpoints[index - 1U].tick) {
            throw std::invalid_argument("run manifest checkpoints are not in increasing tick order");
        }
    }
}

}  // namespace

std::uint64_t scenario_hash(const ScenarioEntries& scenario) {
    std::string canonical;
    for (const auto& [key, value] : scenario) {
        canonical += key;
        canonical += '\t';
        canonical += value;
        canonical += '\n';
    }
    return xxh3_64({reinterpret_cast<const std::byte*>(canonical.data()), canonical.size()});
}

std::optional<std::string_view> scenario_value(const ScenarioEntries& scenario,
                                               std::string_view key) {
    for (const auto& [entry_key, value] : scenario) {
        if (entry_key == key) {
            return std::string_view{value};
        }
    }
    return std::nullopt;
}

std::string format_run_manifest(const RunManifest& manifest) {
    validate(manifest);
    std::string text;
    text += "format\t";
    text += run_manifest_format;
    text += "\nengine_version\t" + manifest.engine_version;
    text += "\nmesh_generator_version\t" + std::to_string(manifest.mesh_generator_version) + '\n';
    for (const auto& [key, value] : manifest.scenario) {
        text += "scenario\t" + key + '\t' + value + '\n';
    }
    text += "scenario_hash\t" + hex16(scenario_hash(manifest.scenario)) + '\n';

    // Merge in tick order; a checkpoint precedes a command at the same tick,
    // since the state is hashed on arriving at a boundary and commands take
    // effect on leaving it.
    std::size_t command = 0;
    std::size_t checkpoint = 0;
    while (command < manifest.commands.size() || checkpoint < manifest.checkpoints.size()) {
        const bool take_checkpoint =
            checkpoint < manifest.checkpoints.size() &&
            (command == manifest.commands.size() ||
             manifest.checkpoints[checkpoint].tick <= manifest.commands[command].tick);
        if (take_checkpoint) {
            const auto& entry = manifest.checkpoints[checkpoint++];
            text += "checkpoint\t" + std::to_string(entry.tick) + '\t' + hex16(entry.state_hash) +
                    '\n';
        } else {
            const auto& entry = manifest.commands[command++];
            text += "command\t" + std::to_string(entry.tick) + '\t' + entry.actor + '\t' +
                    entry.type + '\t' + entry.payload + '\n';
        }
    }
    if (manifest.end_tick) {
        text += "end_tick\t" + std::to_string(*manifest.end_tick) + '\n';
    }
    return text;
}

RunManifest parse_run_manifest(std::string_view text) {
    RunManifest manifest;
    std::optional<std::uint64_t> stored_scenario_hash;
    bool has_format = false;
    bool has_engine = false;
    bool has_generator = false;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            throw std::runtime_error("run manifest does not end with a line break");
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1U;
        ++line_number;
        const auto fields = split_tabs(line);
        const std::string_view record = fields.front();
        const auto expect_fields = [&](std::size_t count) {
            if (fields.size() != count) {
                throw std::runtime_error("run manifest line " + std::to_string(line_number) +
                                         ": " + std::string(record) + " needs " +
                                         std::to_string(count - 1U) + " fields");
            }
        };
        if (line_number == 1U) {
            if (record != "format" || fields.size() != 2U || fields[1] != run_manifest_format) {
                throw std::runtime_error("not a PRUNv1 run manifest");
            }
            has_format = true;
            continue;
        }
        if (record == "engine_version") {
            expect_fields(2U);
            manifest.engine_version = fields[1];
            has_engine = true;
        } else if (record == "mesh_generator_version") {
            expect_fields(2U);
            manifest.mesh_generator_version = parse_integer<std::uint32_t>(fields[1], line_number);
            has_generator = true;
        } else if (record == "scenario") {
            expect_fields(3U);
            if (stored_scenario_hash) {
                throw std::runtime_error("run manifest scenario entry after scenario_hash");
            }
            manifest.scenario.emplace_back(fields[1], fields[2]);
        } else if (record == "scenario_hash") {
            expect_fields(2U);
            stored_scenario_hash = parse_hex16(fields[1], line_number);
        } else if (record == "command") {
            expect_fields(5U);
            manifest.commands.push_back({parse_integer<SimulationTick>(fields[1], line_number),
                                         std::string(fields[2]), std::string(fields[3]),
                                         std::string(fields[4])});
        } else if (record == "checkpoint") {
            expect_fields(3U);
            manifest.checkpoints.push_back({parse_integer<SimulationTick>(fields[1], line_number),
                                            parse_hex16(fields[2], line_number)});
        } else if (record == "end_tick") {
            expect_fields(2U);
            manifest.end_tick = parse_integer<SimulationTick>(fields[1], line_number);
        } else {
            throw std::runtime_error("run manifest line " + std::to_string(line_number) +
                                     ": unknown record '" + std::string(record) + "'");
        }
    }
    if (!has_format || !has_engine || !has_generator || !stored_scenario_hash) {
        throw std::runtime_error("run manifest header is incomplete");
    }
    if (*stored_scenario_hash != scenario_hash(manifest.scenario)) {
        throw std::runtime_error("run manifest scenario_hash does not match its scenario");
    }
    try {
        validate(manifest);
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(error.what());
    }
    return manifest;
}

void write_run_manifest(const std::filesystem::path& path, const RunManifest& manifest) {
    const std::string text = format_run_manifest(manifest);
    auto partial_path = path;
    partial_path += ".partial";
    try {
        {
            std::ofstream output(partial_path, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open run manifest for writing: " +
                                         partial_path.string());
            }
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            if (!output) {
                throw std::runtime_error("failed while writing run manifest: " +
                                         partial_path.string());
            }
        }
        std::filesystem::rename(partial_path, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(partial_path, ignored);
        throw;
    }
}

RunManifest read_run_manifest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open run manifest: " + path.string());
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return parse_run_manifest(text);
}

}  // namespace planetsim
