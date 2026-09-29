#include "sim/core/fields/field_registry.hpp"
#include "sim/core/random/counter_rng.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/orbit/solar_diagnostics.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/terrain_diagnostics.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct MeshOptions {
    std::uint32_t subdivision = 6;
    double radius_m = 6'371'000.0;
    bool benchmark_layout = false;
    std::uint32_t benchmark_iterations = 100;
};

struct OperatorOptions {
    std::uint32_t min_subdivision = 2;
    std::uint32_t max_subdivision = 6;
    double radius_m = 6'371'000.0;
    std::string error_map_path;
};

struct SolarOptions {
    std::uint32_t subdivision = 5;
    double radius_m = 6'371'000.0;
    double time_days = 0.0;
};

struct SnapshotWriteOptions {
    std::uint32_t subdivision = 5;
    std::filesystem::path output_path;
};

struct TerrainOptions {
    std::uint32_t subdivision = 5;
    std::uint64_t seed = 1;
    planetsim::PlanetPreset preset = planetsim::PlanetPreset::earth_like;
    std::optional<double> land_fraction;
    std::optional<std::uint32_t> plate_count;
    std::size_t worker_count = 0;  // 0: hardware concurrency
    std::filesystem::path map_path;
    std::filesystem::path snapshot_path;
};

constexpr std::uint64_t snapshot_synthetic_seed = 0x9B97'F4A7'C150'0011ULL;
constexpr planetsim::SimulationTick snapshot_synthetic_tick = 123'456;

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  planet_cli mesh [--subdivision LEVEL] [--radius METRES]"
              " [--benchmark-layout] [--benchmark-iterations COUNT]\n"
           << "  planet_cli solar [--subdivision LEVEL] [--radius METRES]"
              " [--time-days DAYS]\n"
           << "  planet_cli operators [--min-subdivision LEVEL] [--max-subdivision LEVEL]"
              " [--radius METRES] [--error-map CSV]\n"
           << "  planet_cli terrain [--subdivision LEVEL] [--seed N] [--preset NAME]"
              " [--land-fraction F] [--plates P] [--workers W] [--map FILE.csv]"
              " [--snapshot FILE.psnap]\n"
           << "    presets: earth_like (default), aqua_planet, dead_rock\n";
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

[[nodiscard]] std::uint64_t parse_unsigned(std::string_view text, std::string_view description) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string(description) + ": " +
                                    std::string(text));
    }
    return parsed;
}

[[nodiscard]] TerrainOptions parse_terrain_options(int argument_count, char** arguments) {
    TerrainOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--subdivision") {
            options.subdivision = parse_subdivision(value);
        } else if (argument == "--seed") {
            options.seed = parse_unsigned(value, "seed");
        } else if (argument == "--preset") {
            const auto preset = planetsim::parse_planet_preset(value);
            if (!preset) {
                throw std::invalid_argument("unknown preset: " + std::string(value));
            }
            options.preset = *preset;
        } else if (argument == "--land-fraction") {
            options.land_fraction = parse_double(value, "land fraction");
        } else if (argument == "--plates") {
            const std::uint64_t plates = parse_unsigned(value, "plate count");
            if (plates > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("invalid plate count: " + std::string(value));
            }
            options.plate_count = static_cast<std::uint32_t>(plates);
        } else if (argument == "--workers") {
            options.worker_count = static_cast<std::size_t>(parse_unsigned(value, "worker count"));
            if (options.worker_count == 0U) {
                throw std::invalid_argument("worker count must be positive");
            }
        } else if (argument == "--map") {
            options.map_path = std::filesystem::path(value);
        } else if (argument == "--snapshot") {
            options.snapshot_path = std::filesystem::path(value);
        } else {
            throw std::invalid_argument("unknown terrain option: " + std::string(argument));
        }
    }
    return options;
}

[[nodiscard]] std::string_view boundary_class_name(planetsim::BoundaryClass boundary_class) {
    switch (boundary_class) {
    case planetsim::BoundaryClass::none:
        return "none";
    case planetsim::BoundaryClass::convergent:
        return "convergent";
    case planetsim::BoundaryClass::divergent:
        return "divergent";
    case planetsim::BoundaryClass::transform:
        return "transform";
    }
    return "unknown";
}

