#include "sim/core/serialization/snapshot_file.hpp"

#include "sim/core/serialization/crc32c.hpp"
#include "sim/planet/planet_state.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef PLANETSIM_ENGINE_VERSION
#error "PLANETSIM_ENGINE_VERSION must be supplied by the build"
#endif

namespace planetsim {
namespace {

inline constexpr std::array<std::byte, 8> snapshot_magic{
    std::byte{'P'}, std::byte{'S'}, std::byte{'N'}, std::byte{'A'},
    std::byte{'P'}, std::byte{'v'}, std::byte{'1'}, std::byte{0},
};

struct EncodedChunk {
    const FieldDescriptor* descriptor = nullptr;
    std::uint64_t offset = 0;
    std::vector<std::byte> bytes;
    std::uint32_t checksum = 0;
};

template <typename UInt>
void append_little_endian(std::vector<std::byte>& output, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t byte_index = 0; byte_index < sizeof(UInt); ++byte_index) {
        output.push_back(
            static_cast<std::byte>((value >> (byte_index * 8U)) & static_cast<UInt>(0xFFU)));
    }
}

void append_float32(std::vector<std::byte>& output, float value) {
    append_little_endian(output, std::bit_cast<std::uint32_t>(value));
}

void append_float64(std::vector<std::byte>& output, double value) {
    append_little_endian(output, std::bit_cast<std::uint64_t>(value));
}

[[nodiscard]] std::string_view partition_name(FieldPartition partition) {
    switch (partition) {
    case FieldPartition::slow:
        return "slow";
    case FieldPartition::fast:
        return "fast";
    case FieldPartition::climatology:
        return "climatology";
    case FieldPartition::derived:
        return "derived";
    }
    throw std::logic_error("unknown field partition");
}

[[nodiscard]] std::string_view layout_name(FieldLayout layout) {
    switch (layout) {
    case FieldLayout::cell:
        return "cell";
    case FieldLayout::cell_layers:
        return "cell_layers";
    case FieldLayout::edge:
        return "edge";
    case FieldLayout::global:
        return "global";
    }
    throw std::logic_error("unknown field layout");
}

[[nodiscard]] std::string_view data_type_name(FieldDataType data_type) {
    switch (data_type) {
    case FieldDataType::float32:
        return "float32";
    case FieldDataType::float64:
        return "float64";
    }
    throw std::logic_error("unknown field data type");
}

void append_json_string(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            output.push_back('\\');
            output.push_back(character);
        } else {
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw std::invalid_argument("snapshot manifest strings may not contain controls");
            }
            output.push_back(character);
        }
    }
    output.push_back('"');
}

[[nodiscard]] EncodedChunk encode_chunk(const FieldDescriptor& descriptor,
                                        const PlanetState& state,
                                        std::uint64_t offset) {
    EncodedChunk chunk;
    chunk.descriptor = &descriptor;
    chunk.offset = offset;

    switch (descriptor.id) {
    case FieldId::hypsometry_m:
        chunk.bytes.reserve(state.slow().hypsometry_m.size() * sizeof(float));
        for (std::size_t layer = 0; layer < state.slow().hypsometry_m.layer_count(); ++layer) {
            for (const float value : state.slow().hypsometry_m.layer(layer)) {
                append_float32(chunk.bytes, value);
            }
        }
        break;
    case FieldId::sea_level_m:
        chunk.bytes.reserve(sizeof(double));
        append_float64(chunk.bytes, state.slow().sea_level_m);
        break;
    case FieldId::top_of_atmosphere_insolation_W_m2:
        throw std::logic_error("derived forcing field cannot be persisted");
    }

    chunk.checksum = crc32c({chunk.bytes.data(), chunk.bytes.size()});
    return chunk;
}

[[nodiscard]] std::vector<EncodedChunk> encode_slow_state(const PlanetState& state) {
    std::vector<EncodedChunk> chunks;
    std::uint64_t offset = 0;
    for (const auto& descriptor : field_registry) {
        if (!descriptor.persistent()) {
            continue;
        }
        auto chunk = encode_chunk(descriptor, state, offset);
        if (chunk.bytes.size() > std::numeric_limits<std::uint64_t>::max() - offset) {
            throw std::length_error("snapshot chunk area is too large");
        }
        offset += static_cast<std::uint64_t>(chunk.bytes.size());
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

[[nodiscard]] std::string build_manifest(const PlanetState& state,
                                         SimulationTick tick,
                                         std::string_view parent_snapshot_id,
                                         const std::vector<EncodedChunk>& chunks) {
    std::string manifest;
    manifest.reserve(512U + chunks.size() * 256U);
    manifest += "{\"format\":\"PSNAPv1\",\"schema_version\":";
    manifest += std::to_string(persistent_snapshot_schema_version);
    manifest += ",\"engine_version\":";
    append_json_string(manifest, PLANETSIM_ENGINE_VERSION);
    manifest += ",\"tick\":";
    manifest += std::to_string(tick);
    manifest += ",\"mesh_level\":";
    manifest += std::to_string(state.mesh().subdivision());
    manifest += ",\"cell_count\":";
    manifest += std::to_string(state.mesh().cell_count());
    manifest += ",\"parent_snapshot_id\":";
    append_json_string(manifest, parent_snapshot_id);
    manifest += ",\"fields\":[";

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        const auto& chunk = chunks[index];
        const auto& descriptor = *chunk.descriptor;
        if (index != 0U) {
            manifest.push_back(',');
        }
        manifest += "{\"field_id\":";
        manifest += std::to_string(static_cast<std::uint32_t>(descriptor.id));
        manifest += ",\"name\":";
        append_json_string(manifest, descriptor.name);
        manifest += ",\"partition\":";
        append_json_string(manifest, partition_name(descriptor.partition));
        manifest += ",\"layout\":";
        append_json_string(manifest, layout_name(descriptor.layout));
        manifest += ",\"dtype\":";
        append_json_string(manifest, data_type_name(descriptor.data_type));
        manifest += ",\"layers\":";
        manifest += std::to_string(descriptor.layers);
        manifest += ",\"compression\":\"none\",\"byte_range\":[";
        manifest += std::to_string(chunk.offset);
        manifest.push_back(',');
        manifest += std::to_string(chunk.bytes.size());
        manifest += "],\"checksum\":";
        manifest += std::to_string(chunk.checksum);
        manifest.push_back('}');
    }
    manifest += "]}";
    return manifest;
}

void write_bytes(std::ofstream& output, std::span<const std::byte> bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::length_error("snapshot write exceeds stream size");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

void write_snapshot(const std::filesystem::path& path,
                    const PlanetState& state,
                    SimulationTick tick,
                    std::string parent_snapshot_id) {
    if (tick < 0) {
        throw std::invalid_argument("snapshot tick must be non-negative");
    }

    const auto chunks = encode_slow_state(state);
    const auto manifest = build_manifest(state, tick, parent_snapshot_id, chunks);
    if (manifest.size() > std::numeric_limits<std::uint64_t>::max()) {
        throw std::length_error("snapshot manifest is too large");
    }

    std::vector<std::byte> header(snapshot_magic.begin(), snapshot_magic.end());
    append_little_endian(header, static_cast<std::uint64_t>(manifest.size()));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open snapshot for writing: " + path.string());
    }
    write_bytes(output, header);
    output.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
    for (const auto& chunk : chunks) {
        write_bytes(output, chunk.bytes);
    }
    if (!output) {
        throw std::runtime_error("failed while writing snapshot: " + path.string());
    }
}

}  // namespace planetsim
