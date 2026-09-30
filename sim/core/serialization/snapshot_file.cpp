#include "sim/core/serialization/snapshot_file.hpp"

#include "sim/core/serialization/crc32c.hpp"
#include "sim/core/serialization/xxh3.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
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

struct ParsedSnapshot {
    SnapshotManifest manifest;
    std::vector<std::byte> file_bytes;
    std::size_t chunk_area_offset = 0;
};

template <typename UInt>
void append_little_endian(std::vector<std::byte>& output, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t byte_index = 0; byte_index < sizeof(UInt); ++byte_index) {
        output.push_back(
            static_cast<std::byte>((value >> (byte_index * 8U)) & static_cast<UInt>(0xFFU)));
    }
}

template <typename UInt>
[[nodiscard]] UInt read_little_endian(std::span<const std::byte> bytes,
                                      std::size_t offset,
                                      std::string_view description) {
    static_assert(std::is_unsigned_v<UInt>);
    if (offset > bytes.size() || sizeof(UInt) > bytes.size() - offset) {
        throw std::runtime_error("truncated snapshot " + std::string(description));
    }
    UInt value = 0;
    for (std::size_t byte_index = 0; byte_index < sizeof(UInt); ++byte_index) {
        value |= static_cast<UInt>(std::to_integer<std::uint8_t>(bytes[offset + byte_index]))
                 << (byte_index * 8U);
    }
    return value;
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

// The schema version that introduced each persistent field; 0 for fields
// that are never persisted. The switch covers every FieldId, so a new field
// must declare its version to compile.
[[nodiscard]] std::uint32_t first_schema_version(FieldId id) noexcept {
    switch (id) {
    case FieldId::hypsometry_m:
    case FieldId::sea_level_m:
        return 1U;
    case FieldId::land_surface_temperature_K:
    case FieldId::land_ground_temperature_K:
    case FieldId::ocean_deep_temperature_K:
        return 2U;
    case FieldId::ocean_mixed_layer_temperature_K:
        return 3U;
    case FieldId::land_snow_water_equivalent_kg_m2:
    case FieldId::sea_ice_mass_kg_m2:
        return 4U;
    case FieldId::top_of_atmosphere_insolation_W_m2:
    case FieldId::substep_mean_insolation_W_m2:
    case FieldId::prescribed_precipitation_kg_m2_s:
        return 0U;
    }
    return 0U;
}

void append_cell_field(std::vector<std::byte>& output, const Field2D<float>& field) {
    output.reserve(field.size() * sizeof(float));
    for (const float value : field.values()) {
        append_float32(output, value);
    }
}

void append_cell_field(std::vector<std::byte>& output, const Field2D<double>& field) {
    output.reserve(field.size() * sizeof(double));
    for (const double value : field.values()) {
        append_float64(output, value);
    }
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
    case FieldId::land_surface_temperature_K:
        append_cell_field(chunk.bytes, state.slow().land_surface_temperature_K);
        break;
    case FieldId::land_ground_temperature_K:
        append_cell_field(chunk.bytes, state.slow().land_ground_temperature_K);
        break;
    case FieldId::ocean_mixed_layer_temperature_K:
        append_cell_field(chunk.bytes, state.slow().ocean_mixed_layer_temperature_K);
        break;
    case FieldId::ocean_deep_temperature_K:
        append_cell_field(chunk.bytes, state.slow().ocean_deep_temperature_K);
        break;
    case FieldId::land_snow_water_equivalent_kg_m2:
        append_cell_field(chunk.bytes, state.slow().land_snow_water_equivalent_kg_m2);
        break;
    case FieldId::sea_ice_mass_kg_m2:
        append_cell_field(chunk.bytes, state.slow().sea_ice_mass_kg_m2);
        break;
    case FieldId::top_of_atmosphere_insolation_W_m2:
    case FieldId::substep_mean_insolation_W_m2:
    case FieldId::prescribed_precipitation_kg_m2_s:
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
    manifest += ",\"mesh_generator_version\":";
    manifest += std::to_string(mesh_generator_version);
    manifest += ",\"mesh_checksum\":";
    manifest += std::to_string(mesh_geometry_checksum(state.mesh()));
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

[[nodiscard]] bool is_valid_utf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuation_count = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuation_count = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuation_count = 3U;
        } else {
            return false;
        }
        if (continuation_count > text.size() - index - 1U) {
            return false;
        }

        const auto second = static_cast<unsigned char>(text[index + 1U]);
        if ((second & 0xC0U) != 0x80U) {
            return false;
        }
        if ((first == 0xE0U && second < 0xA0U) ||
            (first == 0xEDU && second >= 0xA0U) ||
            (first == 0xF0U && second < 0x90U) ||
            (first == 0xF4U && second >= 0x90U)) {
            return false;
        }
        for (std::size_t continuation = 2U; continuation <= continuation_count; ++continuation) {
            const auto byte = static_cast<unsigned char>(text[index + continuation]);
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
        }
        index += continuation_count + 1U;
    }
    return true;
}