void write_terrain_map(const std::filesystem::path& path, const planetsim::PlanetState& state,
                       const planetsim::GeologyState& geology,
                       const planetsim::DrainageState& drainage) {
    const auto& mesh = state.mesh();
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open terrain map: " + path.string());
    }
    constexpr double degrees = 180.0 / std::numbers::pi;
    output << std::setprecision(9)
           << "cell_id,latitude_deg,longitude_deg,plate_id,crust_type,crust_age_myr,"
              "nearest_boundary_class,nearest_boundary_distance_km,mean_elevation_m,"
              "lowest_quantile_m,highest_quantile_m,land_fraction,"
              "drainage_elevation_m,filled_elevation_m,downstream,basin_id,"
              "depression_id,catchment_area_m2\n";
    for (const auto& cell : mesh.cells()) {
        const auto quantiles = planetsim::cell_hypsometry(state.slow().hypsometry_m, cell.id);
        output << cell.id.value() << ','
               << planetsim::latitude_rad(cell.center_unit) * degrees << ','
               << planetsim::longitude_rad(cell.center_unit) * degrees << ','
               << geology.plate_id[cell.id].value() << ','
               << (geology.crust_type[cell.id] == planetsim::CrustType::continental
                       ? "continental"
                       : "oceanic")
               << ','
               << geology.crust_age_s[cell.id] / planetsim::seconds_per_million_years << ','
               << boundary_class_name(geology.nearest_boundary_class[cell.id]) << ','
               << geology.nearest_boundary_distance_m[cell.id] / 1'000.0 << ','
               << planetsim::mean_elevation_m(quantiles) << ',' << quantiles.front() << ','
               << quantiles.back() << ','
               << drainage.surface.land_fraction[cell.id] << ','
               << drainage.surface.drainage_elevation_m[cell.id] << ','
               << drainage.surface.filled_elevation_m[cell.id] << ','
               << drainage.downstream[cell.id] << ','
               << drainage.basin_id[cell.id] << ','
               << drainage.surface.depression_id[cell.id] << ','
               << drainage.catchment_area_m2[cell.id] << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed writing terrain map: " + path.string());
    }
}

