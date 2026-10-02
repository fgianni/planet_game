#include "sim/core/fields/field_registry.hpp"
#include "sim/core/random/counter_rng.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/core/serialization/run_manifest.hpp"
#include "sim/core/serialization/history_store.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/dynamics/orography.hpp"
#include "sim/planet/dynamics/primitive_equations.hpp"
#include "sim/planet/dynamics/shallow_water.hpp"
#include "sim/planet/dynamics/williamson_cases.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/mesh_diagnostics.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/solar_diagnostics.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
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
#include <sstream>
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
    std::optional<std::int64_t> substep;   // climate sub-step mean instead of an instant
};

struct SnapshotWriteOptions {
    std::uint32_t subdivision = 5;
    std::filesystem::path output_path;
    planetsim::SnapshotCompression compression = planetsim::SnapshotCompression::shuffle_zstd;
    // With a parent: a delta of the perturbed synthetic state against it.
    std::filesystem::path delta_of;
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

struct HistoryOptions {
    std::uint32_t subdivision = 6;
    std::uint64_t seed = 1;
    int spin_up_years = 30;
    int decades = 10;
    std::uint32_t base_interval = 8;
    std::size_t worker_count = 0;   // 0: hardware concurrency
    std::filesystem::path directory;
};

struct AtmosphereOptions {
    std::uint32_t subdivision = 5;
    std::uint64_t seed = 1;
    std::uint32_t layers = 3;
    std::size_t worker_count = 0;   // 0: hardware concurrency
};

struct ThermalOptions {
    std::uint32_t subdivision = 5;
    std::uint64_t seed = 1;
    planetsim::PlanetPreset preset = planetsim::PlanetPreset::earth_like;
    int years = 60;
    std::optional<double> grey_emissivity;
    std::optional<double> calibrate_K;   // fit g so the last spin-up year has this mean
    double precipitation_kg_m2_s = 0.0;  // uniform prescribed precipitation (ADR-0008 §3.3 B)
    std::optional<double> transport_W_m2_K;           // D (ADR-0009)
    std::optional<double> calibrate_transport_PW;     // fit D to this peak transport
    std::optional<double> calibrate_gradient_K;       // fit D to this P2 equator-to-pole ΔT
    // ADR-0010: a layered atmosphere of N layers and optical depth τ₀
    // (replaces the grey layer; --calibrate then fits τ₀).
    std::optional<std::uint32_t> atmosphere_layers;
    std::optional<double> optical_depth;
    std::size_t worker_count = 0;        // 0: hardware concurrency
};

struct RunOptions {
    planetsim::Scenario scenario;
    int years = 1;
    std::vector<planetsim::RunCommand> commands;
    std::size_t worker_count = 0;   // 0: hardware concurrency
    std::optional<std::filesystem::path> manifest_path;
    std::optional<std::filesystem::path> snapshot_path;
    // ADR-0001 §5 gates: exit 3 if the stepping rate or the total wall time
    // (setup included) misses them.
    std::optional<double> min_years_per_minute;
    std::optional<double> max_seconds;
};

constexpr std::uint64_t snapshot_synthetic_seed = 0x9B97'F4A7'C150'0011ULL;
constexpr planetsim::SimulationTick snapshot_synthetic_tick = 123'456;

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  planet_cli mesh [--subdivision LEVEL] [--radius METRES]"
              " [--benchmark-layout] [--benchmark-iterations COUNT]\n"
           << "  planet_cli solar [--subdivision LEVEL] [--radius METRES]"
              " [--time-days DAYS | --substep K]\n"
           << "  planet_cli operators [--min-subdivision LEVEL] [--max-subdivision LEVEL]"
              " [--radius METRES] [--error-map CSV]\n"
           << "  planet_cli terrain [--subdivision LEVEL] [--seed N] [--preset NAME]"
              " [--land-fraction F] [--plates P] [--workers W] [--map FILE.csv]"
              " [--snapshot FILE.psnap]\n"
           << "    presets: earth_like (default), aqua_planet, dead_rock\n"
           << "  planet_cli calendar [--year N | --from-tick T]\n"
           << "  planet_cli atmosphere [--subdivision LEVEL] [--seed N] [--layers N]"
              " [--workers W]\n"
           << "  planet_cli history [--subdivision LEVEL] [--seed N] [--spin-up-years N]"
              " [--decades N] [--base-interval N] [--workers W] [--out DIRECTORY]\n"
           << "  planet_cli thermal [--subdivision LEVEL] [--seed N] [--preset NAME]"
              " [--years N] [--grey G | --layers N [--optical-depth TAU]]"
              " [--calibrate KELVIN]"
              " [--transport D | --calibrate-gradient KELVIN | --calibrate-transport PW]"
              " [--precipitation KG_M2_S]"
              " [--workers W]\n"
           << "  planet_cli run [--subdivision LEVEL] [--seed N] [--preset NAME] [--years N]"
              " [--spin-up-years N] [--initial-mode climate|reference]"
              " [--command TICK,TYPE,PAYLOAD]... [--workers W] [--manifest FILE.prun]"
              " [--snapshot FILE.psnap] [--min-years-per-minute R] [--max-seconds S]\n"
           << "    commands: set_mode,climate|reference; set_solar_luminosity_factor,F\n"
           << "  planet_cli replay FILE.prun [--workers W]\n"
           << "  planet_cli shallow-water [--case 2|5] [--subdivision LEVEL] [--days D]"
              " [--workers W] [--damping-hours H] [--reference-subdivision LEVEL]\n"
           << "  planet_cli dynamics [--test rest|held-suarez] [--subdivision LEVEL]"
              " [--layers N] [--days D] [--average-days D] [--damping-hours H] [--seed N]"
              " [--lapse-rate K_PER_KM] [--orography-passes P | --orography-step M] [--workers W]\n";
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

[[nodiscard]] std::int64_t parse_int64(std::string_view text, std::string_view description) {
    std::int64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string(description) + ": " +
                                    std::string(text));
    }
    return parsed;
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

[[nodiscard]] planetsim::RunCommand parse_run_command(std::string_view text) {
    const std::size_t first = text.find(',');
    const std::size_t second =
        first == std::string_view::npos ? first : text.find(',', first + 1U);
    if (second == std::string_view::npos) {
        throw std::invalid_argument("--command needs TICK,TYPE,PAYLOAD: " + std::string(text));
    }
    return {parse_int64(text.substr(0, first), "command tick"), "cli",
            std::string(text.substr(first + 1U, second - first - 1U)),
            std::string(text.substr(second + 1U))};
}

[[nodiscard]] RunOptions parse_run_options(int argument_count, char** arguments) {
    RunOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--subdivision") {
            options.scenario.subdivision = parse_subdivision(value);
        } else if (argument == "--seed") {
            options.scenario.seed = parse_unsigned(value, "seed");
        } else if (argument == "--preset") {
            const auto preset = planetsim::parse_planet_preset(value);
            if (!preset) {
                throw std::invalid_argument("unknown preset: " + std::string(value));
            }
            options.scenario.preset = *preset;
        } else if (argument == "--years" || argument == "--spin-up-years") {
            const std::int64_t years = parse_int64(value, "year count");
            if (years < 0 || years > 100'000 || (argument == "--years" && years == 0)) {
                throw std::invalid_argument("invalid year count: " + std::string(value));
            }
            (argument == "--years" ? options.years : options.scenario.spin_up_years) =
                static_cast<int>(years);
        } else if (argument == "--initial-mode") {
            if (value == "climate") {
                options.scenario.initial_mode = planetsim::SimulationMode::climate;
            } else if (value == "reference") {
                options.scenario.initial_mode = planetsim::SimulationMode::reference;
            } else {
                throw std::invalid_argument("unknown initial mode: " + std::string(value));
            }
        } else if (argument == "--command") {
            options.commands.push_back(parse_run_command(value));
        } else if (argument == "--workers") {
            options.worker_count = static_cast<std::size_t>(parse_unsigned(value, "worker count"));
            if (options.worker_count == 0U) {
                throw std::invalid_argument("worker count must be positive");
            }
        } else if (argument == "--manifest") {
            options.manifest_path = std::filesystem::path(value);
        } else if (argument == "--snapshot") {
            options.snapshot_path = std::filesystem::path(value);
        } else if (argument == "--min-years-per-minute") {
            options.min_years_per_minute = parse_double(value, "years per minute");
        } else if (argument == "--max-seconds") {
            options.max_seconds = parse_double(value, "maximum seconds");
        } else {
            throw std::invalid_argument("unknown run option: " + std::string(argument));
        }
    }
    return options;
}

