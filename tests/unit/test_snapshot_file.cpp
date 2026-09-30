#include "sim/core/random/counter_rng.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t synthetic_seed = 0x9B97'F4A7'C150'0011ULL;
constexpr planetsim::SimulationTick synthetic_tick = 123'456;
constexpr double test_radius_m = 6'371'000.0;

void populate_synthetic_state(planetsim::PlanetState& state) {
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        std::array<float, planetsim::hypsometry_layer_count> quantiles{};
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            const double unit = planetsim::keyed_random_unit_double(
                synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick,
                static_cast<std::uint32_t>(cell), static_cast<std::uint32_t>(layer));
            quantiles[layer] = static_cast<float>(-8'000.0 + unit * 16'000.0);
        }
        std::sort(quantiles.begin(), quantiles.end());
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            state.slow().hypsometry_m.at(
                layer, planetsim::CellId{static_cast<std::uint32_t>(cell)}) = quantiles[layer];
        }
    }
    const double sea_unit = planetsim::keyed_random_unit_double(
        synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick, 0U, 99U);
    state.slow().sea_level_m = -200.0 + sea_unit * 400.0;
    const auto temperature = [&](std::size_t cell, std::uint32_t sample) {
        return 220.0 + 100.0 * planetsim::keyed_random_unit_double(
                                   synthetic_seed, planetsim::RandomStreamId::validation,
                                   synthetic_tick, static_cast<std::uint32_t>(cell), sample);
    };
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        auto& slow = state.slow();
        slow.land_surface_temperature_K[cell] = static_cast<float>(temperature(cell, 20U));
        slow.land_ground_temperature_K[cell] = static_cast<float>(temperature(cell, 21U));
        slow.ocean_mixed_layer_temperature_K[cell] = temperature(cell, 22U);
        slow.ocean_deep_temperature_K[cell] = temperature(cell, 23U);
    }
}

[[nodiscard]] std::shared_ptr<const planetsim::PlanetMesh> make_mesh(std::uint32_t level) {
    return std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, test_radius_m));
}

[[nodiscard]] std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open test file for reading");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open test file for writing");
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::uint64_t manifest_length(const std::vector<char>& bytes) {
    if (bytes.size() < 16U) {
        throw std::runtime_error("test snapshot header is truncated");
    }
    std::uint64_t length = 0;
    for (std::size_t byte_index = 0; byte_index < sizeof(length); ++byte_index) {
        length |= static_cast<std::uint64_t>(
                      static_cast<unsigned char>(bytes[8U + byte_index]))
                  << (byte_index * 8U);
    }
    return length;
}

[[nodiscard]] std::string extract_manifest(const std::vector<char>& bytes) {
    const auto length = manifest_length(bytes);
    if (length > bytes.size() - 16U) {
        throw std::runtime_error("test snapshot manifest is truncated");
    }
    return {bytes.data() + 16U, static_cast<std::size_t>(length)};
}

void append_u64_little_endian(std::vector<char>& output, std::uint64_t value) {
    for (std::size_t byte_index = 0; byte_index < sizeof(value); ++byte_index) {
        output.push_back(
            static_cast<char>((value >> (byte_index * 8U)) & static_cast<std::uint64_t>(0xFFU)));
    }
}

[[nodiscard]] std::vector<char> replace_manifest(const std::vector<char>& original,
                                                 const std::string& manifest) {
    const std::size_t old_chunk_offset =
        16U + static_cast<std::size_t>(manifest_length(original));
    std::vector<char> result;
    result.reserve(16U + manifest.size() + original.size() - old_chunk_offset);
    result.insert(result.end(), original.begin(), original.begin() + 8);
    append_u64_little_endian(result, manifest.size());
    result.insert(result.end(), manifest.begin(), manifest.end());
    result.insert(result.end(), original.begin() + static_cast<std::ptrdiff_t>(old_chunk_offset),
                  original.end());
    return result;
}