int run_terrain(const TerrainOptions& options) {
    auto parameters = planetsim::geology_parameters_for(options.preset);
    if (options.land_fraction) {
        parameters.target_land_fraction = *options.land_fraction;
    }
    if (options.plate_count) {
        parameters.plate_count = *options.plate_count;
    }
    planetsim::validate_geology_parameters(parameters);
    const std::size_t worker_count =
        options.worker_count != 0U ? options.worker_count
                                   : std::max<std::size_t>(1U, std::thread::hardware_concurrency());

    const auto mesh_start = std::chrono::steady_clock::now();
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, 6'371'000.0));
    const auto generation_start = std::chrono::steady_clock::now();
    planetsim::PlanetState state(mesh);
    const auto generation =
        planetsim::generate_terrain(state, options.seed, parameters, worker_count);
    const auto generation_finish = std::chrono::steady_clock::now();
    const auto diagnostics = planetsim::compute_terrain_diagnostics(
        *mesh, generation.geology, state.slow().hypsometry_m, state.slow().sea_level_m,
        worker_count);
    const auto drainage_start = std::chrono::steady_clock::now();
    const auto timed_drainage = planetsim::generate_drainage(
        *mesh, state.slow().hypsometry_m, state.slow().sea_level_m, worker_count);
    const auto drainage_finish = std::chrono::steady_clock::now();

    const auto milliseconds = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    const auto& solution = generation.sea_level;
    std::cout << std::setprecision(6) << "preset: " << planetsim::planet_preset_name(options.preset)
              << '\n'
              << "seed: " << options.seed << '\n'
              << "subdivision: " << options.subdivision << '\n'
              << "cell_count: " << mesh->cell_count() << '\n'
              << "worker_count: " << worker_count << '\n'
              << "plate_count: " << diagnostics.plate_count << '\n'
              << "plate_area_fractions:";
    for (const double fraction : diagnostics.plate_area_fractions) {
        std::cout << ' ' << fraction;
    }
    std::cout << '\n';
    for (const auto boundary_class :
         {planetsim::BoundaryClass::convergent, planetsim::BoundaryClass::divergent,
          planetsim::BoundaryClass::transform}) {
        const auto index = static_cast<std::size_t>(boundary_class);
        std::cout << "boundary_" << boundary_class_name(boundary_class)
                  << "_length_km: " << diagnostics.boundary_length_m[index] / 1'000.0
                  << " (edges: " << diagnostics.boundary_edge_count[index] << ")\n";
    }
    std::cout << "continental_area_fraction: " << diagnostics.continental_area_fraction << '\n'
              << "oceanic_crust_age_myr: " << diagnostics.oceanic_age_min_myr << " -- "
              << diagnostics.oceanic_age_max_myr << '\n'
              << "continental_crust_age_myr: " << diagnostics.continental_age_min_myr << " -- "
              << diagnostics.continental_age_max_myr << '\n'
              << "mean_elevation_percentiles_m:";
    for (std::size_t index = 0; index < planetsim::terrain_elevation_percentiles.size(); ++index) {
        std::cout << " p" << planetsim::terrain_elevation_percentiles[index] << '='
                  << diagnostics.mean_elevation_percentiles_m[index];
    }
    std::cout << '\n'
              << "target_land_fraction: " << parameters.target_land_fraction << '\n'
              << "achieved_land_fraction: " << solution.achieved_land_fraction << '\n'
              << "sea_level_m: " << std::setprecision(9) << solution.sea_level_m
              << std::setprecision(6) << '\n'
              << "sea_level_in_connectivity_jump: " << (solution.target_in_jump ? "true" : "false")
              << '\n';
    if (solution.target_in_jump) {
        std::cout << "land_fraction_above_jump: " << solution.land_fraction_above_jump << '\n'
                  << "jump_changes_ocean_connectivity: "
                  << (solution.jump_changes_ocean_connectivity ? "true" : "false") << '\n';
    }
    std::cout << "inland_depressions: " << diagnostics.inland_depression_count << '\n'
              << "ocean_area_fraction: " << diagnostics.ocean_area_fraction << '\n'
              << "ocean_shallower_than_200m_fraction: "
              << diagnostics.ocean_shallower_than_200m_fraction << '\n'
              << "ocean_deeper_than_4000m_fraction: " << diagnostics.ocean_deeper_than_4000m_fraction
              << '\n'
              << "ocean_elevation_mean_m: " << diagnostics.ocean_elevation_mean_m << '\n'
              << "ocean_elevation_std_m: " << diagnostics.ocean_elevation_std_m << '\n';
    const auto& drainage = timed_drainage;
    const double drainage_time_ms = milliseconds(drainage_start, drainage_finish);
    std::cout << "drainage_outlet_count: " << drainage.surface.outlet_count << '\n'
              << "drainage_basin_count: " << drainage.diagnostics.basin_count << '\n'
              << "drainage_depression_count: " << drainage.surface.depressions.size() << '\n'
              << "drainage_total_routed_land_area_m2: "
              << drainage.diagnostics.total_routed_land_area_m2 << '\n'
              << "drainage_terminal_catchment_area_m2: "
              << drainage.diagnostics.terminal_catchment_area_m2 << '\n'
              << "drainage_catchment_closure_relative_error: "
              << drainage.diagnostics.catchment_closure_relative_error << '\n'
              << "drainage_largest_catchment_area_m2: "
              << drainage.diagnostics.largest_catchment_area_m2 << '\n'
              << "drainage_maximum_fill_depth_m: "
              << drainage.surface.maximum_fill_depth_m << '\n'
              << "drainage_invalid_downstream_count: "
              << drainage.diagnostics.invalid_downstream_count << '\n'
              << "drainage_cycle_count: " << drainage.diagnostics.cycle_count << '\n'
              << "drainage_unreachable_cell_count: "
              << drainage.diagnostics.unreachable_cell_count << '\n'
              << "drainage_time_ms: " << drainage_time_ms
              << '\n'
              << "mesh_time_ms: " << milliseconds(mesh_start, generation_start) << '\n'
              << "generation_time_ms: " << milliseconds(generation_start, generation_finish) << '\n';

    if (!options.map_path.empty()) {
        write_terrain_map(options.map_path, state, generation.geology, drainage);
        std::cout << "map_written: " << options.map_path.string() << '\n';
    }
    if (!options.snapshot_path.empty()) {
        planetsim::write_snapshot(options.snapshot_path, state, 0);
        std::cout << "snapshot_written: " << options.snapshot_path.string() << '\n';
    }
    const bool terrain_valid =
        drainage.diagnostics.invalid_downstream_count == 0U &&
        drainage.diagnostics.cycle_count == 0U &&
        drainage.diagnostics.unreachable_cell_count == 0U &&
        drainage.diagnostics.catchment_closure_relative_error <= 1e-12;
    std::cout << "terrain_valid: " << (terrain_valid ? "true" : "false") << '\n';
    return terrain_valid ? 0 : 2;
}

[[nodiscard]] SnapshotWriteOptions parse_snapshot_write_options(int argument_count,
                                                                char** arguments) {
    SnapshotWriteOptions options;
    for (int index = 3; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--subdivision") {
            options.subdivision = parse_subdivision(value);
        } else if (argument == "--out") {
            options.output_path = std::filesystem::path(value);
        } else {
            throw std::invalid_argument("unknown snapshot write option: " +
                                        std::string(argument));
        }
    }
    if (options.output_path.empty()) {
        throw std::invalid_argument("snapshot write requires --out FILE");
    }
    return options;
}