[[nodiscard]] ThermalOptions parse_thermal_options(int argument_count, char** arguments) {
    ThermalOptions options;
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
        } else if (argument == "--years") {
            const std::int64_t years = parse_int64(value, "year count");
            if (years <= 0 || years > 100'000) {
                throw std::invalid_argument("invalid year count: " + std::string(value));
            }
            options.years = static_cast<int>(years);
        } else if (argument == "--grey") {
            options.grey_emissivity = parse_double(value, "grey emissivity");
        } else if (argument == "--layers") {
            options.atmosphere_layers =
                static_cast<std::uint32_t>(parse_unsigned(value, "atmosphere layer count"));
        } else if (argument == "--optical-depth") {
            options.optical_depth = parse_double(value, "optical depth");
        } else if (argument == "--calibrate") {
            options.calibrate_K = parse_double(value, "calibration target");
        } else if (argument == "--transport") {
            options.transport_W_m2_K = parse_double(value, "transport coefficient");
        } else if (argument == "--calibrate-gradient") {
            options.calibrate_gradient_K = parse_double(value, "gradient calibration target");
        } else if (argument == "--calibrate-transport") {
            options.calibrate_transport_PW = parse_double(value, "transport calibration target");
        } else if (argument == "--precipitation") {
            options.precipitation_kg_m2_s = parse_double(value, "precipitation");
            if (!std::isfinite(options.precipitation_kg_m2_s) ||
                options.precipitation_kg_m2_s < 0.0) {
                throw std::invalid_argument("precipitation must be finite and non-negative");
            }
        } else if (argument == "--workers") {
            options.worker_count = static_cast<std::size_t>(parse_unsigned(value, "worker count"));
            if (options.worker_count == 0U) {
                throw std::invalid_argument("worker count must be positive");
            }
        } else {
            throw std::invalid_argument("unknown thermal option: " + std::string(argument));
        }
    }
    if (options.grey_emissivity && options.calibrate_K) {
        throw std::invalid_argument("--grey and --calibrate are exclusive");
    }
    if (options.atmosphere_layers && options.grey_emissivity) {
        throw std::invalid_argument("--layers replaces the grey layer; drop --grey");
    }
    if ((options.transport_W_m2_K ? 1 : 0) + (options.calibrate_transport_PW ? 1 : 0) +
            (options.calibrate_gradient_K ? 1 : 0) >
        1) {
        throw std::invalid_argument(
            "--transport, --calibrate-transport and --calibrate-gradient are exclusive");
    }
    return options;
}

[[nodiscard]] HistoryOptions parse_history_options(int argument_count, char** arguments) {
    HistoryOptions options;
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
        } else if (argument == "--spin-up-years") {
            options.spin_up_years = static_cast<int>(parse_int64(value, "spin-up years"));
        } else if (argument == "--decades") {
            options.decades = static_cast<int>(parse_int64(value, "decade count"));
        } else if (argument == "--base-interval") {
            options.base_interval =
                static_cast<std::uint32_t>(parse_unsigned(value, "base interval"));
        } else if (argument == "--workers") {
            options.worker_count = static_cast<std::size_t>(parse_unsigned(value, "worker count"));
        } else if (argument == "--out") {
            options.directory = std::filesystem::path(value);
        } else {
            throw std::invalid_argument("unknown history option: " + std::string(argument));
        }
    }
    if (options.spin_up_years < 0 || options.decades <= 0) {
        throw std::invalid_argument("history needs a non-negative spin-up and at least a decade");
    }
    return options;
}

[[nodiscard]] AtmosphereOptions parse_atmosphere_options(int argument_count, char** arguments) {
    AtmosphereOptions options;
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
        } else if (argument == "--layers") {
            options.layers = static_cast<std::uint32_t>(parse_unsigned(value, "layer count"));
        } else if (argument == "--workers") {
            options.worker_count = static_cast<std::size_t>(parse_unsigned(value, "worker count"));
        } else {
            throw std::invalid_argument("unknown atmosphere option: " + std::string(argument));
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
              << "sea_level_m: " << std::setprecision(9) << solution.sea_level_m << '\n'
              << "sea_level_datum_shift_m: " << solution.datum_shift_m
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
              << "drainage_land_basin_count: " << drainage.diagnostics.land_basin_count << '\n'
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
        } else if (argument == "--compression") {
            if (value == "none") {
                options.compression = planetsim::SnapshotCompression::none;
            } else if (value == "zstd") {
                options.compression = planetsim::SnapshotCompression::shuffle_zstd;
            } else {
                throw std::invalid_argument("unknown compression: " + std::string(value));
            }
        } else if (argument == "--delta-of") {
            options.delta_of = std::filesystem::path(value);
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
    // Schema 2 surface-energy temperatures, 220-320 K.
    const auto temperature = [&](std::size_t cell, std::uint32_t sample) {
        return 220.0 + 100.0 * planetsim::keyed_random_unit_double(
                                   snapshot_synthetic_seed, planetsim::RandomStreamId::validation,
                                   snapshot_synthetic_tick, static_cast<std::uint32_t>(cell),
                                   sample);
    };
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        auto& slow = state.slow();
        slow.land_surface_temperature_K[cell] = static_cast<float>(temperature(cell, 20U));
        slow.land_ground_temperature_K[cell] = static_cast<float>(temperature(cell, 21U));
        slow.ocean_mixed_layer_temperature_K[cell] = temperature(cell, 22U);
        slow.ocean_deep_temperature_K[cell] = temperature(cell, 23U);
        slow.land_snow_water_equivalent_kg_m2[cell] = 2.0 * (temperature(cell, 24U) - 220.0);
        slow.sea_ice_mass_kg_m2[cell] = 20.0 * (temperature(cell, 25U) - 220.0);
    }
    // Schema 5 atmosphere (ADR-0010 §4.1): three layers, p_s 52-102 kPa,
    // layers 170-270 K.
    auto& slow = state.slow();
    slow.atmosphere_temperature_K =
        planetsim::Field3D<double>(3U, state.mesh().cell_count(), 0.0);
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        slow.atmosphere_surface_pressure_Pa[cell] = 500.0 * (temperature(cell, 26U) - 116.0);
        for (std::size_t layer = 0; layer < 3U; ++layer) {
            slow.atmosphere_temperature_K.layer(layer)[cell] =
                temperature(cell, 30U + static_cast<std::uint32_t>(layer)) - 50.0;
        }
    }
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
        } else if (argument == "--substep") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--substep requires a value");
            }
            options.substep = parse_int64(arguments[index], "sub-step index");
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

    struct CGridRow {
        std::string_view name;
        planetsim::OperatorErrorNorms planetsim::CGridValidation::*norms;
    };
    constexpr CGridRow c_grid_rows[] = {
        {"c_grid_tangential_velocity", &planetsim::CGridValidation::tangential_velocity},
        {"c_grid_vorticity", &planetsim::CGridValidation::vorticity},
        {"c_grid_kinetic_energy", &planetsim::CGridValidation::kinetic_energy},
        {"c_grid_reconstruction", &planetsim::CGridValidation::reconstruction},
        {"c_grid_normal_gradient", &planetsim::CGridValidation::normal_gradient},
        {"c_grid_tangential_gradient", &planetsim::CGridValidation::tangential_gradient},
    };
    const auto print_norms = [](std::string_view name, const planetsim::OperatorErrorNorms& norms,
                                const planetsim::OperatorErrorNorms* coarse) {
        std::cout << "  " << name << "_relative_l2: " << norms.relative_l2
                  << " relative_max: " << norms.relative_max
                  << " pentagon_relative_max: " << norms.pentagon_relative_max
                  << " seam_relative_max: " << norms.seam_relative_max
                  << " interior_relative_max: " << norms.interior_relative_max;
        if (coarse != nullptr) {
            std::cout << std::fixed << std::setprecision(3)
                      << " order_l2: " << std::log2(coarse->relative_l2 / norms.relative_l2)
                      << " order_max: " << std::log2(coarse->relative_max / norms.relative_max)
                      << std::scientific << std::setprecision(6);
        }
        std::cout << '\n';
    };

    std::cout << std::setprecision(6) << std::scientific;
    planetsim::OperatorValidation previous;
    planetsim::CGridValidation previous_c_grid;
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
            print_norms(row.name, validation.*(row.norms),
                        has_previous ? &(previous.*(row.norms)) : nullptr);
        }
        auto c_grid = planetsim::validate_c_grid_operators(mesh);
        const auto& identities = c_grid.identities;
        std::cout << "  c_grid_corners: " << c_grid.corner_count
                  << " kite_cell_area: " << identities.kite_cell_area
                  << " kite_triangle_area: " << identities.kite_triangle_area
                  << " weight_antisymmetry: " << identities.weight_antisymmetry << '\n'
                  << "  c_grid_coriolis_work: " << identities.coriolis_work
                  << " curl_of_gradient: " << identities.curl_of_gradient
                  << " perp_vorticity: " << identities.perp_vorticity << '\n';
        for (const auto& row : c_grid_rows) {
            print_norms(row.name, c_grid.*(row.norms),
                        has_previous ? &(previous_c_grid.*(row.norms)) : nullptr);
        }
        previous_c_grid = c_grid;
        if (level == options.max_subdivision && !options.error_map_path.empty()) {
            write_error_map(options.error_map_path, mesh, validation);
        }
        previous = std::move(validation);
        has_previous = true;
    }
    return 0;
}

