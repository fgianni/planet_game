#include "sim/core/scheduler/simulation_clock.hpp"
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
#include <utility>
#include <vector>

namespace {

struct MeshOptions {
    std::uint32_t subdivision = 6;
    double radius_m = 6'371'000.0;
    bool benchmark_layout = false;
    std::uint32_t benchmark_iterations = 100;
};

struct SolarOptions {
    std::uint32_t subdivision = 5;
    double radius_m = 6'371'000.0;
    double time_days = 0.0;
};

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  planet_cli mesh [--subdivision LEVEL] [--radius METRES]"
              " [--benchmark-layout] [--benchmark-iterations COUNT]\n"
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
        } else if (argument == "--benchmark-layout") {
            options.benchmark_layout = true;
        } else if (argument == "--benchmark-iterations") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--benchmark-iterations requires a value");
            }
            options.benchmark_iterations = parse_subdivision(arguments[index]);
            if (options.benchmark_iterations == 0U) {
                throw std::invalid_argument("benchmark iterations must be positive");
            }
        } else {
            throw std::invalid_argument("unknown mesh option: " + std::string(argument));
        }
    }
    return options;
}

struct LayoutBenchmarkResult {
    double ordered_time_ms = 0.0;
    double naive_time_ms = 0.0;
    double naive_to_ordered_ratio = 0.0;
    double checksum = 0.0;
};

struct LayoutKernelResult {
    double time_ms = 0.0;
    std::vector<float> values;
};

[[nodiscard]] LayoutKernelResult run_layout_kernel(const std::vector<std::uint32_t>& offsets,
                                                   const std::vector<std::uint32_t>& neighbors,
                                                   std::vector<float> values,
                                                   std::uint32_t iterations) {
    std::vector<float> scratch(values.size(), 0.0F);
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        const float bias = static_cast<float>(iteration % 7U) * 1.0e-7F;
        for (std::size_t cell = 0; cell < values.size(); ++cell) {
            float neighbor_sum = 0.0F;
            for (std::uint32_t edge = offsets[cell]; edge < offsets[cell + 1U]; ++edge) {
                neighbor_sum += values[neighbors[edge]];
            }
            const float divisor = static_cast<float>(offsets[cell + 1U] - offsets[cell]);
            scratch[cell] = 0.5F * values[cell] + 0.5F * neighbor_sum / divisor + bias;
        }
        values.swap(scratch);
    }
    const auto finish = std::chrono::steady_clock::now();
    return {std::chrono::duration<double, std::milli>(finish - start).count(), std::move(values)};
}

