#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct MeshOptions {
    std::uint32_t subdivision = 6;
    double radius_m = 6'371'000.0;
};

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  planet_cli mesh [--subdivision LEVEL] [--radius METRES]\n";
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

[[nodiscard]] double parse_radius(std::string_view text) {
    double parsed = 0.0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid radius: " + std::string(text));
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
            options.radius_m = parse_radius(arguments[index]);
        } else {
            throw std::invalid_argument("unknown mesh option: " + std::string(argument));
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
              << "non_reciprocal_neighbor_count: "
              << diagnostics.non_reciprocal_neighbor_count << '\n'
              << "adjacency_edge_mismatch_count: "
              << diagnostics.adjacency_edge_mismatch_count << '\n'
              << "invalid_vertex_index_count: "
              << diagnostics.invalid_vertex_index_count << '\n'
              << "non_finite_geometry_count: "
              << diagnostics.non_finite_geometry_count << '\n'
              << "topology_valid: " << diagnostics.topology_valid() << '\n'
              << "construction_time_ms: " << construction_time_ms << '\n';

    constexpr double area_error_limit = 5.0e-14;
    return diagnostics.topology_valid() &&
                   diagnostics.relative_area_error <= area_error_limit
               ? 0
               : 2;
}

}  // namespace

int main(int argument_count, char** arguments) {
    try {
        if (argument_count == 2 && std::string_view{arguments[1]} == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument_count < 2 || std::string_view{arguments[1]} != "mesh") {
            print_usage(std::cerr);
            return 1;
        }
        return run_mesh(parse_mesh_options(argument_count, arguments));
    } catch (const std::exception& exception) {
        std::cerr << "planet_cli: " << exception.what() << '\n';
        return 1;
    }
}