struct ShallowWaterOptions {
    int test_case = 2;
    std::uint32_t subdivision = 5;
    double days = 5.0;
    std::size_t workers = 1;
    std::optional<double> damping_hours;
    std::optional<std::uint32_t> reference_subdivision;
};

[[nodiscard]] ShallowWaterOptions parse_shallow_water_options(int argument_count,
                                                              char** arguments) {
    ShallowWaterOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--case") {
            options.test_case = static_cast<int>(parse_unsigned(value, "case"));
            if (options.test_case != 2 && options.test_case != 5) {
                throw std::invalid_argument("shallow-water cases are 2 and 5");
            }
        } else if (argument == "--subdivision") {
            options.subdivision = parse_subdivision(value);
        } else if (argument == "--days") {
            options.days = parse_double(value, "days");
        } else if (argument == "--workers") {
            options.workers = static_cast<std::size_t>(parse_unsigned(value, "workers"));
        } else if (argument == "--damping-hours") {
            options.damping_hours = parse_double(value, "damping hours");
        } else if (argument == "--reference-subdivision") {
            options.reference_subdivision = parse_subdivision(value);
        } else {
            throw std::invalid_argument("unknown shallow-water option: " + std::string(argument));
        }
    }
    if (!(options.days > 0.0) || options.workers == 0U) {
        throw std::invalid_argument("days and workers must be positive");
    }
    return options;
}

// The generic axis of test 2: Williamson's α tilt, in a direction shared
// with no symmetry axis of the icosahedron.
[[nodiscard]] planetsim::Vec3d shallow_water_case_2_axis() {
    return {std::sin(0.7) * std::cos(0.4), std::sin(0.7) * std::sin(0.4), std::cos(0.7)};
}

struct ShallowWaterRun {
    planetsim::PlanetMesh mesh;
    planetsim::ShallowWaterState state;
    planetsim::ShallowWaterDiagnostics initial;
    planetsim::ShallowWaterDiagnostics final;
    planetsim::ThicknessErrors errors;
    std::size_t steps = 0;
    double seconds = 0.0;
};

[[nodiscard]] ShallowWaterRun run_shallow_water_case(const ShallowWaterOptions& options,
                                                     std::uint32_t subdivision) {
    ShallowWaterRun run{planetsim::make_icosphere(subdivision, planetsim::williamson_radius_m),
                        {}, {}, {}, {}, 0, 0.0};
    const auto& mesh = run.mesh;
    const auto grid = planetsim::CGridGeometry::build(mesh);
    auto test = options.test_case == 2
                    ? planetsim::williamson_case_2(mesh, grid, shallow_water_case_2_axis())
                    : planetsim::williamson_case_5(mesh, grid);
    if (options.damping_hours) {
        test.parameters.hyperviscosity_m4_s =
            planetsim::hyperviscosity_for_damping_time(mesh, *options.damping_hours * 3600.0);
    }
    const planetsim::ShallowWaterModel model(mesh, grid, test.parameters, test.bottom_height_m);
    planetsim::SubstepRule rule;
    rule.wave_speed_m_s = options.test_case == 2 ? 180.0 : 250.0;
    rule.max_wind_m_s = options.test_case == 2 ? 60.0 : 60.0;

    run.state = test.initial;
    run.initial = model.diagnose(run.state, options.workers);
    const auto start = std::chrono::steady_clock::now();
    run.steps = model.advance(run.state, options.days * planetsim::williamson_day_s, rule,
                              options.workers);
    run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    run.final = model.diagnose(run.state, options.workers);
    run.errors =
        planetsim::thickness_errors(run.mesh, run.state.thickness_m, test.initial.thickness_m);
    return run;
}

int run_shallow_water(const ShallowWaterOptions& options) {
    const auto run = run_shallow_water_case(options, options.subdivision);
    const auto relative = [](double final, double initial) {
        return (final - initial) / std::abs(initial);
    };
    std::cout << std::setprecision(6) << std::scientific << "case: " << options.test_case
              << " subdivision: " << options.subdivision << " days: " << options.days << '\n'
              << "steps: " << run.steps
              << " dt_s: " << options.days * planetsim::williamson_day_s /
                                  static_cast<double>(run.steps)
              << " wall_s: " << run.seconds << '\n'
              << "relative_mass_change: " << relative(run.final.mass_m3, run.initial.mass_m3)
              << '\n'
              << "relative_energy_change: "
              << relative(run.final.energy_J(), run.initial.energy_J()) << '\n'
              << "relative_potential_enstrophy_change: "
              << relative(run.final.potential_enstrophy, run.initial.potential_enstrophy) << '\n'
              << "max_wind_m_s: " << run.final.max_wind_m_s << '\n';
    if (options.test_case == 2) {
        std::cout << "thickness_l2_error: " << run.errors.l2
                  << " thickness_linf_error: " << run.errors.linf << '\n';
    }
    if (options.reference_subdivision) {
        // The difference from a run on another level, sampled at the nearest
        // reference cell: a self-convergence measure where no exact solution
        // exists (test 5).
        const auto reference = run_shallow_water_case(options, *options.reference_subdivision);
        double error_squared = 0.0;
        double exact_squared = 0.0;
        for (const auto& cell : run.mesh.cells()) {
            std::size_t nearest = 0;
            double best = -2.0;
            for (const auto& candidate : reference.mesh.cells()) {
                const double alignment = planetsim::dot(candidate.center_unit, cell.center_unit);
                if (alignment > best) {
                    best = alignment;
                    nearest = candidate.id.to_index();
                }
            }
            const double difference =
                run.state.thickness_m[cell.id] - reference.state.thickness_m[nearest];
            error_squared += cell.area_m2 * difference * difference;
            exact_squared += cell.area_m2 * reference.state.thickness_m[nearest] *
                             reference.state.thickness_m[nearest];
        }
        std::cout << "reference_subdivision: " << *options.reference_subdivision
                  << " thickness_l2_difference: " << std::sqrt(error_squared / exact_squared)
                  << '\n';
    }
    return 0;
}

