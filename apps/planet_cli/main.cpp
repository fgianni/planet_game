#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"
#include "sim/planet/orbit/solar_diagnostics.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct MeshOptions {
    std::uint32_t subdivision = 6;
    double radius_m = 6'371'000.0;
};

struct SolarOptions {
    std::uint32_t subdivision = 5;
    double radius_m = 6'371'000.0;
    double time_days = 0.0;
};

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  planet_cli mesh [--subdivision LEVEL] [--radius METRES]\n"
           << "  planet_cli solar [--subdivision LEVEL] [--radius METRES]"
              " [--time-days DAYS]\n";
}

[[nodiscard]] std::uint32_t parse_subdivision(std::string_view text) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid subdivision level: " + std::string(text));
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] double parse_double(std::string_view text, std::string_view description) {
    double parsed = 0.0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string(description) + ": " +
                                    std::string(text));
    }
    return parsed;
}

[[nodiscard]] MeshOptions parse_mesh_options(int argument_count, char** arguments) {
    MeshOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--subdivision") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--subdivision requires a value");
            }
            options.subdivision = parse_subdivision(arguments[index]);
        } else if (argument == "--radius") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--radius requires a value");
            }
            options.radius_m = parse_double(arguments[index], "radius");
        } else {
            throw std::invalid_argument("unknown mesh option: " + std::string(argument));
        }
    }
    return options;
}

[[nodiscard]] SolarOptions parse_solar_options(int argument_count, char** arguments) {
    SolarOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--subdivision") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--subdivision requires a value");
            }
            options.subdivision = parse_subdivision(arguments[index]);
        } else if (argument == "--radius") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--radius requires a value");
            }
            options.radius_m = parse_double(arguments[index], "radius");
        } else if (argument == "--time-days") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--time-days requires a value");
            }
            options.time_days = parse_double(arguments[index], "time in days");
        } else {
            throw std::invalid_argument("unknown solar option: " + std::string(argument));
        }
    }
    return options;
}

int run_mesh(const MeshOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    const auto mesh = planetsim::make_icosphere(options.subdivision, options.radius_m);
    const auto finish = std::chrono::steady_clock::now();
    const auto diagnostics = planetsim::analyze_mesh(mesh);
    const double construction_time_ms =
        std::chrono::duration<double, std::milli>(finish - start).count();

    std::cout << std::setprecision(17) << std::boolalpha
              << "subdivision: " << diagnostics.subdivision << '\n'
              << "cell_count: " << diagnostics.cell_count << '\n'
              << "vertex_count: " << diagnostics.vertex_count << '\n'
              << "radius_m: " << mesh.radius_m() << '\n'
              << "total_area_m2: " << diagnostics.total_area_m2 << '\n'
              << "expected_area_m2: " << diagnostics.expected_area_m2 << '\n'
              << "relative_area_error: " << diagnostics.relative_area_error << '\n'
              << "min_cell_area_m2: " << diagnostics.min_cell_area_m2 << '\n'
              << "max_cell_area_m2: " << diagnostics.max_cell_area_m2 << '\n'
              << "min_edge_length_m: " << diagnostics.min_edge_length_m << '\n'
              << "max_edge_length_m: " << diagnostics.max_edge_length_m << '\n'
              << "max_center_norm_error: " << diagnostics.max_center_norm_error << '\n'
              << "invalid_neighbor_count: " << diagnostics.invalid_neighbor_count << '\n'
              << "non_reciprocal_neighbor_count: " << diagnostics.non_reciprocal_neighbor_count
              << '\n'
              << "adjacency_edge_mismatch_count: " << diagnostics.adjacency_edge_mismatch_count
              << '\n'
              << "invalid_vertex_index_count: " << diagnostics.invalid_vertex_index_count << '\n'
              << "non_finite_geometry_count: " << diagnostics.non_finite_geometry_count << '\n'
              << "topology_valid: " << diagnostics.topology_valid() << '\n'
              << "construction_time_ms: " << construction_time_ms << '\n';

    constexpr double area_error_limit = 5.0e-14;
    return diagnostics.topology_valid() && diagnostics.relative_area_error <= area_error_limit ? 0
                                                                                               : 2;
}