class ManifestParser {
  public:
    explicit ManifestParser(std::string_view text) : text_(text) {}

    [[nodiscard]] SnapshotManifest parse() {
        SnapshotManifest manifest;
        expect('{');
        expect_key("format");
        manifest.format = parse_string();
        expect_next_key("schema_version");
        manifest.schema_version = parse_u32("schema_version");
        expect_next_key("engine_version");
        manifest.engine_version = parse_string();
        expect_next_key("tick");
        const std::uint64_t tick = parse_u64();
        if (tick > static_cast<std::uint64_t>(std::numeric_limits<SimulationTick>::max())) {
            fail("tick is out of range");
        }
        manifest.tick = static_cast<SimulationTick>(tick);
        expect_next_key("mesh_level");
        manifest.mesh_level = parse_u32("mesh_level");
        expect_next_key("cell_count");
        manifest.cell_count = parse_u64();
        expect_next_key("mesh_generator_version");
        manifest.mesh_generator_version = parse_u32("mesh_generator_version");
        expect_next_key("mesh_checksum");
        manifest.mesh_checksum = parse_u32("mesh_checksum");
        expect_next_key("parent_snapshot_id");
        manifest.parent_snapshot_id = parse_string();
        expect_next_key("fields");
        manifest.fields = parse_fields();
        expect('}');
        if (position_ != text_.size()) {
            fail("trailing data");
        }
        return manifest;
    }

  private:
    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error("snapshot manifest at byte " + std::to_string(position_) +
                                 ": " + std::string(message));
    }

    void expect(char expected) {
        if (position_ >= text_.size() || text_[position_] != expected) {
            fail(std::string("expected '") + expected + "'");
        }
        ++position_;
    }

    void expect_key(std::string_view expected) {
        const std::string key = parse_string();
        if (key != expected) {
            fail("expected key " + std::string(expected));
        }
        expect(':');
    }

    void expect_next_key(std::string_view expected) {
        expect(',');
        expect_key(expected);
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') {
                if (!is_valid_utf8(value)) {
                    fail("string is not valid UTF-8");
                }
                return value;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("control character in string");
            }
            if (character == '\\') {
                if (position_ >= text_.size()) {
                    fail("truncated string escape");
                }
                const char escaped = text_[position_++];
                if (escaped != '"' && escaped != '\\') {
                    fail("unsupported string escape");
                }
                value.push_back(escaped);
            } else {
                value.push_back(character);
            }
        }
        fail("unterminated string");
    }

    [[nodiscard]] std::uint64_t parse_u64() {
        if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') {
            fail("expected non-negative integer");
        }
        if (text_[position_] == '0' && position_ + 1U < text_.size() &&
            text_[position_ + 1U] >= '0' && text_[position_ + 1U] <= '9') {
            fail("integer has a leading zero");
        }

        std::uint64_t value = 0;
        while (position_ < text_.size() && text_[position_] >= '0' &&
               text_[position_] <= '9') {
            const auto digit = static_cast<std::uint64_t>(text_[position_] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                fail("integer is out of range");
            }
            value = value * 10U + digit;
            ++position_;
        }
        return value;
    }

    [[nodiscard]] std::uint32_t parse_u32(std::string_view description) {
        const std::uint64_t value = parse_u64();
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            fail(std::string(description) + " is out of range");
        }
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] SnapshotFieldInfo parse_field() {
        SnapshotFieldInfo field;
        expect('{');
        expect_key("field_id");
        field.field_id = parse_u32("field_id");
        expect_next_key("name");
        field.name = parse_string();
        expect_next_key("partition");
        field.partition = parse_string();
        expect_next_key("layout");
        field.layout = parse_string();
        expect_next_key("dtype");
        field.dtype = parse_string();
        expect_next_key("layers");
        field.layers = parse_u32("layers");
        expect_next_key("compression");
        field.compression = parse_string();
        expect_next_key("byte_range");
        expect('[');
        field.byte_offset = parse_u64();
        expect(',');
        field.byte_length = parse_u64();
        expect(']');
        expect_next_key("checksum");
        field.checksum = parse_u32("checksum");
        expect('}');
        return field;
    }

    [[nodiscard]] std::vector<SnapshotFieldInfo> parse_fields() {
        std::vector<SnapshotFieldInfo> fields;
        expect('[');
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return fields;
        }
        while (true) {
            fields.push_back(parse_field());
            if (position_ >= text_.size()) {
                fail("unterminated fields array");
            }
            if (text_[position_] == ']') {
                ++position_;
                return fields;
            }
            expect(',');
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

[[nodiscard]] std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open snapshot for reading: " + path.string());
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine snapshot size: " + path.string());
    }
    const auto unsigned_size = static_cast<std::uintmax_t>(end);
    if (unsigned_size > std::numeric_limits<std::size_t>::max() ||
        unsigned_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::length_error("snapshot file is too large");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(unsigned_size));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("failed while reading snapshot: " + path.string());
    }
    return bytes;
}