struct DynamicsOptions {
    std::string test = "held-suarez";
    std::uint32_t subdivision = 5;
    std::size_t layers = 3;
    double days = 10.0;
    double average_days = 0.0;
    std::optional<double> damping_hours;
    std::uint64_t seed = 1;
    std::size_t workers = 0;
    double lapse_rate_K_m = 0.0;   // rest test: 0 isothermal (250 K), else T(z) = 288 − Γ z
    std::size_t orography_passes = 0;
    std::optional<double> orography_step_m;
};

[[nodiscard]] DynamicsOptions parse_dynamics_options(int argument_count, char** arguments) {
    DynamicsOptions options;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--test") {
            options.test = std::string(value);
            if (options.test != "rest" && options.test != "held-suarez") {
                throw std::invalid_argument("dynamics tests are rest and held-suarez");
            }
        } else if (argument == "--subdivision") {
            options.subdivision = parse_subdivision(value);
        } else if (argument == "--layers") {
            options.layers = static_cast<std::size_t>(parse_unsigned(value, "layers"));
        } else if (argument == "--days") {
            options.days = parse_double(value, "days");
        } else if (argument == "--average-days") {
            options.average_days = parse_double(value, "average days");
        } else if (argument == "--damping-hours") {
            options.damping_hours = parse_double(value, "damping hours");
        } else if (argument == "--seed") {
            options.seed = parse_unsigned(value, "seed");
        } else if (argument == "--workers") {
            options.workers = static_cast<std::size_t>(parse_unsigned(value, "workers"));
        } else if (argument == "--orography-step") {
            options.orography_step_m = parse_double(value, "orography step");
        } else if (argument == "--orography-passes") {
            options.orography_passes =
                static_cast<std::size_t>(parse_unsigned(value, "orography passes"));
        } else if (argument == "--lapse-rate") {
            options.lapse_rate_K_m = parse_double(value, "lapse rate") / 1000.0;
        } else {
            throw std::invalid_argument("unknown dynamics option: " + std::string(argument));
        }
    }
    if (options.layers == 0U || !(options.days > 0.0) || options.average_days < 0.0 ||
        options.average_days > options.days) {
        throw std::invalid_argument("invalid dynamics options");
    }
    return options;
}