[[nodiscard]] LayoutBenchmarkResult benchmark_layout(const planetsim::PlanetMesh& mesh,
                                                     std::uint32_t iterations) {
    const std::size_t cell_count = mesh.cell_count();
    std::vector<planetsim::CellId> source_to_cell(cell_count);
    for (const auto& cell : mesh.cells()) {
        source_to_cell[cell.source_vertex_index] = cell.id;
    }

    std::vector<std::uint32_t> ordered_offsets(cell_count + 1U, 0U);
    std::vector<std::uint32_t> naive_offsets(cell_count + 1U, 0U);
    std::vector<std::uint32_t> ordered_neighbors;
    std::vector<std::uint32_t> naive_neighbors;
    ordered_neighbors.reserve(mesh.directed_edge_count());
    naive_neighbors.reserve(mesh.directed_edge_count());
    std::vector<float> ordered_values(cell_count);
    std::vector<float> naive_values(cell_count);

    for (std::size_t cell_index = 0; cell_index < cell_count; ++cell_index) {
        const auto& cell = mesh.cells()[cell_index];
        const float value = 0.25F + static_cast<float>(cell.source_vertex_index % 997U) / 997.0F;
        ordered_values[cell_index] = value;
        naive_values[cell.source_vertex_index] = value;
        for (const auto& adjacency : mesh.cell_edges(cell.id)) {
            ordered_neighbors.push_back(adjacency.neighbor.value());
        }
        ordered_offsets[cell_index + 1U] = static_cast<std::uint32_t>(ordered_neighbors.size());
    }
    for (std::size_t source_index = 0; source_index < cell_count; ++source_index) {
        const auto cell_id = source_to_cell[source_index];
        for (const auto& adjacency : mesh.cell_edges(cell_id)) {
            naive_neighbors.push_back(mesh.cell(adjacency.neighbor).source_vertex_index);
        }
        naive_offsets[source_index + 1U] = static_cast<std::uint32_t>(naive_neighbors.size());
    }

    const auto ordered = run_layout_kernel(ordered_offsets, ordered_neighbors,
                                           std::move(ordered_values), iterations);
    const auto naive =
        run_layout_kernel(naive_offsets, naive_neighbors, std::move(naive_values), iterations);

    double checksum = 0.0;
    for (std::size_t source_index = 0; source_index < cell_count; ++source_index) {
        const float ordered_value = ordered.values[source_to_cell[source_index].to_index()];
        const float naive_value = naive.values[source_index];
        if (ordered_value != naive_value) {
            throw std::logic_error("ordered and naive layout kernels diverged");
        }
        checksum += static_cast<double>(ordered_value);
    }

    return {ordered.time_ms, naive.time_ms,
            ordered.time_ms > 0.0 ? naive.time_ms / ordered.time_ms : 0.0, checksum};
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
              << "corner_count: " << diagnostics.corner_count << '\n'
              << "edge_count: " << diagnostics.edge_count << '\n'
              << "directed_edge_count: " << diagnostics.directed_edge_count << '\n'
              << "pentagon_count: " << diagnostics.pentagon_count << '\n'
              << "hexagon_count: " << diagnostics.hexagon_count << '\n'
              << "deterministic_block_count: " << diagnostics.block_count << '\n'
              << "geometry_topology_bytes: " << diagnostics.allocated_bytes << '\n'
              << "radius_m: " << mesh.radius_m() << '\n'
              << "total_area_m2: " << diagnostics.total_area_m2 << '\n'
              << "expected_area_m2: " << diagnostics.expected_area_m2 << '\n'
              << "relative_area_error: " << diagnostics.relative_area_error << '\n'
              << "min_cell_area_m2: " << diagnostics.min_cell_area_m2 << '\n'
              << "max_cell_area_m2: " << diagnostics.max_cell_area_m2 << '\n'
              << "min_edge_length_m: " << diagnostics.min_edge_length_m << '\n'
              << "max_edge_length_m: " << diagnostics.max_edge_length_m << '\n'
              << "min_centroid_distance_m: " << diagnostics.min_centroid_distance_m << '\n'
              << "max_centroid_distance_m: " << diagnostics.max_centroid_distance_m << '\n'
              << "max_center_norm_error: " << diagnostics.max_center_norm_error << '\n'
              << "max_tangent_basis_error: " << diagnostics.max_tangent_basis_error << '\n'
              << "mean_neighbor_index_delta: " << diagnostics.mean_neighbor_index_delta << '\n'
              << "naive_mean_neighbor_index_delta: " << diagnostics.naive_mean_neighbor_index_delta
              << '\n'
              << "invalid_neighbor_count: " << diagnostics.invalid_neighbor_count << '\n'
              << "non_reciprocal_neighbor_count: " << diagnostics.non_reciprocal_neighbor_count
              << '\n'
              << "invalid_edge_reference_count: " << diagnostics.invalid_edge_reference_count
              << '\n'
              << "invalid_corner_index_count: " << diagnostics.invalid_corner_index_count << '\n'
              << "shared_corner_mismatch_count: " << diagnostics.shared_corner_mismatch_count
              << '\n'
              << "edge_reference_count_mismatch: " << diagnostics.edge_reference_count_mismatch
              << '\n'
              << "invalid_block_count: " << diagnostics.invalid_block_count << '\n'
              << "non_finite_geometry_count: " << diagnostics.non_finite_geometry_count << '\n'
              << "topology_valid: " << diagnostics.topology_valid() << '\n'
              << "construction_time_ms: " << construction_time_ms << '\n';

    if (options.benchmark_layout) {
        const auto benchmark = benchmark_layout(mesh, options.benchmark_iterations);
        std::cout << "layout_benchmark_iterations: " << options.benchmark_iterations << '\n'
                  << "ordered_layout_time_ms: " << benchmark.ordered_time_ms << '\n'
                  << "naive_layout_time_ms: " << benchmark.naive_time_ms << '\n'
                  << "naive_to_ordered_time_ratio: " << benchmark.naive_to_ordered_ratio << '\n'
                  << "layout_benchmark_checksum: " << benchmark.checksum << '\n';
    }

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
    constexpr double ticks_per_day = 1'440.0;
    const double requested_ticks = options.time_days * ticks_per_day;
    if (requested_ticks >
        static_cast<double>(std::numeric_limits<planetsim::SimulationTick>::max())) {
        throw std::invalid_argument("time in days exceeds the simulation clock range");
    }
    const auto simulation_tick =
        static_cast<planetsim::SimulationTick>(std::llround(requested_ticks));
    const double effective_simulation_time_s = planetsim::simulation_time_s(simulation_tick);
    planetsim::update_solar_forcing(state, parameters, simulation_tick);
    const auto finish = std::chrono::steady_clock::now();

    const auto diagnostics = planetsim::analyze_solar_forcing(state);
    const auto& orbit = state.forcing().orbit;
    const double evaluation_time_ms =
        std::chrono::duration<double, std::milli>(finish - start).count();
    const double radians_to_degrees = 180.0 / std::numbers::pi_v<double>;

    std::cout << std::setprecision(17) << std::boolalpha
              << "subdivision: " << parameters.mesh_subdivision << '\n'
              << "cell_count: " << diagnostics.cell_count << '\n'
              << "simulation_tick: " << simulation_tick << '\n'
              << "simulation_time_s: " << effective_simulation_time_s << '\n'
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
