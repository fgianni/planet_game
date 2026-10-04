#include "sim/presentation/presentation_record.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace planetsim::presentation {
namespace {
constexpr std::array<char, 8> magic{{'P', 'F', 'R', 'A', 'M', 'E', '0', '1'}};

template <typename T> void write(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
template <typename T> void read(std::istream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!stream) throw std::runtime_error("truncated presentation record");
}
void write_vector(std::ostream& stream, const std::vector<float>& values) {
    const auto size = static_cast<std::uint64_t>(values.size());
    write(stream, size);
    stream.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(size * sizeof(float)));
}
void read_vector(std::istream& stream, std::vector<float>& values) {
    std::uint64_t size = 0;
    read(stream, size);
    if (size > 100'000'000U) throw std::runtime_error("presentation record vector is too large");
    values.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(size * sizeof(float)));
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
}  // namespace

void write_presentation_record(const std::filesystem::path& path,
                               const std::vector<StateSnapshot>& frames) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot open presentation record for writing: " + path.string());
    stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    const auto count = static_cast<std::uint64_t>(frames.size()); write(stream, count);
    for (const auto& frame : frames) write_frame(stream, frame);
    if (!stream) throw std::runtime_error("failed writing presentation record: " + path.string());
}

std::vector<StateSnapshot> read_presentation_record(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open presentation record: " + path.string());
    std::array<char, magic.size()> actual{}; stream.read(actual.data(), actual.size());
    if (actual != magic) throw std::runtime_error("invalid presentation record magic");
    std::uint64_t count = 0; read(stream, count);
    if (count > 10'000'000U) throw std::runtime_error("presentation record has too many frames");
    std::vector<StateSnapshot> frames; frames.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) frames.push_back(read_frame(stream));
    return frames;
}
}  // namespace planetsim::presentation