// ADR-0011 V4 (rest over terrain) and V5 (Held–Suarez) for the dry core.
int run_dynamics(const DynamicsOptions& options) {
    const std::size_t workers =
        options.workers != 0U ? options.workers
                              : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    const auto planet = planetsim::PlanetParameters::earth_development();
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, planet.radius_m));
    const auto grid = planetsim::CGridGeometry::build(*mesh);
    const std::size_t cells = mesh->cell_count();
    const std::size_t n = options.layers;
    const bool rest = options.test == "rest";

    planetsim::PrimitiveEquationParameters parameters;
    parameters.layer_count = n;
    parameters.gravity_m_s2 = planetsim::surface_gravity_m_s2(planet);
    parameters.rotation_rate_rad_s =
        2.0 * std::numbers::pi_v<double> / planet.sidereal_rotation_period_s;
    parameters.held_suarez.enabled = !rest;
    if (options.damping_hours) {
        parameters.hyperviscosity_m4_s =
            planetsim::hyperviscosity_for_damping_time(*mesh, *options.damping_hours * 3600.0);
    }

    planetsim::Field2D<double> height(cells, 0.0);
    planetsim::Field2D<double> surface_pressure(cells, parameters.reference_pressure_Pa);
    planetsim::Field3D<double> temperature(n, cells, 0.0);
    if (rest) {
        // Isothermal (250 K) at rest over the Earth-like terrain: hydrostatic
        // p_s = p₀ exp(−g z_s / (R T)) is the exact continuous rest state.
        planetsim::PlanetState state(mesh);
        static_cast<void>(planetsim::generate_terrain(
            state, options.seed,
            planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like), workers));
        const auto fractions = planetsim::compute_surface_fractions(
            *mesh, state.slow().hypsometry_m, state.slow().sea_level_m);
        planetsim::compute_surface_height(*mesh, state.slow(), fractions, height);
        if (options.orography_step_m) {
            auto limited = planetsim::limit_dynamics_orography_steps(
                *mesh, height, *options.orography_step_m,
                planetsim::dynamics_orography_max_passes, workers);
            height = std::move(limited.height_m);
            std::cout << "orography_step_limit_m=" << *options.orography_step_m
                      << " passes=" << limited.passes << '\n';
        } else {
            height = planetsim::smooth_dynamics_orography(*mesh, height,
                                                          options.orography_passes, workers);
        }
        double steepest = 0.0;
        for (const auto& edge : mesh->edges()) {
            steepest = std::max(steepest,
                                std::abs(height[edge.first_cell] - height[edge.second_cell]));
        }
        std::cout << "orography_passes=" << options.orography_passes
                  << " max_neighbour_step_m=" << steepest << '\n';
        const double g = parameters.gravity_m_s2;
        const double r = parameters.gas_constant_J_kg_K;
        const double p0 = parameters.reference_pressure_Pa;
        const double lapse = options.lapse_rate_K_m;
        for (std::size_t i = 0; i < cells; ++i) {
            if (lapse == 0.0) {
                surface_pressure[i] = p0 * std::exp(-g * height[i] / (r * 250.0));
                for (std::size_t k = 0; k < n; ++k) {
                    temperature.layer(k)[i] = 250.0;
                }
                continue;
            }
            // T(z) = 288 − Γ z, so T = 288 (p/p₀)^a with a = R Γ / g; a
            // layer's T is the exact mass mean over its pressure range.
            const double a = r * lapse / g;
            surface_pressure[i] = p0 * std::pow(1.0 - lapse * height[i] / 288.0, 1.0 / a);
            for (std::size_t k = 0; k < n; ++k) {
                const double nd = static_cast<double>(n);
                const double pb = surface_pressure[i] * (1.0 - static_cast<double>(k) / nd);
                const double pt = surface_pressure[i] * (1.0 - static_cast<double>(k + 1U) / nd);
                temperature.layer(k)[i] =
                    288.0 * (std::pow(pb / p0, a + 1.0) - std::pow(pt / p0, a + 1.0)) * p0 /
                    ((a + 1.0) * (pb - pt));
            }
        }
    } else {
        // Held–Suarez: rest at 300 K with a keyed 0.1 K perturbation to break
        // the symmetry.
        for (std::size_t i = 0; i < cells; ++i) {
            for (std::size_t k = 0; k < n; ++k) {
                temperature.layer(k)[i] =
                    300.0 + 0.2 * (planetsim::keyed_random_unit_double(
                                       options.seed, planetsim::RandomStreamId::validation,
                                       static_cast<std::uint32_t>(k),
                                       static_cast<std::uint32_t>(i)) -
                                   0.5);
            }
        }
    }
    const planetsim::PrimitiveEquationModel model(*mesh, grid, parameters, height);
    auto state = model.state_at_rest(surface_pressure, temperature);
    const auto initial = model.diagnose(state, workers);
    const planetsim::SubstepRule rule;

    // Time means of the cell winds over the averaging window, for zonal
    // means and transient eddy kinetic energy.
    std::vector<double> mean_east(n * cells, 0.0);
    std::vector<double> mean_square(n * cells, 0.0);
    std::size_t samples = 0;
    planetsim::EdgeField<double> layer_u(mesh->edge_count());
    planetsim::Field2D<double> east(cells);
    planetsim::Field2D<double> north(cells);

    std::cout << std::setprecision(6) << std::scientific << "dynamics test=" << options.test
              << " subdivision=" << options.subdivision << " layers=" << n
              << " steps_per_10_min=" << planetsim::substep_count(*mesh, 600.0, rule)
              << " hyperviscosity_m4_s=" << parameters.hyperviscosity_m4_s << '\n';
    const auto start = std::chrono::steady_clock::now();
    const auto whole_days = static_cast<std::size_t>(std::ceil(options.days));
    for (std::size_t day = 0; day < whole_days; ++day) {
        const double span = std::min(1.0, options.days - static_cast<double>(day)) * 86'400.0;
        model.advance(state, span, rule, workers);
        const double elapsed = static_cast<double>(day + 1U);
        if (elapsed > options.days - options.average_days + 1e-9) {
            for (std::size_t k = 0; k < n; ++k) {
                std::copy(state.normal_velocity_m_s.layer(k).begin(),
                          state.normal_velocity_m_s.layer(k).end(), layer_u.values().begin());
                planetsim::reconstruct_cell_vector(*mesh, grid, layer_u, east, north, workers);
                for (std::size_t i = 0; i < cells; ++i) {
                    mean_east[k * cells + i] += east[i];
                    mean_square[k * cells + i] += east[i] * east[i] + north[i] * north[i];
                }
            }
            ++samples;
        }
        if (rest || (day + 1U) % 50U == 0U || day + 1U == whole_days) {
            const auto now = model.diagnose(state, workers);
            std::cout << "day=" << day + 1U << " max_wind_m_s=" << now.max_wind_m_s
                      << " kinetic_J=" << now.kinetic_energy_J << '\n';
        }
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto final = model.diagnose(state, workers);
    std::cout << "wall_s=" << seconds << " relative_mass_change="
              << (final.mass_kg - initial.mass_kg) / initial.mass_kg
              << " relative_energy_change="
              << (final.energy_J() - initial.energy_J()) / initial.energy_J() << '\n';

    if (samples > 0U) {
        // Zonal means in 5° bands, area-weighted, of the time-mean eastward
        // wind and the transient eddy kinetic energy ½(|u|² mean − ū²).
        constexpr int bands = 36;
        std::vector<double> band_area(bands, 0.0);
        std::vector<double> band_u(n * bands, 0.0);
        std::vector<double> band_eke(n * bands, 0.0);
        for (const auto& cell : mesh->cells()) {
            const double latitude = planetsim::latitude_rad(cell.center_unit);
            const int band = std::clamp(
                static_cast<int>((latitude + std::numbers::pi_v<double> / 2.0) /
                                 (std::numbers::pi_v<double> / bands)),
                0, bands - 1);
            band_area[static_cast<std::size_t>(band)] += cell.area_m2;
            for (std::size_t k = 0; k < n; ++k) {
                const std::size_t index = k * cells + cell.id.to_index();
                const double mean = mean_east[index] / static_cast<double>(samples);
                const double square = mean_square[index] / static_cast<double>(samples);
                band_u[k * bands + static_cast<std::size_t>(band)] += cell.area_m2 * mean;
                band_eke[k * bands + static_cast<std::size_t>(band)] +=
                    cell.area_m2 * 0.5 * std::max(0.0, square - mean * mean);
            }
        }
        std::cout << std::fixed << std::setprecision(2) << "averaged_days=" << samples << '\n'
                  << "latitude_deg";
        for (std::size_t k = 0; k < n; ++k) {
            std::cout << " u_layer" << k << " eke_layer" << k;
        }
        std::cout << '\n';
        for (int band = 0; band < bands; ++band) {
            std::cout << -90.0 + 5.0 * (band + 0.5);
            for (std::size_t k = 0; k < n; ++k) {
                const double area = band_area[static_cast<std::size_t>(band)];
                std::cout << ' ' << band_u[k * bands + static_cast<std::size_t>(band)] / area
                          << ' ' << band_eke[k * bands + static_cast<std::size_t>(band)] / area;
            }
            std::cout << '\n';
        }
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

// The climate-mode forcing of one sub-step (ADR-0006 §4.3): its span and the
// global, zonal and extreme sub-step mean insolation.
int run_solar_substep(const planetsim::PlanetParameters& parameters, std::int64_t index) {
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(parameters.mesh_subdivision, parameters.radius_m));
    planetsim::PlanetState state(mesh);
    const auto substep = planetsim::climate_substep(index, parameters);
    const auto start = std::chrono::steady_clock::now();
    planetsim::update_substep_mean_insolation(state, parameters, substep);
    const auto finish = std::chrono::steady_clock::now();

    const auto& field = state.forcing().substep_mean_insolation_W_m2;
    constexpr int band_count = 18;
    std::array<double, band_count> band_power{};
    std::array<double, band_count> band_area{};
    double total_power = 0.0;
    double total_area = 0.0;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    bool valid = true;
    for (const auto& cell : mesh->cells()) {
        const double value = field[cell.id];
        valid = valid && std::isfinite(value) && value >= 0.0;
        const double latitude_deg =
            planetsim::latitude_rad(cell.center_unit) * 180.0 / std::numbers::pi_v<double>;
        const int band = std::clamp(static_cast<int>((latitude_deg + 90.0) / 10.0), 0,
                                    band_count - 1);
        band_power[static_cast<std::size_t>(band)] += cell.area_m2 * value;
        band_area[static_cast<std::size_t>(band)] += cell.area_m2;
        total_power += cell.area_m2 * value;
        total_area += cell.area_m2;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    std::cout << std::setprecision(9) << "subdivision: " << parameters.mesh_subdivision << '\n'
              << "substep_index: " << substep.index << '\n'
              << "substep_month: " << substep.month << '\n'
              << "substep_begin_tick: " << substep.begin_tick << '\n'
              << "substep_end_tick: " << substep.end_tick << '\n'
              << "substep_length_ticks: " << substep.length_ticks() << '\n'
              << "quadrature_nodes: " << planetsim::substep_forcing_quadrature_nodes << '\n'
              << "global_mean_insolation_W_m2: " << total_power / total_area << '\n'
              << "min_insolation_W_m2: " << minimum << '\n'
              << "max_insolation_W_m2: " << maximum << '\n';
    for (int band = 0; band < band_count; ++band) {
        std::cout << "zonal_mean_" << (-90 + 10 * band) << "_" << (-80 + 10 * band)
                  << "_W_m2: " << band_power[static_cast<std::size_t>(band)] /
                                      band_area[static_cast<std::size_t>(band)]
                  << '\n';
    }
    std::cout << "substep_forcing_valid: " << (valid ? "true" : "false") << '\n'
              << "evaluation_time_ms: "
              << std::chrono::duration<double, std::milli>(finish - start).count() << '\n';
    return valid ? 0 : 2;
}

int run_solar(const SolarOptions& options) {
    auto parameters = planetsim::PlanetParameters::earth_development();
    parameters.mesh_subdivision = options.subdivision;
    parameters.radius_m = options.radius_m;
    parameters.validate();
    if (options.substep) {
        return run_solar_substep(parameters, *options.substep);
    }
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
                  << (descriptor.scenario_layered() ? std::string("scenario")
                                                    : std::to_string(descriptor.layers))
                  << '\t' << descriptor.units << '\n';
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
    planetsim::SimulationTick tick = snapshot_synthetic_tick;
    const auto start = std::chrono::steady_clock::now();
    if (options.delta_of.empty()) {
        planetsim::write_snapshot(options.output_path, state, tick, {}, options.compression);
    } else {
        // The golden delta (task M4-05): every third cell's land surface
        // 0.5 K warmer and every second cell's sea ice 1 kg/m² heavier, a
        // tick later; terrain and the other fields unchanged.
        planetsim::SnapshotManifest parent_manifest;
        const auto parent =
            planetsim::read_snapshot_chunks(options.delta_of, nullptr, parent_manifest);
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            if (cell % 3U == 0U) {
                state.slow().land_surface_temperature_K[cell] += 0.5F;
            }
            if (cell % 2U == 0U) {
                state.slow().sea_ice_mass_kg_m2[cell] += 1.0;
            }
        }
        tick += 1;
        static_cast<void>(planetsim::write_delta_snapshot(
            options.output_path, state, tick, options.delta_of.stem().string(), parent));
    }
    const auto finish = std::chrono::steady_clock::now();

    std::cout << std::setprecision(17)
              << "snapshot_written: " << options.output_path.string() << '\n'
              << "subdivision: " << options.subdivision << '\n'
              << "cell_count: " << mesh->cell_count() << '\n'
              << "simulation_tick: " << tick << '\n'
              << "size_bytes: " << std::filesystem::file_size(options.output_path) << '\n'
              << "write_time_ms: "
              << std::chrono::duration<double, std::milli>(finish - start).count() << '\n';
    return 0;
}

int run_snapshot_inspect(const std::filesystem::path& path) {
    const auto manifest = planetsim::inspect_snapshot(path);
    std::cout << "format: " << manifest.format << '\n'
              << "kind: " << (manifest.delta ? "delta" : "full") << '\n'
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

// Prints twelve consecutive climate sub-steps of the ADR-0006 calendar for
// the default Earth: those of orbital year N, or those starting with the
// sub-step that contains tick T.
int run_calendar(int argument_count, char** arguments) {
    std::int64_t first_index = 0;
    for (int index = 2; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (++index >= argument_count) {
            throw std::invalid_argument(std::string(argument) + " requires a value");
        }
        const std::string_view value{arguments[index]};
        if (argument == "--year") {
            const std::int64_t year = parse_int64(value, "year");
            if (year > std::numeric_limits<std::int64_t>::max() / 12 ||
                year < std::numeric_limits<std::int64_t>::min() / 12) {
                throw std::invalid_argument("year is out of range");
            }
            first_index = year * planetsim::climate_substeps_per_year;
        } else if (argument == "--from-tick") {
            const auto parameters = planetsim::PlanetParameters::earth_development();
            first_index =
                planetsim::climate_substep_containing(parse_int64(value, "tick"), parameters).index;
        } else {
            throw std::invalid_argument("unknown calendar option: " + std::string(argument));
        }
    }

    const auto parameters = planetsim::PlanetParameters::earth_development();
    const auto calendar = planetsim::make_orbital_calendar(parameters);
    std::cout << std::setprecision(12)
              << "orbital_period_s: " << calendar.orbital_period_s << '\n'
              << "orbital_period_ticks: "
              << calendar.orbital_period_s /
                     static_cast<double>(planetsim::simulation_seconds_per_tick)
              << '\n'
              << "initial_orbital_phase_rad: " << calendar.initial_orbital_phase_rad << '\n'
              << "index month begin_tick end_tick length_ticks begin_day\n";
    for (std::int64_t offset = 0; offset < planetsim::climate_substeps_per_year; ++offset) {
        const auto substep = planetsim::climate_substep(calendar, first_index + offset);
        std::cout << substep.index << ' ' << substep.month << ' ' << substep.begin_tick << ' '
                  << substep.end_tick << ' ' << substep.length_ticks() << ' ' << std::fixed
                  << std::setprecision(4)
                  << static_cast<double>(substep.begin_tick) /
                         static_cast<double>(24 * 60)
                  << std::defaultfloat << std::setprecision(12) << '\n';
    }
    return 0;
}

}  // namespace

// The surface energy columns of ADR-0007 on a generated planet: spin-up from
// the equilibrium initial state, the last year's budget, per-sub-step cost
// (V10) and, with --calibrate, the bisection fit of g (specification §24).
// ADR-0003 V7: a decade history of the Earth-like planet. Prints the full
// snapshot's raw and compressed size and its write time, every decade's
// delta size against the full snapshot's, and the time to reconstruct the
// deepest snapshot of the chain.
// The atmosphere at hydrostatic rest over a generated Earth-like planet
// (ADR-0010 §4.3, task M5-02): mass, surface and sea-level pressure, and the
// mean temperature, pressure and height of every layer.
int run_atmosphere(const AtmosphereOptions& options) {
    const std::size_t worker_count =
        options.worker_count != 0U ? options.worker_count
                                   : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    const auto parameters = planetsim::PlanetParameters::earth_development();
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, options.seed, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like),
        worker_count));
    planetsim::initialise_surface_temperatures(
        *mesh, state.slow(), parameters,
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like),
        worker_count);
    planetsim::initialise_cryosphere(*mesh, state.slow());
    auto atmosphere = planetsim::atmosphere_parameters_for(planetsim::PlanetPreset::earth_like);
    atmosphere.layer_count = options.layers;
    planetsim::initialise_atmosphere(*mesh, state.slow(), parameters, atmosphere);
    const auto diagnostics = planetsim::diagnose_atmosphere(*mesh, state.slow(), parameters,
                                                            atmosphere, worker_count);
    std::cout << std::setprecision(10) << "atmosphere subdivision=" << options.subdivision
              << " seed=" << options.seed << " layers=" << diagnostics.layer_count
              << " gravity_m_s2=" << planetsim::surface_gravity_m_s2(parameters) << '\n'
              << "mass_kg=" << diagnostics.mass_kg
              << " surface_pressure_Pa mean=" << diagnostics.mean_surface_pressure_Pa
              << " min=" << diagnostics.min_surface_pressure_Pa
              << " max=" << diagnostics.max_surface_pressure_Pa << '\n'
              << "sea_level_pressure_Pa mean=" << diagnostics.mean_sea_level_pressure_Pa
              << " min=" << diagnostics.min_sea_level_pressure_Pa
              << " max=" << diagnostics.max_sea_level_pressure_Pa
              << " surface_air_K=" << diagnostics.mean_surface_air_temperature_K << '\n';
    for (std::size_t layer = 0; layer < diagnostics.layer_count; ++layer) {
        std::cout << "layer=" << layer
                  << " sigma=" << planetsim::layer_sigma(layer, diagnostics.layer_count)
                  << " pressure_Pa=" << diagnostics.mean_layer_pressure_Pa[layer]
                  << " temperature_K=" << diagnostics.mean_layer_temperature_K[layer]
                  << " height_m=" << diagnostics.mean_layer_height_m[layer] << '\n';
    }
    return 0;
}