[[nodiscard]] std::vector<char> replace_manifest_once(const std::vector<char>& original,
                                                      std::string_view old_text,
                                                      std::string_view new_text) {
    std::string manifest = extract_manifest(original);
    const std::size_t position = manifest.find(old_text);
    if (position == std::string::npos) {
        throw std::runtime_error("test manifest text was not found");
    }
    manifest.replace(position, old_text.size(), new_text);
    return replace_manifest(original, manifest);
}

void expect_read_failure(planetsim::test::Context& test,
                         const std::filesystem::path& path,
                         const std::shared_ptr<const planetsim::PlanetMesh>& mesh,
                         std::string_view expected_message) {
    planetsim::PlanetState target(mesh);
    target.slow().sea_level_m = 987.0;
    target.slow().hypsometry_m.at(0U, planetsim::CellId{0}) = 654.0F;
    try {
        static_cast<void>(planetsim::read_snapshot(path, target));
        PLANETSIM_EXPECT(test, false);
    } catch (const std::exception& exception) {
        const bool message_matches =
            expected_message.empty() ||
            std::string_view{exception.what()}.find(expected_message) != std::string_view::npos;
        if (!message_matches) {
            std::cerr << "expected snapshot error containing '" << expected_message
                      << "', got '" << exception.what() << "'\n";
        }
        PLANETSIM_EXPECT(test, message_matches);
    }
    PLANETSIM_EXPECT_NEAR(test, target.slow().sea_level_m, 987.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, target.slow().hypsometry_m.at(0U, planetsim::CellId{0}), 654.0,
                          0.0);
}

void expect_states_equal(planetsim::test::Context& test,
                         const planetsim::PlanetState& first,
                         const planetsim::PlanetState& second) {
    PLANETSIM_EXPECT(test, first.slow().hypsometry_m.layer_count() ==
                               second.slow().hypsometry_m.layer_count());
    PLANETSIM_EXPECT(test, first.slow().hypsometry_m.cell_count() ==
                               second.slow().hypsometry_m.cell_count());
    for (std::size_t layer = 0; layer < first.slow().hypsometry_m.layer_count(); ++layer) {
        const auto first_values = first.slow().hypsometry_m.layer(layer);
        const auto second_values = second.slow().hypsometry_m.layer(layer);
        PLANETSIM_EXPECT(test, std::equal(first_values.begin(), first_values.end(),
                                          second_values.begin(), second_values.end()));
    }
    PLANETSIM_EXPECT(test,
                     std::bit_cast<std::uint64_t>(first.slow().sea_level_m) ==
                         std::bit_cast<std::uint64_t>(second.slow().sea_level_m));
    const auto same_float = [](const auto& a, const auto& b) {
        return a.size() == b.size() &&
               std::equal(a.values().begin(), a.values().end(), b.values().begin(),
                          [](float x, float y) {
                              return std::bit_cast<std::uint32_t>(x) ==
                                     std::bit_cast<std::uint32_t>(y);
                          });
    };
    PLANETSIM_EXPECT(test, same_float(first.slow().land_surface_temperature_K,
                                      second.slow().land_surface_temperature_K));
    PLANETSIM_EXPECT(test, same_float(first.slow().land_ground_temperature_K,
                                      second.slow().land_ground_temperature_K));
    const auto same_double = [](const auto& a, const auto& b) {
        return a.size() == b.size() &&
               std::equal(a.values().begin(), a.values().end(), b.values().begin(),
                          [](double x, double y) {
                              return std::bit_cast<std::uint64_t>(x) ==
                                     std::bit_cast<std::uint64_t>(y);
                          });
    };
    PLANETSIM_EXPECT(test, same_double(first.slow().ocean_mixed_layer_temperature_K,
                                       second.slow().ocean_mixed_layer_temperature_K));
    PLANETSIM_EXPECT(test, same_double(first.slow().ocean_deep_temperature_K,
                                       second.slow().ocean_deep_temperature_K));
}

}  // namespace