void populate_snapshot_synthetic_state(planetsim::PlanetState& state) {
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        std::array<float, planetsim::hypsometry_layer_count> quantiles{};
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            const double unit = planetsim::keyed_random_unit_double(
                snapshot_synthetic_seed, planetsim::RandomStreamId::validation,
                snapshot_synthetic_tick, static_cast<std::uint32_t>(cell),
                static_cast<std::uint32_t>(layer));
            quantiles[layer] = static_cast<float>(-8'000.0 + unit * 16'000.0);
        }
        std::sort(quantiles.begin(), quantiles.end());
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            state.slow().hypsometry_m.at(
                layer, planetsim::CellId{static_cast<std::uint32_t>(cell)}) = quantiles[layer];
        }
    }
    const double sea_unit = planetsim::keyed_random_unit_double(
        snapshot_synthetic_seed, planetsim::RandomStreamId::validation,
        snapshot_synthetic_tick, 0U, 99U);
    state.slow().sea_level_m = -200.0 + sea_unit * 400.0;
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

[[nodiscard]] OperatorOptions parse_operator_options(int argument_count, char** arguments) {
    OperatorOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--min-subdivision") {
            options.min_subdivision = parse_subdivision(value);
        } else if (argument == "--max-subdivision") {
            options.max_subdivision = parse_subdivision(value);
        } else if (argument == "--radius") {
            options.radius_m = parse_double(value, "radius");
        } else if (argument == "--error-map") {
            options.error_map_path = std::string(value);
        } else {
            throw std::invalid_argument("unknown operators option: " + std::string(argument));
        }
    }
    if (options.min_subdivision > options.max_subdivision) {
        throw std::invalid_argument("minimum subdivision exceeds maximum subdivision");
    }
    return options;
}

void write_error_map(const std::string& path, const planetsim::PlanetMesh& mesh,
                     const planetsim::OperatorValidation& validation) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open error map: " + path);
    }
    const double radians_to_degrees = 180.0 / std::numbers::pi_v<double>;
    output << std::setprecision(9)
           << "cell,latitude_deg,longitude_deg,is_pentagon,gradient_error,divergence_error,"
              "laplacian_error,poisson_solution_error\n";
    for (const auto& cell : mesh.cells()) {
        const auto& error = validation.cells[cell.id.to_index()];
        output << cell.id.value() << ','
               << planetsim::latitude_rad(cell.center_unit) * radians_to_degrees << ','
               << planetsim::longitude_rad(cell.center_unit) * radians_to_degrees << ','
               << (cell.is_pentagon() ? 1 : 0) << ',' << error.gradient << ','
               << error.divergence << ',' << error.laplacian << ',' << error.poisson_solution
               << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed to write error map: " + path);
    }
}