int run_history(const HistoryOptions& options) {
    const std::size_t worker_count =
        options.worker_count != 0U ? options.worker_count
                                   : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    const auto parameters = planetsim::PlanetParameters::earth_development();
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, options.seed, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like),
        worker_count));
    const auto fractions = planetsim::compute_surface_fractions(
        *mesh, state.slow().hypsometry_m, state.slow().sea_level_m, worker_count);
    const auto surface =
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);
    planetsim::initialise_climate(*mesh, state.slow(), parameters, surface, worker_count);
    if (options.spin_up_years > 0) {
        static_cast<void>(planetsim::spin_up_surface_energy(state, parameters, surface, fractions,
                                                            options.spin_up_years, worker_count));
    }

    const auto directory = options.directory.empty()
                               ? std::filesystem::temp_directory_path() / "planetsim_history"
                               : options.directory;
    std::filesystem::remove_all(directory);
    planetsim::HistoryStore store(directory, options.base_interval);
    const auto milliseconds = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    std::cout << std::setprecision(6) << "history subdivision=" << options.subdivision
              << " cells=" << mesh->cell_count() << " spin_up_years=" << options.spin_up_years
              << " decades=" << options.decades << " base_interval=" << options.base_interval
              << " raw_bytes=" << planetsim::encode_snapshot_chunks(state).size_bytes() << '\n';
    std::string parent;
    double fastest_full_ms = 1e300;
    for (int decade = 0; decade <= options.decades; ++decade) {
        if (decade > 0) {
            static_cast<void>(planetsim::spin_up_surface_energy(state, parameters, surface,
                                                                fractions, 10, worker_count));
        }
        const auto tick = static_cast<planetsim::SimulationTick>(decade) * 5'259'487;
        // The full snapshot's write time, best of three (the gate measures the
        // writer, not the file system's first touch).
        const auto full_path = directory / "full_probe.psnap";
        for (int repeat = 0; repeat < 3; ++repeat) {
            const auto start = std::chrono::steady_clock::now();
            planetsim::write_snapshot(full_path, state, tick);
            fastest_full_ms =
                std::min(fastest_full_ms, milliseconds(start, std::chrono::steady_clock::now()));
        }
        const auto full_bytes = std::filesystem::file_size(full_path);
        const auto start = std::chrono::steady_clock::now();
        parent = store.save(state, tick, parent);
        const double save_ms = milliseconds(start, std::chrono::steady_clock::now());
        const auto& entry = store.entry(parent);
        std::cout << "decade=" << decade << " kind=" << (entry.delta ? "delta" : "full")
                  << " depth=" << entry.depth << " bytes=" << entry.file_bytes
                  << " full_bytes=" << full_bytes << " ratio="
                  << static_cast<double>(entry.file_bytes) / static_cast<double>(full_bytes)
                  << " save_ms=" << save_ms << '\n';
    }
    // Reconstruct the deepest snapshot from a fresh store (no cache).
    std::string deepest = parent;
    std::uint32_t depth = 0;
    for (const auto& entry : store.entries()) {
        if (entry.depth >= depth) {
            depth = entry.depth;
            deepest = entry.id;
        }
    }
    planetsim::HistoryStore reopened(directory, options.base_interval);
    planetsim::PlanetState loaded(mesh);
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(reopened.load(deepest, loaded));
    std::cout << "full_write_ms=" << fastest_full_ms << " load_depth=" << depth
              << " load_ms=" << milliseconds(start, std::chrono::steady_clock::now()) << '\n';
    return 0;
}