int main() {
    planetsim::test::Context test;

    // Unique per run, so concurrent test runs from different build trees
    // cannot interfere.
    const auto temporary_directory =
        std::filesystem::temp_directory_path() /
        ("planetsim_snapshot_file_test_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(temporary_directory);
    std::filesystem::create_directories(temporary_directory);

    const auto first_path = temporary_directory / "canonical_first.psnap";
    const auto second_path = temporary_directory / "canonical_second.psnap";
    const auto level_zero_mesh = make_mesh(0U);
    planetsim::PlanetState first(level_zero_mesh);
    planetsim::PlanetState second(level_zero_mesh);
    populate_synthetic_state(first);
    populate_synthetic_state(second);
    first.open_fast_state();
    first.forcing().top_of_atmosphere_insolation_W_m2[0] = 777.0F;

    planetsim::write_snapshot(first_path, first, synthetic_tick, "parent-0");
    planetsim::write_snapshot(second_path, second, synthetic_tick, "parent-0");
    const auto canonical_bytes = read_file(first_path);
    PLANETSIM_EXPECT(test, canonical_bytes == read_file(second_path));

    const auto inspected = planetsim::inspect_snapshot(first_path);
    PLANETSIM_EXPECT(test, inspected.format == "PSNAPv1");
    PLANETSIM_EXPECT(test,
                     inspected.schema_version == planetsim::persistent_snapshot_schema_version);
    PLANETSIM_EXPECT(test, !inspected.engine_version.empty());
    PLANETSIM_EXPECT(test, inspected.engine_version == planetsim::snapshot_engine_version());
    PLANETSIM_EXPECT(test, inspected.mesh_generator_version == planetsim::mesh_generator_version);
    PLANETSIM_EXPECT(test,
                     inspected.mesh_checksum == planetsim::mesh_geometry_checksum(*level_zero_mesh));
    PLANETSIM_EXPECT(test, !std::filesystem::exists(first_path.string() + ".partial"));
    PLANETSIM_EXPECT(test, inspected.tick == synthetic_tick);
    PLANETSIM_EXPECT(test, inspected.mesh_level == 0U);
    PLANETSIM_EXPECT(test, inspected.cell_count == 12U);
    PLANETSIM_EXPECT(test, inspected.parent_snapshot_id == "parent-0");
    PLANETSIM_EXPECT(test, inspected.fields.size() == 6U);
    if (inspected.fields.size() == 6U) {
        PLANETSIM_EXPECT(test, inspected.fields[0].field_id ==
                                   static_cast<std::uint32_t>(planetsim::FieldId::hypsometry_m));
        PLANETSIM_EXPECT(test, inspected.fields[1].field_id ==
                                   static_cast<std::uint32_t>(planetsim::FieldId::sea_level_m));
        PLANETSIM_EXPECT(test, inspected.fields[4].field_id ==
                                   static_cast<std::uint32_t>(
                                       planetsim::FieldId::ocean_deep_temperature_K));
        PLANETSIM_EXPECT(test, inspected.fields[5].field_id ==
                                   static_cast<std::uint32_t>(
                                       planetsim::FieldId::ocean_mixed_layer_temperature_K));
        PLANETSIM_EXPECT(test, inspected.fields[5].dtype == "float64");
    }

    for (const std::uint32_t level : {0U, 4U, 6U}) {
        const auto mesh = make_mesh(level);
        planetsim::PlanetState original(mesh);
        planetsim::PlanetState loaded(mesh);
        populate_synthetic_state(original);
        const auto input_path =
            temporary_directory / ("round_trip_" + std::to_string(level) + "_input.psnap");
        const auto output_path =
            temporary_directory / ("round_trip_" + std::to_string(level) + "_output.psnap");

        const auto write_start = std::chrono::steady_clock::now();
        planetsim::write_snapshot(input_path, original, synthetic_tick, "round-trip-parent");
        const auto write_finish = std::chrono::steady_clock::now();
        const auto loaded_manifest = planetsim::read_snapshot(input_path, loaded);
        expect_states_equal(test, original, loaded);
        PLANETSIM_EXPECT(test, !loaded.has_fast_state());
        PLANETSIM_EXPECT_NEAR(test, loaded.forcing().incident_solar_flux_W_m2, 0.0, 0.0);

        planetsim::write_snapshot(output_path, loaded, loaded_manifest.tick,
                                  loaded_manifest.parent_snapshot_id);
        PLANETSIM_EXPECT(test, read_file(input_path) == read_file(output_path));

        if (level == 6U) {
            const double write_time_ms =
                std::chrono::duration<double, std::milli>(write_finish - write_start).count();
            std::cout << "l6_snapshot_size_bytes: " << std::filesystem::file_size(input_path)
                      << '\n'
                      << "l6_snapshot_write_time_ms: " << write_time_ms << '\n';
        }
    }

    const std::size_t chunk_area_offset =
        16U + static_cast<std::size_t>(manifest_length(canonical_bytes));
    for (const auto& field : inspected.fields) {
        auto corrupted = canonical_bytes;
        const std::size_t corrupt_offset =
            chunk_area_offset + static_cast<std::size_t>(field.byte_offset) +
            static_cast<std::size_t>(field.byte_length / 2U);
        corrupted[corrupt_offset] =
            static_cast<char>(static_cast<unsigned char>(corrupted[corrupt_offset]) ^ 0x01U);
        const auto corrupt_path =
            temporary_directory / ("corrupt_chunk_" + std::to_string(field.field_id) + ".psnap");
        write_file(corrupt_path, corrupted);
        expect_read_failure(test, corrupt_path, level_zero_mesh,
                            "field_id " + std::to_string(field.field_id));
    }

    const auto invalid_path = temporary_directory / "invalid.psnap";

    write_file(invalid_path, {});
    expect_read_failure(test, invalid_path, level_zero_mesh, "header");

    auto corrupted_magic = canonical_bytes;
    corrupted_magic[0] = 'X';
    write_file(invalid_path, corrupted_magic);
    expect_read_failure(test, invalid_path, level_zero_mesh, "magic");

    auto corrupted_length = canonical_bytes;
    for (std::size_t index = 8U; index < 16U; ++index) {
        corrupted_length[index] = static_cast<char>(0xFF);
    }
    write_file(invalid_path, corrupted_length);
    expect_read_failure(test, invalid_path, level_zero_mesh, "manifest is truncated");

    auto truncated = canonical_bytes;
    truncated.pop_back();
    write_file(invalid_path, truncated);
    expect_read_failure(test, invalid_path, level_zero_mesh, "field_id 196613");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"format\"", "\"xormat\""));
    expect_read_failure(test, invalid_path, level_zero_mesh, "expected key format");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"schema_version\":3",
                                     "\"schema_version\":9"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "schema_version");

    // A file that claims schema 1 may not carry fields introduced in schema 2.
    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"schema_version\":3",
                                     "\"schema_version\":1"));
    expect_read_failure(test, invalid_path, level_zero_mesh,
                        "does not exist in snapshot schema_version 1");

    // Nor may a schema 2 file carry the float64 mixed layer of schema 3.
    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"schema_version\":3",
                                     "\"schema_version\":2"));
    expect_read_failure(test, invalid_path, level_zero_mesh,
                        "field_id 196613: field does not exist in snapshot schema_version 2");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"mesh_level\":0",
                                     "\"mesh_level\":1"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "mesh_level");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"cell_count\":12",
                                     "\"cell_count\":13"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "field_id 131073");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"field_id\":131073",
                                     "\"field_id\":999999"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "field_id 999999");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"field_id\":131074",
                                     "\"field_id\":131073"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "duplicate field");

    std::string missing_manifest = extract_manifest(canonical_bytes);
    const std::size_t fields_marker = missing_manifest.find("\"fields\":[");
    const std::size_t first_field =
        fields_marker == std::string::npos ? std::string::npos : fields_marker + 10U;
    const std::size_t second_field =
        first_field == std::string::npos
            ? std::string::npos
            : missing_manifest.find("},{\"field_id\":", first_field);
    PLANETSIM_EXPECT(test, first_field != std::string::npos);
    PLANETSIM_EXPECT(test, second_field != std::string::npos);
    if (first_field != std::string::npos && second_field != std::string::npos) {
        missing_manifest.erase(first_field, second_field + 2U - first_field);
        write_file(invalid_path, replace_manifest(canonical_bytes, missing_manifest));
        expect_read_failure(test, invalid_path, level_zero_mesh, "field_id 131073");
    }

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"compression\":\"none\"",
                                     "\"compression\":\"zstd\""));
    expect_read_failure(test, invalid_path, level_zero_mesh, "field_id 131073");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"partition\":\"slow\"",
                                     "\"partition\":\"fast\""));
    expect_read_failure(test, invalid_path, level_zero_mesh, "partition");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"layout\":\"cell_layers\"",
                                     "\"layout\":\"cell_layerx\""));
    expect_read_failure(test, invalid_path, level_zero_mesh, "layout");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"dtype\":\"float32\"",
                                     "\"dtype\":\"float64\""));
    expect_read_failure(test, invalid_path, level_zero_mesh, "dtype");

    write_file(invalid_path,
               replace_manifest_once(canonical_bytes, "\"layers\":9", "\"layers\":8"));
    expect_read_failure(test, invalid_path, level_zero_mesh, "layer count");

    write_file(invalid_path, canonical_bytes);
    expect_read_failure(test, invalid_path, make_mesh(1U), "mesh_level");

    // Mesh identity: a snapshot written against different mesh geometry at the
    // same level and cell count must not load.
    write_file(invalid_path,
               replace_manifest_once(
                   canonical_bytes,
                   "\"mesh_generator_version\":" +
                       std::to_string(planetsim::mesh_generator_version),
                   "\"mesh_generator_version\":" +
                       std::to_string(planetsim::mesh_generator_version + 1U)));
    expect_read_failure(test, invalid_path, level_zero_mesh, "mesh_generator_version");

    write_file(invalid_path,
               replace_manifest_once(
                   canonical_bytes, "\"mesh_checksum\":" + std::to_string(inspected.mesh_checksum),
                   "\"mesh_checksum\":" + std::to_string(inspected.mesh_checksum ^ 1U)));
    expect_read_failure(test, invalid_path, level_zero_mesh, "mesh_checksum");

    // The writer rejects a slow state that does not match the mesh, and a
    // failed write leaves an existing snapshot and no partial file behind.
    {
        planetsim::PlanetState malformed(level_zero_mesh);
        malformed.slow().hypsometry_m =
            planetsim::Field3D<float>(planetsim::hypsometry_layer_count, 11U, 0.0F);
        bool rejected = false;
        try {
            planetsim::write_snapshot(first_path, malformed, synthetic_tick);
        } catch (const std::invalid_argument&) {
            rejected = true;
        } catch (const std::runtime_error& exception) {
            rejected = std::string_view{exception.what()}.find("field_id 131073") !=
                       std::string_view::npos;
        }
        PLANETSIM_EXPECT(test, rejected);
        PLANETSIM_EXPECT(test, read_file(first_path) == canonical_bytes);
        PLANETSIM_EXPECT(test, !std::filesystem::exists(first_path.string() + ".partial"));
    }

    std::filesystem::remove_all(temporary_directory);
    return test.result();
}