int run_solar(const SolarOptions& options) {
    auto parameters = planetsim::PlanetParameters::earth_development();
    parameters.mesh_subdivision = options.subdivision;
    parameters.radius_m = options.radius_m;
    parameters.validate();
    if (!std::isfinite(options.time_days) || options.time_days < 0.0) {
        throw std::invalid_argument("time in days must be finite and non-negative");
    }

    const auto start = std::chrono::steady_clock::now();
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(parameters.mesh_subdivision, parameters.radius_m));
    planetsim::PlanetState state(mesh);
    constexpr double seconds_per_day = 86'400.0;
    const double simulation_time_s = options.time_days * seconds_per_day;
    planetsim::update_solar_forcing(state, parameters, simulation_time_s);
    const auto finish = std::chrono::steady_clock::now();

    const auto diagnostics = planetsim::analyze_solar_forcing(state);
    const auto& orbit = state.forcing().orbit;
    const double evaluation_time_ms =
        std::chrono::duration<double, std::milli>(finish - start).count();
    const double radians_to_degrees = 180.0 / std::numbers::pi_v<double>;

    std::cout << std::setprecision(17) << std::boolalpha
              << "subdivision: " << parameters.mesh_subdivision << '\n'
              << "cell_count: " << diagnostics.cell_count << '\n'
              << "simulation_time_s: " << simulation_time_s << '\n'
              << "rotation_angle_rad: " << orbit.rotation_angle_rad << '\n'
              << "orbital_phase_rad: " << orbit.orbital_phase_rad << '\n'
              << "eccentric_anomaly_rad: " << orbit.eccentric_anomaly_rad << '\n'
              << "true_anomaly_rad: " << orbit.true_anomaly_rad << '\n'
              << "solar_longitude_rad: " << orbit.solar_longitude_rad << '\n'
              << "solar_declination_deg: " << orbit.solar_declination_rad * radians_to_degrees
              << '\n'
              << "orbital_distance_m: " << orbit.orbital_distance_m << '\n'
              << "sun_direction_body_x: " << orbit.sun_direction_body_unit.x << '\n'
              << "sun_direction_body_y: " << orbit.sun_direction_body_unit.y << '\n'
              << "sun_direction_body_z: " << orbit.sun_direction_body_unit.z << '\n'
              << "incident_solar_flux_W_m2: " << state.forcing().incident_solar_flux_W_m2 << '\n'
              << "illuminated_cell_count: " << diagnostics.illuminated_cell_count << '\n'
              << "night_cell_count: " << diagnostics.night_cell_count << '\n'
              << "min_insolation_W_m2: " << diagnostics.min_insolation_W_m2 << '\n'
              << "max_insolation_W_m2: " << diagnostics.max_insolation_W_m2 << '\n'
              << "global_mean_insolation_W_m2: " << diagnostics.global_mean_insolation_W_m2 << '\n'
              << "expected_global_mean_insolation_W_m2: "
              << diagnostics.expected_global_mean_insolation_W_m2 << '\n'
              << "relative_global_mean_error: " << diagnostics.relative_global_mean_error << '\n'
              << "total_incoming_power_W: " << diagnostics.total_incoming_power_W << '\n'
              << "expected_incoming_power_W: " << diagnostics.expected_incoming_power_W << '\n'
              << "night_side_nonzero_count: " << diagnostics.night_side_nonzero_count << '\n'
              << "out_of_range_count: " << diagnostics.out_of_range_count << '\n'
              << "non_finite_count: " << diagnostics.non_finite_count << '\n'
              << "solar_forcing_valid: " << diagnostics.forcing_valid() << '\n'
              << "evaluation_time_ms: " << evaluation_time_ms << '\n';

    return diagnostics.forcing_valid() ? 0 : 2;
}

}  // namespace

int main(int argument_count, char** arguments) {
    try {
        if (argument_count == 2 && std::string_view{arguments[1]} == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument_count < 2) {
            print_usage(std::cerr);
            return 1;
        }
        const std::string_view command{arguments[1]};
        if (command == "mesh") {
            return run_mesh(parse_mesh_options(argument_count, arguments));
        }
        if (command == "solar") {
            return run_solar(parse_solar_options(argument_count, arguments));
        }
        print_usage(std::cerr);
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "planet_cli: " << exception.what() << '\n';
        return 1;
    }
}