int run_thermal(const ThermalOptions& options) {
    const std::size_t worker_count =
        options.worker_count != 0U ? options.worker_count
                                   : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    auto parameters = planetsim::PlanetParameters::earth_development();
    if (options.preset == planetsim::PlanetPreset::dead_rock) {
        parameters.axial_tilt_rad = 0.0;   // experiment A (specification §13.1)
    }
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(options.subdivision, 6'371'000.0));
    // Every spin-up starts by reinitialising the temperatures, so one state
    // serves the calibration and the final run.
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, options.seed, planetsim::geology_parameters_for(options.preset), worker_count));
    const auto fractions = planetsim::compute_surface_fractions(
        *mesh, state.slow().hypsometry_m, state.slow().sea_level_m, worker_count);

    auto surface = planetsim::surface_energy_parameters_for(options.preset);
    if (options.grey_emissivity) {
        surface.grey_emissivity = *options.grey_emissivity;
    }
    if (options.transport_W_m2_K) {
        surface.transport_coefficient_W_m2_K = *options.transport_W_m2_K;
    }
    if (options.atmosphere_layers) {
        surface.grey_emissivity = 0.0;
        surface.atmosphere.layer_count = *options.atmosphere_layers;
    }
    if (options.optical_depth) {
        surface.atmosphere.longwave_optical_depth = *options.optical_depth;
    }
    const bool layered = surface.atmosphere.layer_count > 0U;
    auto& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
    for (std::size_t cell = 0; cell < precipitation.size(); ++cell) {
        precipitation[cell] = static_cast<float>(options.precipitation_kg_m2_s);
    }
    const auto spin_up = [&](const planetsim::SurfaceEnergyParameters& candidate) {
        planetsim::initialise_climate(*mesh, state.slow(), parameters, candidate, worker_count);
        return planetsim::spin_up_surface_energy(state, parameters, candidate, fractions,
                                                 options.years, worker_count);
    };

    std::cout << std::setprecision(9) << "preset=" << planetsim::planet_preset_name(options.preset)
              << " subdivision=" << options.subdivision << " cells=" << mesh->cell_count()
              << " seed=" << options.seed << " years=" << options.years
              << " axial_tilt_deg=" << parameters.axial_tilt_rad * 180.0 / std::numbers::pi
              << " land_area_fraction=" << fractions.land_area_fraction << '\n';

    // Bisection of one parameter so that a spin-up statistic reaches its
    // target; the statistic increases with the parameter.
    // `parameter` returns the fitted member of a parameter set.
    const auto bisect = [&](double low, double high, double target, double tolerance,
                            const auto& parameter, const auto& statistic, std::string_view name) {
        for (int iteration = 0; iteration < 40; ++iteration) {
            auto candidate = surface;
            parameter(candidate) = 0.5 * (low + high);
            const double value = statistic(spin_up(candidate));
            std::cout << "calibrate " << name << "=" << parameter(candidate)
                      << " value=" << value << '\n';
            (value < target ? low : high) = parameter(candidate);
            if (std::abs(value - target) <= tolerance) {
                break;
            }
        }
        parameter(surface) = 0.5 * (low + high);
    };
    const auto transport = [](planetsim::SurfaceEnergyParameters& p) -> double& {
        return p.transport_coefficient_W_m2_K;
    };
    const auto grey = [](planetsim::SurfaceEnergyParameters& p) -> double& {
        return p.grey_emissivity;
    };
    const auto optical_depth = [](planetsim::SurfaceEnergyParameters& p) -> double& {
        return p.atmosphere.longwave_optical_depth;
    };
    const auto mean_K = [](const planetsim::AnnualSurfaceSummary& year) {
        return year.mean_surface_temperature_K;
    };
    const auto peak_PW = [](const planetsim::AnnualSurfaceSummary& year) {
        return year.peak_poleward_transport_W() / 1e15;
    };
    // D and g are fitted alternately (ADR-0009 §4.4): transport changes the
    // global mean through the T⁴ nonlinearity, g the gradient that drives it.
    // The P2 difference falls as D grows; bisection wants a rising statistic.
    const auto negative_gradient = [](const planetsim::AnnualSurfaceSummary& year) {
        return -year.p2_equator_to_pole_K();
    };
    for (int round = 0; round < 6 && (options.calibrate_K || options.calibrate_transport_PW ||
                                      options.calibrate_gradient_K);
         ++round) {
        if (options.calibrate_transport_PW) {
            bisect(0.3, 2.5, *options.calibrate_transport_PW, 0.02, transport, peak_PW, "D");
        }
        if (options.calibrate_gradient_K) {
            bisect(0.02, 1.5, -*options.calibrate_gradient_K, 0.05, transport,
                   negative_gradient, "D");
        }
        if (options.calibrate_K) {
            if (layered) {
                bisect(0.2, 12.0, *options.calibrate_K, 0.02, optical_depth, mean_K, "tau");
            } else {
                bisect(0.2, 0.8, *options.calibrate_K, 0.02, grey, mean_K, "g");
            }
        }
        const auto year = spin_up(surface);
        const bool transport_fits =
            (!options.calibrate_transport_PW ||
             std::abs(peak_PW(year) - *options.calibrate_transport_PW) <= 0.05) &&
            (!options.calibrate_gradient_K ||
             std::abs(year.p2_equator_to_pole_K() - *options.calibrate_gradient_K) <= 0.1);
        const bool mean_fits =
            !options.calibrate_K || std::abs(mean_K(year) - *options.calibrate_K) <= 0.05;
        std::cout << "calibrated round=" << round << std::setprecision(6)
                  << " g=" << surface.grey_emissivity
                  << " tau=" << surface.atmosphere.longwave_optical_depth
                  << " D=" << surface.transport_coefficient_W_m2_K << std::setprecision(9)
                  << " mean_K=" << mean_K(year) << " peak_PW=" << peak_PW(year)
                  << " p2_equator_to_pole_K=" << year.p2_equator_to_pole_K() << '\n';
        if (transport_fits && mean_fits) {
            break;
        }
    }

    const auto spin_start = std::chrono::steady_clock::now();
    const auto year = spin_up(surface);
    const auto spin_finish = std::chrono::steady_clock::now();
    const double substep_ms =
        std::chrono::duration<double, std::milli>(spin_finish - spin_start).count() /
        static_cast<double>(options.years * planetsim::climate_substeps_per_year);
    std::cout << "g=" << surface.grey_emissivity
              << " layers=" << surface.atmosphere.layer_count
              << " tau=" << surface.atmosphere.longwave_optical_depth
              << " D=" << surface.transport_coefficient_W_m2_K
              << " mean_K=" << year.mean_surface_temperature_K
              << " land_mean_K=" << year.land_mean_surface_temperature_K
              << " ocean_mean_K=" << year.ocean_mean_surface_temperature_K
              << " absorbed_W=" << year.absorbed_W << " emitted_W=" << year.emitted_W
              << " relative_imbalance=" << year.relative_imbalance() << '\n'
              << "transport peak_poleward_PW=" << peak_PW(year)
              << " equator_to_pole_K=" << year.equator_to_pole_difference_K()
              << " p2_equator_to_pole_K=" << year.p2_equator_to_pole_K() << " northward_PW";
    for (const double value : year.northward_transport_W) {
        std::cout << ' ' << std::setprecision(3) << value / 1e15;
    }
    std::cout << std::setprecision(9) << '\n' << "zonal_mean_K";
    for (const double value : year.zonal_mean_surface_temperature_K) {
        std::cout << ' ' << std::setprecision(4) << value;
    }
    std::cout << std::setprecision(9) << '\n'
              << "snow precipitation_kg_m2_s=" << options.precipitation_kg_m2_s
              << " snowfall_kg=" << year.snowfall_kg << " rain_kg=" << year.rain_kg
              << " melt_kg=" << year.melt_kg << " snow_end_kg=" << year.snow_kg << '\n'
              << "sea_ice ice_end_kg=" << year.ice_kg
              << " north_area_min_max_km2=" << year.ice_area_north_min_m2 / 1e6 << ' '
              << year.ice_area_north_max_m2 / 1e6
              << " south_area_min_max_km2=" << year.ice_area_south_min_m2 / 1e6 << ' '
              << year.ice_area_south_max_m2 / 1e6 << '\n'
              << "snow_cover north_area_min_max_km2=" << year.snow_area_north_min_m2 / 1e6
              << ' ' << year.snow_area_north_max_m2 / 1e6
              << " south_area_min_max_km2=" << year.snow_area_south_min_m2 / 1e6 << ' '
              << year.snow_area_south_max_m2 / 1e6 << '\n'
              << "closure worst_ratio=" << year.worst_closure_ratio << '\n';
    if (layered) {
        std::cout << "atmosphere layer_mean_K";
        for (std::size_t layer = 0; layer < surface.atmosphere.layer_count; ++layer) {
            std::cout << ' ' << std::setprecision(5) << year.mean_layer_temperature_K[layer];
        }
        const double area = 4.0 * std::numbers::pi * mesh->radius_m() * mesh->radius_m();
        std::cout << std::setprecision(6) << " olr_W_m2=" << year.emitted_W / area
                  << " surface_up_W_m2=" << year.surface_upward_longwave_W / area
                  << " down_W_m2=" << year.downward_longwave_W / area
                  << " sensible_W_m2=" << year.sensible_heat_W / area
                  << " convective_fraction=" << year.convective_area_fraction
                  << " max_column_residual_W_m2=" << year.max_column_residual_W_m2
                  << " max_column_correction_K=" << year.max_column_correction_K
                  << " max_column_iterations=" << year.max_column_iterations
                  << " unconverged=" << year.unconverged_columns << '\n';
    }
    std::cout << std::setprecision(9) << "timing workers=" << worker_count
              << " ms_per_substep=" << substep_ms << '\n';
    return 0;
}

