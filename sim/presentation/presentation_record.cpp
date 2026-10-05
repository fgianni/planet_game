#include "sim/presentation/presentation_record.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace planetsim::presentation {
namespace {
constexpr std::array<char, 8> magic_v1{{'P', 'F', 'R', 'A', 'M', 'E', '0', '1'}};
constexpr std::array<char, 8> magic_v2{{'P', 'F', 'R', 'A', 'M', 'E', '0', '2'}};

template <typename T> void write(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
template <typename T> void read(std::istream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!stream) throw std::runtime_error("truncated presentation record");
}
template <typename T> void write_vector(std::ostream& stream, const std::vector<T>& values) {
    const auto size = static_cast<std::uint64_t>(values.size());
    write(stream, size);
    stream.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(size * sizeof(T)));
}
template <typename T> void read_vector(std::istream& stream, std::vector<T>& values) {
    std::uint64_t size = 0;
    read(stream, size);
    if (size > 100'000'000U) throw std::runtime_error("presentation record vector is too large");
    values.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(size * sizeof(T)));
    if (!stream) throw std::runtime_error("truncated presentation record vector");
}

void write_frame(std::ostream& stream, const StateSnapshot& frame) {
    write(stream, frame.schema_version); write(stream, frame.simulation_tick);
    write(stream, frame.simulation_time_s); write(stream, frame.rotation_angle_rad);
    write(stream, frame.orbital_phase_rad); write(stream, frame.eccentric_anomaly_rad);
    write(stream, frame.true_anomaly_rad); write(stream, frame.solar_longitude_rad);
    write(stream, frame.solar_declination_rad); write(stream, frame.orbital_distance_m);
    write(stream, frame.incident_solar_flux_W_m2);
    write(stream, frame.sun_direction_inertial_unit); write(stream, frame.sun_direction_body_unit);
    write(stream, frame.top_of_atmosphere_insolation_field_id);
    for (const auto* values : {&frame.top_of_atmosphere_insolation_W_m2, &frame.surface_temperature_K,
             &frame.land_snow_water_equivalent_kg_m2, &frame.sea_ice_mass_kg_m2,
             &frame.climatology_surface_temperature_mean_K,
             &frame.climatology_surface_temperature_variance_K2}) write_vector(stream, *values);
}
[[nodiscard]] StateSnapshot read_frame(std::istream& stream) {
    StateSnapshot frame;
    read(stream, frame.schema_version); read(stream, frame.simulation_tick);
    read(stream, frame.simulation_time_s); read(stream, frame.rotation_angle_rad);
    read(stream, frame.orbital_phase_rad); read(stream, frame.eccentric_anomaly_rad);
    read(stream, frame.true_anomaly_rad); read(stream, frame.solar_longitude_rad);
    read(stream, frame.solar_declination_rad); read(stream, frame.orbital_distance_m);
    read(stream, frame.incident_solar_flux_W_m2);
    read(stream, frame.sun_direction_inertial_unit); read(stream, frame.sun_direction_body_unit);
    read(stream, frame.top_of_atmosphere_insolation_field_id);
    for (auto* values : {&frame.top_of_atmosphere_insolation_W_m2, &frame.surface_temperature_K,
             &frame.land_snow_water_equivalent_kg_m2, &frame.sea_ice_mass_kg_m2,
             &frame.climatology_surface_temperature_mean_K,
             &frame.climatology_surface_temperature_variance_K2}) read_vector(stream, *values);
    return frame;
}