int run_operators(const OperatorOptions& options) {
    struct Row {
        std::string_view name;
        planetsim::OperatorErrorNorms planetsim::OperatorValidation::*norms;
    };
    constexpr Row rows[] = {
        {"gradient", &planetsim::OperatorValidation::gradient},
        {"divergence", &planetsim::OperatorValidation::divergence},
        {"laplacian", &planetsim::OperatorValidation::laplacian},
        {"poisson_solution", &planetsim::OperatorValidation::poisson_solution},
    };

    std::cout << std::setprecision(6) << std::scientific;
    planetsim::OperatorValidation previous;
    bool has_previous = false;
    for (std::uint32_t level = options.min_subdivision; level <= options.max_subdivision;
         ++level) {
        const auto mesh = planetsim::make_icosphere(level, options.radius_m);
        auto validation = planetsim::validate_operators(mesh);
        const auto nondivergent = planetsim::check_nondivergent_flux(mesh);
        std::cout << "subdivision: " << level << " cells: " << validation.cell_count << '\n'
                  << "  nondivergent_relative_max_divergence: "
                  << nondivergent.relative_max_divergence << '\n'
                  << "  relative_global_imbalance: " << nondivergent.relative_global_imbalance
                  << '\n'
                  << "  poisson_iterations: " << validation.poisson_iterations << '\n';
        for (const auto& row : rows) {
            const auto& norms = validation.*(row.norms);
            std::cout << "  " << row.name << "_relative_l2: " << norms.relative_l2
                      << " relative_max: " << norms.relative_max
                      << " pentagon_relative_max: " << norms.pentagon_relative_max
                      << " seam_relative_max: " << norms.seam_relative_max
                      << " interior_relative_max: " << norms.interior_relative_max;
            if (has_previous) {
                const auto& coarse = previous.*(row.norms);
                std::cout << std::fixed << std::setprecision(3)
                          << " order_l2: " << std::log2(coarse.relative_l2 / norms.relative_l2)
                          << " order_max: "
                          << std::log2(coarse.relative_max / norms.relative_max)
                          << std::scientific << std::setprecision(6);
            }
            std::cout << '\n';
        }
        if (level == options.max_subdivision && !options.error_map_path.empty()) {
            write_error_map(options.error_map_path, mesh, validation);
        }
        previous = std::move(validation);
        has_previous = true;
    }
    return 0;
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

int run_registry_dump() {
    for (const auto& descriptor : planetsim::field_registry) {
        std::cout << static_cast<std::uint32_t>(descriptor.id) << '\t' << descriptor.name << '\t'
                  << planetsim::field_partition_name(descriptor.partition) << '\t'
                  << planetsim::field_layout_name(descriptor.layout) << '\t'
                  << planetsim::field_data_type_name(descriptor.data_type) << '\t'
                  << descriptor.layers << '\t' << descriptor.units << '\n';
    }
    for (const auto retired_id : planetsim::retired_field_ids) {
        std::cout << "# retired_field_id\t" << static_cast<std::uint32_t>(retired_id) << '\n';
    }
    return 0;
}

int run_snapshot_write(const SnapshotWriteOptions& options) {
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    populate_snapshot_synthetic_state(state);
    const auto start = std::chrono::steady_clock::now();
    planetsim::write_snapshot(options.output_path, state, snapshot_synthetic_tick);
    const auto finish = std::chrono::steady_clock::now();

    std::cout << std::setprecision(17)
              << "snapshot_written: " << options.output_path.string() << '\n'
              << "subdivision: " << options.subdivision << '\n'
              << "cell_count: " << mesh->cell_count() << '\n'
              << "simulation_tick: " << snapshot_synthetic_tick << '\n'
              << "size_bytes: " << std::filesystem::file_size(options.output_path) << '\n'
              << "write_time_ms: "
              << std::chrono::duration<double, std::milli>(finish - start).count() << '\n';
    return 0;
}

int run_snapshot_inspect(const std::filesystem::path& path) {
    const auto manifest = planetsim::inspect_snapshot(path);
    std::cout << "format: " << manifest.format << '\n'
              << "schema_version: " << manifest.schema_version << '\n'
              << "engine_version: " << manifest.engine_version << '\n'
              << "tick: " << manifest.tick << '\n'
              << "mesh_level: " << manifest.mesh_level << '\n'
              << "cell_count: " << manifest.cell_count << '\n'
              << "mesh_generator_version: " << manifest.mesh_generator_version << '\n'
              << "mesh_checksum: " << manifest.mesh_checksum << '\n'
              << "parent_snapshot_id: " << manifest.parent_snapshot_id << '\n'
              << "field_count: " << manifest.fields.size() << '\n';
    for (const auto& field : manifest.fields) {
        std::cout << "field_id: " << field.field_id << " name: " << field.name
                  << " partition: " << field.partition << " layout: " << field.layout
                  << " dtype: " << field.dtype << " layers: " << field.layers
                  << " compression: " << field.compression
                  << " byte_offset: " << field.byte_offset
                  << " byte_length: " << field.byte_length
                  << " checksum: " << field.checksum << '\n';
    }
    std::cout << "checksums_valid: true\n";
    return 0;
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
        if (command == "operators") {
            return run_operators(parse_operator_options(argument_count, arguments));
        }
        if (command == "solar") {
            return run_solar(parse_solar_options(argument_count, arguments));
        }
        if (command == "terrain") {
            return run_terrain(parse_terrain_options(argument_count, arguments));
        }
        if (command == "registry") {
            if (argument_count == 3 && std::string_view{arguments[2]} == "dump") {
                return run_registry_dump();
            }
            throw std::invalid_argument("registry requires dump");
        }
        if (command == "snapshot") {
            if (argument_count < 3) {
                throw std::invalid_argument("snapshot requires write or inspect");
            }
            const std::string_view snapshot_command{arguments[2]};
            if (snapshot_command == "write") {
                return run_snapshot_write(
                    parse_snapshot_write_options(argument_count, arguments));
            }
            if (snapshot_command == "inspect") {
                if (argument_count != 4) {
                    throw std::invalid_argument("snapshot inspect requires exactly one file");
                }
                return run_snapshot_inspect(arguments[3]);
            }
            throw std::invalid_argument("unknown snapshot command: " +
                                        std::string(snapshot_command));
        }
        print_usage(std::cerr);
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "planet_cli: " << exception.what() << '\n';
        return 1;
    }
}