// A persistent field retired from the registry, as it was registered, and
// the schema versions that store it. Files of those versions must still load.
struct RetiredPersistentField {
    FieldDescriptor descriptor;
    std::uint32_t first_schema;
    std::uint32_t last_schema;
};

inline constexpr std::array<RetiredPersistentField, 1> retired_persistent_fields{{
    {{retired_ocean_mixed_layer_temperature_float32_K, "ocean_mixed_layer_temperature_K",
      FieldPartition::slow, FieldLayout::cell, FieldDataType::float32, 1U, "K"},
     2U,
     2U},
}};

consteval bool retired_persistent_fields_are_retired() {
    for (const auto& field : retired_persistent_fields) {
        bool retired = false;
        for (const auto id : retired_field_ids) {
            retired = retired || id == field.descriptor.id;
        }
        if (!retired || field.last_schema >= persistent_snapshot_schema_version) {
            return false;
        }
    }
    return true;
}
static_assert(retired_persistent_fields_are_retired(),
              "a retired persistent field must be retired in the registry and absent from "
              "the current schema");

// A field a snapshot may store: registered, or retired but readable from the
// schemas that stored it. `index` numbers registered fields first.
struct StoredField {
    const FieldDescriptor* descriptor = nullptr;
    std::size_t index = 0;
    std::uint32_t first_schema = 0;
    std::uint32_t last_schema = 0;
};

inline constexpr std::size_t stored_field_count =
    field_registry.size() + retired_persistent_fields.size();

[[nodiscard]] StoredField stored_field_at(std::size_t index) {
    if (index < field_registry.size()) {
        const auto& descriptor = field_registry[index];
        return {&descriptor, index, first_schema_version(descriptor.id),
                persistent_snapshot_schema_version};
    }
    const auto& retired = retired_persistent_fields[index - field_registry.size()];
    return {&retired.descriptor, index, retired.first_schema, retired.last_schema};
}