[[nodiscard]] std::string hash_hex(std::uint64_t hash) {
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

int run_scenario(const RunOptions& options) {
    const std::size_t worker_count =
        options.worker_count != 0U ? options.worker_count
                                   : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    const auto setup_start = std::chrono::steady_clock::now();
    planetsim::PlanetRun run(options.scenario, worker_count);
    for (const auto& command : options.commands) {
        run.submit(command);
    }
    const auto run_start = std::chrono::steady_clock::now();
    const auto end_tick = planetsim::orbital_year_begin_tick(options.years, run.parameters());
    run.run_until(end_tick);
    const auto run_finish = std::chrono::steady_clock::now();

    const double setup_s = std::chrono::duration<double>(run_start - setup_start).count();
    const double run_s = std::chrono::duration<double>(run_finish - run_start).count();
    const double total_s = setup_s + run_s;
    const double years_per_minute = run_s > 0.0 ? options.years * 60.0 / run_s : 0.0;
    const auto manifest = run.manifest();
    const auto& last = run.last_step();

    std::cout << std::setprecision(9) << "scenario";
    for (const auto& [key, value] : manifest.scenario) {
        std::cout << ' ' << key << '=' << value;
    }
    std::cout << " cells=" << run.state().mesh().cell_count() << '\n'
              << "run years=" << options.years << " end_tick=" << run.tick()
              << " steps=" << run.scheduler().step_count()
              << " commands=" << manifest.commands.size()
              << " checkpoints=" << manifest.checkpoints.size()
              << " state_hash=" << hash_hex(run.state_hash()) << '\n'
              << "last_step mean_K=" << last.mean_surface_temperature_K
              << " closure_residual_J=" << last.closure_residual_J()
              << " max_newton_residual_W_m2=" << last.max_newton_residual_W_m2 << '\n'
              << "timing workers=" << worker_count << " setup_s=" << setup_s
              << " run_s=" << run_s << " total_s=" << total_s
              << " years_per_minute=" << years_per_minute << '\n';

    if (options.manifest_path) {
        planetsim::write_run_manifest(*options.manifest_path, manifest);
        std::cout << "manifest_written: " << options.manifest_path->string() << '\n';
    }
    if (options.snapshot_path) {
        planetsim::write_snapshot(*options.snapshot_path, run.state(), run.tick());
        std::cout << "snapshot_written: " << options.snapshot_path->string() << '\n';
    }

    bool gate_passed = true;
    if (options.min_years_per_minute) {
        const bool passed = years_per_minute >= *options.min_years_per_minute;
        std::cout << "gate years_per_minute>=" << *options.min_years_per_minute << ": "
                  << (passed ? "passed" : "FAILED") << '\n';
        gate_passed = gate_passed && passed;
    }
    if (options.max_seconds) {
        const bool passed = total_s <= *options.max_seconds;
        std::cout << "gate total_s<=" << *options.max_seconds << ": "
                  << (passed ? "passed" : "FAILED") << '\n';
        gate_passed = gate_passed && passed;
    }
    return gate_passed ? 0 : 3;
}

int run_replay(int argument_count, char** arguments) {
    if (argument_count < 3) {
        throw std::invalid_argument("replay requires a run manifest");
    }
    std::size_t worker_count = std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    for (int index = 3; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument != "--workers" || ++index >= argument_count) {
            throw std::invalid_argument("replay accepts only --workers W");
        }
        worker_count = static_cast<std::size_t>(parse_unsigned(arguments[index], "worker count"));
        if (worker_count == 0U) {
            throw std::invalid_argument("worker count must be positive");
        }
    }
    const auto manifest = planetsim::read_run_manifest(arguments[2]);
    const auto engine = planetsim::snapshot_engine_version();
    std::cout << "manifest engine_version=" << manifest.engine_version
              << " this_build=" << engine << " commands=" << manifest.commands.size()
              << " checkpoints=" << manifest.checkpoints.size()
              << " end_tick=" << manifest.end_tick.value_or(-1) << '\n';
    if (manifest.engine_version != engine) {
        std::cout << "warning: different engine version; only L1 (statistical) equivalence is "
                     "guaranteed across builds (ADR-0003 §3.1)\n";
    }
    const auto start = std::chrono::steady_clock::now();
    const auto result = planetsim::replay_run(manifest, worker_count);
    const double replay_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "replay workers=" << worker_count << " checkpoints_compared="
              << result.checkpoints_compared << " end_tick=" << result.end_tick
              << " state_hash=" << hash_hex(result.final_state_hash) << " time_s=" << replay_s
              << '\n';
    if (result.matched) {
        std::cout << "replay matched\n";
        return 0;
    }
    std::cout << "replay DIVERGED first_divergence_tick=" << *result.first_divergence_tick
              << " day=" << planetsim::simulation_time_s(*result.first_divergence_tick) / 86'400.0
              << '\n';
    return 4;
}

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
        if (command == "atmosphere") {
            return run_atmosphere(parse_atmosphere_options(argument_count, arguments));
        }
        if (command == "history") {
            return run_history(parse_history_options(argument_count, arguments));
        }
        if (command == "thermal") {
            return run_thermal(parse_thermal_options(argument_count, arguments));
        }
        if (command == "run") {
            return run_scenario(parse_run_options(argument_count, arguments));
        }
        if (command == "dynamics") {
            return run_dynamics(parse_dynamics_options(argument_count, arguments));
        }
        if (command == "shallow-water") {
            return run_shallow_water(parse_shallow_water_options(argument_count, arguments));
        }
        if (command == "replay") {
            return run_replay(argument_count, arguments);
        }
        if (command == "calendar") {
            return run_calendar(argument_count, arguments);
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