void write_terrain(std::ostream& stream, const TerrainSnapshot& terrain) {
    write(stream, terrain.schema_version); write(stream, terrain.sea_level_m);
    write(stream, terrain.land_area_fraction); write(stream, terrain.plate_count);
    write(stream, terrain.drainage_outlet_count); write(stream, terrain.drainage_basin_count);
    write(stream, terrain.drainage_depression_count); write(stream, terrain.largest_catchment_area_m2);
    write_vector(stream, terrain.mean_elevation_m); write_vector(stream, terrain.land_fraction);
    write_vector(stream, terrain.plate_id); write_vector(stream, terrain.crust_type);
    write_vector(stream, terrain.crust_age_myr); write_vector(stream, terrain.boundary_class);
    write_vector(stream, terrain.downstream); write_vector(stream, terrain.basin_id);
    write_vector(stream, terrain.depression_id); write_vector(stream, terrain.catchment_area_m2);
}
[[nodiscard]] TerrainSnapshot read_terrain(std::istream& stream) {
    TerrainSnapshot terrain;
    read(stream, terrain.schema_version); read(stream, terrain.sea_level_m);
    read(stream, terrain.land_area_fraction); read(stream, terrain.plate_count);
    read(stream, terrain.drainage_outlet_count); read(stream, terrain.drainage_basin_count);
    read(stream, terrain.drainage_depression_count); read(stream, terrain.largest_catchment_area_m2);
    read_vector(stream, terrain.mean_elevation_m); read_vector(stream, terrain.land_fraction);
    read_vector(stream, terrain.plate_id); read_vector(stream, terrain.crust_type);
    read_vector(stream, terrain.crust_age_myr); read_vector(stream, terrain.boundary_class);
    read_vector(stream, terrain.downstream); read_vector(stream, terrain.basin_id);
    read_vector(stream, terrain.depression_id); read_vector(stream, terrain.catchment_area_m2);
    return terrain;
}

void require_record_is_playback_ready(const PresentationRecord& record) {
    if (record.schema_version != presentation_record_schema_version ||
        !record.is_playback_ready()) {
        throw std::invalid_argument("PFRAME02 requires a mesh, terrain and at least one frame");
    }
    if (!(record.mesh->radius_m > 0.0)) {
        throw std::invalid_argument("PFRAME02 mesh radius must be positive");
    }
    const std::size_t cells = record.terrain->mean_elevation_m.size();
    if (cells == 0U || record.terrain->land_fraction.size() != cells) {
        throw std::invalid_argument("PFRAME02 terrain must have one elevation and land fraction per cell");
    }
    for (const StateSnapshot& frame : record.frames) {
        if (frame.top_of_atmosphere_insolation_W_m2.size() != cells) {
            throw std::invalid_argument("PFRAME02 frame insolation does not match terrain cells");
        }
    }
}

[[nodiscard]] std::vector<StateSnapshot> read_frames(std::istream& stream) {
    std::uint64_t count = 0;
    read(stream, count);
    if (count > 10'000'000U) throw std::runtime_error("presentation record has too many frames");
    std::vector<StateSnapshot> frames; frames.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) frames.push_back(read_frame(stream));
    return frames;
}
}  // namespace

void write_presentation_record(const std::filesystem::path& path, const PresentationRecord& record) {
    require_record_is_playback_ready(record);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot open presentation record for writing: " + path.string());
    stream.write(magic_v2.data(), static_cast<std::streamsize>(magic_v2.size()));
    write(stream, record.schema_version); write(stream, *record.mesh); write_terrain(stream, *record.terrain);
    const auto count = static_cast<std::uint64_t>(record.frames.size()); write(stream, count);
    for (const auto& frame : record.frames) write_frame(stream, frame);
    if (!stream) throw std::runtime_error("failed writing presentation record: " + path.string());
}

void write_presentation_record(const std::filesystem::path& path,
                               const std::vector<StateSnapshot>& frames) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot open presentation record for writing: " + path.string());
    stream.write(magic_v1.data(), static_cast<std::streamsize>(magic_v1.size()));
    const auto count = static_cast<std::uint64_t>(frames.size()); write(stream, count);
    for (const auto& frame : frames) write_frame(stream, frame);
    if (!stream) throw std::runtime_error("failed writing presentation record: " + path.string());
}

PresentationRecord read_presentation_record(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open presentation record: " + path.string());
    std::array<char, magic_v1.size()> magic{}; stream.read(magic.data(), magic.size());
    if (magic == magic_v1) {
        PresentationRecord record;
        record.schema_version = 1U;
        record.frames = read_frames(stream);
        return record;
    }
    if (magic != magic_v2) throw std::runtime_error("invalid presentation record magic");
    PresentationRecord record;
    read(stream, record.schema_version);
    if (record.schema_version != presentation_record_schema_version) {
        throw std::runtime_error("unsupported presentation record schema");
    }
    PresentationMeshDescriptor mesh;
    read(stream, mesh);
    record.mesh = mesh;
    record.terrain = read_terrain(stream);
    record.frames = read_frames(stream);
    require_record_is_playback_ready(record);
    return record;
}

}  // namespace planetsim::presentation