[[nodiscard]] const StoredField* find_stored_field(std::uint32_t raw_field_id,
                                                   StoredField& storage) {
    for (std::size_t index = 0; index < stored_field_count; ++index) {
        storage = stored_field_at(index);
        if (static_cast<std::uint32_t>(storage.descriptor->id) == raw_field_id) {
            return &storage;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint64_t checked_field_byte_length(const FieldDescriptor& descriptor,
                                                      std::uint64_t cell_count) {
    std::uint64_t element_count = 0;
    switch (descriptor.layout) {
    case FieldLayout::cell:
        element_count = cell_count;
        break;
    case FieldLayout::cell_layers:
        if (descriptor.layers != 0U &&
            cell_count > std::numeric_limits<std::uint64_t>::max() / descriptor.layers) {
            throw std::runtime_error("snapshot field dimensions overflow");
        }
        element_count = cell_count * descriptor.layers;
        break;
    case FieldLayout::global:
        element_count = descriptor.layers;
        break;
    case FieldLayout::edge:
        throw std::runtime_error("persistent edge fields require an edge count in the manifest");
    }

    const std::uint64_t element_size =
        descriptor.data_type == FieldDataType::float32 ? sizeof(float) : sizeof(double);
    if (element_count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        throw std::runtime_error("snapshot field byte length overflows");
    }
    return element_count * element_size;
}

[[noreturn]] void field_error(std::uint32_t field_id, std::string_view message) {
    throw std::runtime_error("snapshot field_id " + std::to_string(field_id) + ": " +
                             std::string(message));
}

void validate_manifest_and_chunks(const SnapshotManifest& manifest,
                                  std::span<const std::byte> chunk_area) {
    if (manifest.format != "PSNAPv1") {
        throw std::runtime_error("unsupported snapshot format: " + manifest.format);
    }
    if (manifest.schema_version < oldest_readable_snapshot_schema_version ||
        manifest.schema_version > persistent_snapshot_schema_version) {
        throw std::runtime_error("unsupported snapshot schema_version " +
                                 std::to_string(manifest.schema_version));
    }
    if (manifest.engine_version.empty()) {
        throw std::runtime_error("snapshot engine_version is empty");
    }

    std::array<bool, stored_field_count> seen{};
    bool has_previous_id = false;
    std::uint32_t previous_id = 0;

    for (const auto& field : manifest.fields) {
        StoredField storage;
        const StoredField* stored = find_stored_field(field.field_id, storage);
        if (stored == nullptr) {
            field_error(field.field_id, "unknown field");
        }
        const FieldDescriptor* descriptor = stored->descriptor;
        if (seen[stored->index]) {
            field_error(field.field_id, "duplicate field");
        }
        seen[stored->index] = true;
        if (has_previous_id && field.field_id <= previous_id) {
            field_error(field.field_id, "fields are not in increasing field_id order");
        }
        previous_id = field.field_id;
        has_previous_id = true;

        if (!descriptor->persistent()) {
            field_error(field.field_id, "field is not slow state");
        }
        if (stored->first_schema == 0U || stored->first_schema > manifest.schema_version ||
            stored->last_schema < manifest.schema_version) {
            field_error(field.field_id, "field does not exist in snapshot schema_version " +
                                            std::to_string(manifest.schema_version));
        }
        if (field.partition != partition_name(descriptor->partition)) {
            field_error(field.field_id, "partition does not match registry");
        }
        if (field.layout != layout_name(descriptor->layout)) {
            field_error(field.field_id, "layout does not match registry");
        }
        if (field.dtype != data_type_name(descriptor->data_type)) {
            field_error(field.field_id, "dtype does not match registry");
        }
        if (field.layers != descriptor->layers) {
            field_error(field.field_id, "layer count does not match registry");
        }
        if (field.compression != "none") {
            field_error(field.field_id, "unsupported compression " + field.compression);
        }

        const std::uint64_t expected_length =
            checked_field_byte_length(*descriptor, manifest.cell_count);
        if (field.byte_length != expected_length) {
            field_error(field.field_id, "byte length does not match registry and cell count");
        }
    }

    for (std::size_t index = 0; index < stored_field_count; ++index) {
        const StoredField stored = stored_field_at(index);
        if (stored.descriptor->persistent() && !seen[index] && stored.first_schema != 0U &&
            stored.first_schema <= manifest.schema_version &&
            manifest.schema_version <= stored.last_schema) {
            field_error(static_cast<std::uint32_t>(stored.descriptor->id),
                        "required slow field is missing");
        }
    }

    std::uint64_t expected_offset = 0;
    for (const auto& field : manifest.fields) {
        if (field.byte_offset != expected_offset) {
            field_error(field.field_id, "chunk is not in canonical back-to-back order");
        }
        if (field.byte_offset > chunk_area.size() ||
            field.byte_length > chunk_area.size() - field.byte_offset) {
            field_error(field.field_id, "chunk byte range is outside the file");
        }

        const auto chunk = chunk_area.subspan(static_cast<std::size_t>(field.byte_offset),
                                              static_cast<std::size_t>(field.byte_length));
        if (crc32c(chunk) != field.checksum) {
            field_error(field.field_id, "CRC-32C checksum mismatch");
        }
        expected_offset += field.byte_length;
    }
    if (expected_offset != chunk_area.size()) {
        throw std::runtime_error("snapshot has trailing or unreferenced chunk bytes");
    }
}

[[nodiscard]] ParsedSnapshot parse_snapshot(const std::filesystem::path& path) {
    ParsedSnapshot parsed;
    parsed.file_bytes = read_file_bytes(path);
    if (parsed.file_bytes.size() < 16U) {
        throw std::runtime_error("snapshot header is truncated");
    }
    for (std::size_t index = 0; index < snapshot_magic.size(); ++index) {
        if (parsed.file_bytes[index] != snapshot_magic[index]) {
            throw std::runtime_error("snapshot magic is invalid");
        }
    }

    const auto file_span =
        std::span<const std::byte>{parsed.file_bytes.data(), parsed.file_bytes.size()};
    const std::uint64_t manifest_length =
        read_little_endian<std::uint64_t>(file_span, 8U, "manifest length");
    if (manifest_length > parsed.file_bytes.size() - 16U) {
        throw std::runtime_error("snapshot manifest is truncated");
    }
    parsed.chunk_area_offset = 16U + static_cast<std::size_t>(manifest_length);

    const auto* manifest_begin =
        reinterpret_cast<const char*>(parsed.file_bytes.data() + 16U);
    const std::string_view manifest_text(manifest_begin,
                                         static_cast<std::size_t>(manifest_length));
    parsed.manifest = ManifestParser(manifest_text).parse();

    const auto chunk_area = file_span.subspan(parsed.chunk_area_offset);
    validate_manifest_and_chunks(parsed.manifest, chunk_area);
    return parsed;
}

[[nodiscard]] const SnapshotFieldInfo& require_field(const SnapshotManifest& manifest,
                                                     FieldId id) {
    const auto raw_id = static_cast<std::uint32_t>(id);
    for (const auto& field : manifest.fields) {
        if (field.field_id == raw_id) {
            return field;
        }
    }
    field_error(raw_id, "required slow field is missing");
}

// Rejects a slow state whose containers do not match the registry and mesh,
// so the writer never produces a file its own reader would refuse.
void validate_slow_state(const PlanetState& state) {
    const auto& hypsometry = state.slow().hypsometry_m;
    if (hypsometry.layer_count() != hypsometry_layer_count ||
        hypsometry.cell_count() != state.mesh().cell_count()) {
        field_error(static_cast<std::uint32_t>(FieldId::hypsometry_m),
                    "slow-state dimensions do not match the registry and mesh");
    }
    const std::size_t cells = state.mesh().cell_count();
    const auto check_cells = [cells](std::size_t size, FieldId id) {
        if (size != cells) {
            field_error(static_cast<std::uint32_t>(id),
                        "slow-state dimensions do not match the registry and mesh");
        }
    };
    check_cells(state.slow().land_surface_temperature_K.size(),
                FieldId::land_surface_temperature_K);
    check_cells(state.slow().land_ground_temperature_K.size(),
                FieldId::land_ground_temperature_K);
    check_cells(state.slow().ocean_mixed_layer_temperature_K.size(),
                FieldId::ocean_mixed_layer_temperature_K);
    check_cells(state.slow().ocean_deep_temperature_K.size(), FieldId::ocean_deep_temperature_K);
    check_cells(state.slow().land_snow_water_equivalent_kg_m2.size(),
                FieldId::land_snow_water_equivalent_kg_m2);
    check_cells(state.slow().sea_ice_mass_kg_m2.size(), FieldId::sea_ice_mass_kg_m2);
}

[[nodiscard]] Field2D<float> decode_float_cells(std::span<const std::byte> bytes,
                                                std::size_t cell_count) {
    Field2D<float> values(cell_count, 0.0F);
    for (std::size_t index = 0; index < cell_count; ++index) {
        values[index] = std::bit_cast<float>(read_little_endian<std::uint32_t>(
            bytes, index * sizeof(std::uint32_t), "cell value"));
    }
    return values;
}

[[nodiscard]] Field2D<double> decode_double_cells(std::span<const std::byte> bytes,
                                                  std::size_t cell_count) {
    Field2D<double> values(cell_count, 0.0);
    for (std::size_t index = 0; index < cell_count; ++index) {
        values[index] = std::bit_cast<double>(read_little_endian<std::uint64_t>(
            bytes, index * sizeof(std::uint64_t), "cell value"));
    }
    return values;
}

// Decodes one validated chunk into the staged slow state. The switch covers
// every FieldId, so registering a field without a decoder fails to compile.
void decode_chunk(const FieldDescriptor& descriptor,
                  std::span<const std::byte> bytes,
                  std::size_t cell_count,
                  SlowState& staged) {
    switch (descriptor.id) {
    case FieldId::hypsometry_m: {
        Field3D<float> values(hypsometry_layer_count, cell_count, 0.0F);
        std::size_t offset = 0;
        for (std::size_t layer = 0; layer < values.layer_count(); ++layer) {
            for (float& value : values.layer(layer)) {
                value = std::bit_cast<float>(
                    read_little_endian<std::uint32_t>(bytes, offset, "hypsometry value"));
                offset += sizeof(std::uint32_t);
            }
        }
        staged.hypsometry_m = std::move(values);
        return;
    }
    case FieldId::sea_level_m:
        staged.sea_level_m = std::bit_cast<double>(
            read_little_endian<std::uint64_t>(bytes, 0U, "sea level value"));
        return;
    case FieldId::land_surface_temperature_K:
        staged.land_surface_temperature_K = decode_float_cells(bytes, cell_count);
        return;
    case FieldId::land_ground_temperature_K:
        staged.land_ground_temperature_K = decode_float_cells(bytes, cell_count);
        return;
    case FieldId::ocean_mixed_layer_temperature_K:
        staged.ocean_mixed_layer_temperature_K = decode_double_cells(bytes, cell_count);
        return;
    case FieldId::ocean_deep_temperature_K:
        staged.ocean_deep_temperature_K = decode_double_cells(bytes, cell_count);
        return;
    case FieldId::land_snow_water_equivalent_kg_m2:
        staged.land_snow_water_equivalent_kg_m2 = decode_double_cells(bytes, cell_count);
        return;
    case FieldId::sea_ice_mass_kg_m2:
        staged.sea_ice_mass_kg_m2 = decode_double_cells(bytes, cell_count);
        return;
    case FieldId::top_of_atmosphere_insolation_W_m2:
    case FieldId::substep_mean_insolation_W_m2:
    case FieldId::prescribed_precipitation_kg_m2_s:
        break;
    }
    field_error(static_cast<std::uint32_t>(descriptor.id), "field has no persistent decoder");
}

[[nodiscard]] std::span<const std::byte> field_chunk(const ParsedSnapshot& parsed,
                                                     const SnapshotFieldInfo& field) {
    return {parsed.file_bytes.data() + parsed.chunk_area_offset +
                static_cast<std::size_t>(field.byte_offset),
            static_cast<std::size_t>(field.byte_length)};
}

}  // namespace

std::string_view snapshot_engine_version() noexcept { return PLANETSIM_ENGINE_VERSION; }

std::uint32_t mesh_geometry_checksum(const PlanetMesh& mesh) {
    std::vector<std::byte> bytes;
    bytes.reserve(mesh.cell_count() * 3U * sizeof(double));
    for (const auto& cell : mesh.cells()) {
        append_float64(bytes, cell.center_unit.x);
        append_float64(bytes, cell.center_unit.y);
        append_float64(bytes, cell.center_unit.z);
    }
    return crc32c({bytes.data(), bytes.size()});
}

void write_snapshot(const std::filesystem::path& path,
                    const PlanetState& state,
                    SimulationTick tick,
                    std::string parent_snapshot_id) {
    if (tick < 0) {
        throw std::invalid_argument("snapshot tick must be non-negative");
    }
    validate_slow_state(state);

    const auto chunks = encode_slow_state(state);
    const auto manifest = build_manifest(state, tick, parent_snapshot_id, chunks);
    if (manifest.size() > std::numeric_limits<std::uint64_t>::max()) {
        throw std::length_error("snapshot manifest is too large");
    }

    std::vector<std::byte> header(snapshot_magic.begin(), snapshot_magic.end());
    append_little_endian(header, static_cast<std::uint64_t>(manifest.size()));

    auto partial_path = path;
    partial_path += ".partial";
    try {
        {
            std::ofstream output(partial_path, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open snapshot for writing: " +
                                         partial_path.string());
            }
            write_bytes(output, header);
            output.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
            for (const auto& chunk : chunks) {
                write_bytes(output, chunk.bytes);
            }
            output.close();
            if (!output) {
                throw std::runtime_error("failed while writing snapshot: " +
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

std::uint64_t slow_state_hash(const PlanetState& state) {
    validate_slow_state(state);
    std::vector<std::byte> canonical;
    for (const auto& chunk : encode_slow_state(state)) {
        append_little_endian(canonical, static_cast<std::uint32_t>(chunk.descriptor->id));
        append_little_endian(canonical, static_cast<std::uint64_t>(chunk.bytes.size()));
        canonical.insert(canonical.end(), chunk.bytes.begin(), chunk.bytes.end());
    }
    return xxh3_64({canonical.data(), canonical.size()});
}

SnapshotManifest inspect_snapshot(const std::filesystem::path& path) {
    auto parsed = parse_snapshot(path);
    return std::move(parsed.manifest);
}

SnapshotManifest read_snapshot(const std::filesystem::path& path, PlanetState& target_state,
                               const SnapshotMigration& migration) {
    auto parsed = parse_snapshot(path);
    if (parsed.manifest.mesh_level != target_state.mesh().subdivision()) {
        throw std::runtime_error("snapshot mesh_level " +
                                 std::to_string(parsed.manifest.mesh_level) +
                                 " does not match target mesh level " +
                                 std::to_string(target_state.mesh().subdivision()));
    }
    if (parsed.manifest.cell_count != target_state.mesh().cell_count()) {
        throw std::runtime_error("snapshot cell_count " +
                                 std::to_string(parsed.manifest.cell_count) +
                                 " does not match target mesh cell count " +
                                 std::to_string(target_state.mesh().cell_count()));
    }

    if (parsed.manifest.mesh_generator_version != mesh_generator_version) {
        throw std::runtime_error("snapshot mesh_generator_version " +
                                 std::to_string(parsed.manifest.mesh_generator_version) +
                                 " does not match this build's mesh generator version " +
                                 std::to_string(mesh_generator_version));
    }
    if (parsed.manifest.mesh_checksum != mesh_geometry_checksum(target_state.mesh())) {
        throw std::runtime_error(
            "snapshot mesh_checksum does not match the target mesh geometry");
    }

    // Decode into a staged copy so a failure leaves the target untouched.
    SlowState staged = target_state.slow();
    const std::uint32_t file_schema = parsed.manifest.schema_version;
    for (const auto& descriptor : field_registry) {
        if (!descriptor.persistent() || first_schema_version(descriptor.id) > file_schema) {
            continue;
        }
        const auto& info = require_field(parsed.manifest, descriptor.id);
        decode_chunk(descriptor, field_chunk(parsed, info), target_state.mesh().cell_count(),
                     staged);
    }
    if (file_schema < 2U) {
        if (!migration.initialise_schema_2_fields) {
            throw std::runtime_error(
                "snapshot schema_version " + std::to_string(file_schema) +
                " lacks the surface-energy fields and needs the schema 1 -> 2 migration "
                "initialiser (ADR-0007 §4.6)");
        }
        migration.initialise_schema_2_fields(target_state.mesh(), staged);
    } else if (file_schema < 3U) {
        // Schema 2 -> 3 (ADR-0007 §10): the float32 mixed layer widens exactly
        // into its float64 successor.
        const auto& info =
            require_field(parsed.manifest, retired_ocean_mixed_layer_temperature_float32_K);
        const std::size_t cells = target_state.mesh().cell_count();
        const Field2D<float> stored = decode_float_cells(field_chunk(parsed, info), cells);
        staged.ocean_mixed_layer_temperature_K = Field2D<double>(cells, 0.0);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            staged.ocean_mixed_layer_temperature_K[cell] = static_cast<double>(stored[cell]);
        }
    }
    if (file_schema < 4U) {
        if (!migration.initialise_schema_4_fields) {
            throw std::runtime_error(
                "snapshot schema_version " + std::to_string(file_schema) +
                " lacks the cryosphere fields and needs the schema 3 -> 4 migration "
                "initialiser (ADR-0008 §4.6)");
        }
        migration.initialise_schema_4_fields(target_state.mesh(), staged);
    }
    target_state.slow() = std::move(staged);
    return std::move(parsed.manifest);
}

}  // namespace planetsim
