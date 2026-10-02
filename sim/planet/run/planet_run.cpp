#include "sim/planet/run/planet_run.hpp"

#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace planetsim {
namespace {

constexpr std::uint32_t max_run_subdivision = 7U;
constexpr double max_solar_luminosity_factor = 10.0;

template <typename Integer>
[[nodiscard]] Integer parse_scenario_integer(std::string_view key, std::string_view text) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("scenario " + std::string(key) + " is not an integer: '" +
                                 std::string(text) + "'");
    }
    return value;
}

[[nodiscard]] std::optional<SimulationMode> parse_run_mode(std::string_view text) noexcept {
    if (text == simulation_mode_name(SimulationMode::climate)) {
        return SimulationMode::climate;
    }
    if (text == simulation_mode_name(SimulationMode::reference)) {
        return SimulationMode::reference;
    }
    return std::nullopt;
}

[[nodiscard]] double parse_luminosity_factor(std::string_view text) {
    double factor = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), factor);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(factor) || factor <= 0.0 || factor > max_solar_luminosity_factor) {
        throw std::invalid_argument("solar luminosity factor must be a decimal in (0, 10]: '" +
                                    std::string(text) + "'");
    }
    return factor;
}

[[nodiscard]] PlanetParameters scenario_parameters(const Scenario& scenario) {
    auto parameters = PlanetParameters::earth_development();
    parameters.mesh_subdivision = scenario.subdivision;
    if (scenario.preset == PlanetPreset::dead_rock) {
        parameters.axial_tilt_rad = 0.0;   // experiment A (specification §13.1)
    }
    return parameters;
}

// The preset's surface with the scenario's atmosphere layer count.
[[nodiscard]] SurfaceEnergyParameters scenario_surface_parameters(const Scenario& scenario) {
    SurfaceEnergyParameters surface = surface_energy_parameters_for(scenario.preset);
    surface.atmosphere.layer_count = scenario.resolved_atmosphere_layers();
    return surface;
}

[[nodiscard]] const Scenario& validated(const Scenario& scenario) {
    if (scenario.subdivision > max_run_subdivision) {
        throw std::invalid_argument("run subdivision must be at most 7");
    }
    if (scenario.spin_up_years < 0) {
        throw std::invalid_argument("spin-up years must be non-negative");
    }
    if (scenario.initial_mode != SimulationMode::climate &&
        scenario.initial_mode != SimulationMode::reference) {
        throw std::invalid_argument("a run starts in climate or reference mode");
    }
    if (scenario.atmosphere_layers) {
        const std::uint32_t preset_layers = atmosphere_parameters_for(scenario.preset).layer_count;
        if (*scenario.atmosphere_layers > max_atmosphere_layer_count ||
            (preset_layers == 0U) != (*scenario.atmosphere_layers == 0U)) {
            throw std::invalid_argument(
                "atmosphere_layers must lie in 1.." + std::to_string(max_atmosphere_layer_count) +
                " for a preset with an atmosphere, and be 0 for one without");
        }
    }
    return scenario;
}

}  // namespace

std::uint32_t Scenario::resolved_atmosphere_layers() const noexcept {
    return atmosphere_layers ? *atmosphere_layers : atmosphere_parameters_for(preset).layer_count;
}

ScenarioEntries Scenario::entries() const {
    return {
        {"preset", std::string(planet_preset_name(preset))},
        {"seed", std::to_string(seed)},
        {"subdivision", std::to_string(subdivision)},
        {"spin_up_years", std::to_string(spin_up_years)},
        {"initial_mode", std::string(simulation_mode_name(initial_mode))},
        {"atmosphere_layers", std::to_string(resolved_atmosphere_layers())},
    };
}

Scenario Scenario::from_entries(const ScenarioEntries& entries) {
    Scenario scenario;
    auto defaults = scenario.entries();
    // atmosphere_layers is optional (manifests before M5).
    if (!scenario_value(entries, "atmosphere_layers")) {
        defaults.pop_back();
    }
    if (entries.size() != defaults.size()) {
        throw std::runtime_error("scenario has " + std::to_string(entries.size()) +
                                 " entries; this build expects " +
                                 std::to_string(defaults.size()));
    }
    for (const auto& [key, unused] : defaults) {
        static_cast<void>(unused);
        if (!scenario_value(entries, key)) {
            throw std::runtime_error("scenario lacks " + key);
        }
    }
    const auto value = [&](std::string_view key) { return *scenario_value(entries, key); };
    const auto preset = parse_planet_preset(value("preset"));
    if (!preset) {
        throw std::runtime_error("unknown scenario preset: " + std::string(value("preset")));
    }
    scenario.preset = *preset;
    scenario.seed = parse_scenario_integer<std::uint64_t>("seed", value("seed"));
    scenario.subdivision = parse_scenario_integer<std::uint32_t>("subdivision", value("subdivision"));
    scenario.spin_up_years = parse_scenario_integer<int>("spin_up_years", value("spin_up_years"));
    const auto mode = parse_run_mode(value("initial_mode"));
    if (!mode) {
        throw std::runtime_error("unknown scenario initial_mode: " +
                                 std::string(value("initial_mode")));
    }
    scenario.initial_mode = *mode;
    if (const auto layers = scenario_value(entries, "atmosphere_layers")) {
        scenario.atmosphere_layers =
            parse_scenario_integer<std::uint32_t>("atmosphere_layers", *layers);
    }
    try {
        static_cast<void>(validated(scenario));
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(error.what());
    }
    return scenario;
}

SimulationTick orbital_year_begin_tick(std::int64_t year, const PlanetParameters& parameters) {
    return climate_substep(year * climate_substeps_per_year, parameters).begin_tick;
}

PlanetRun::PlanetRun(const Scenario& scenario, std::size_t worker_count)
    : scenario_(validated(scenario)),
      worker_count_(std::max<std::size_t>(1U, worker_count)),
      base_parameters_(scenario_parameters(scenario_)),
      parameters_(base_parameters_),
      surface_(scenario_surface_parameters(scenario_)),
      mesh_(std::make_shared<const PlanetMesh>(
          make_icosphere(scenario_.subdivision, base_parameters_.radius_m))),
      state_(mesh_) {
    static_cast<void>(generate_terrain(state_, scenario_.seed,
                                       geology_parameters_for(scenario_.preset), worker_count_));
    fractions_ = compute_surface_fractions(*mesh_, state_.slow().hypsometry_m,
                                           state_.slow().sea_level_m, worker_count_);
    initialise_climate(*mesh_, state_.slow(), parameters_, surface_, worker_count_);
    if (scenario_.spin_up_years > 0) {
        static_cast<void>(spin_up_surface_energy(state_, parameters_, surface_, fractions_,
                                                 scenario_.spin_up_years, worker_count_));
    }

    scheduler_ = std::make_unique<Scheduler>(clock_, make_orbital_calendar(parameters_),
                                             scenario_.initial_mode);
    // With an atmosphere, reference mode resolves the winds, which replace
    // ADR-0009's diffusion there (ADR-0011 §4.3); climate mode keeps it.
    const bool winds = surface_.atmosphere.layer_count > 0U;
    register_surface_energy(*scheduler_, state_, parameters_, surface_, fractions_, worker_count_,
                            &last_, !winds);
    if (winds) {
        dynamics_ = std::make_unique<AtmosphereDynamics>(*mesh_, state_.slow(), parameters_,
                                                         surface_.atmosphere, fractions_,
                                                         AtmosphereDynamicsParameters{},
                                                         worker_count_);
        register_atmosphere_dynamics(*scheduler_, state_, *dynamics_);
    }

    manifest_.engine_version = std::string(snapshot_engine_version());
    manifest_.mesh_generator_version = mesh_generator_version;
    manifest_.scenario = scenario_.entries();
    checkpoint_if_due();
}

PlanetRun::~PlanetRun() = default;

void PlanetRun::submit(RunCommand command) {
    if (command.type == run_command_set_mode) {
        if (!parse_run_mode(command.payload)) {
            throw std::invalid_argument("set_mode payload must be climate or reference: '" +
                                        command.payload + "'");
        }
    } else if (command.type == run_command_set_solar_luminosity_factor) {
        static_cast<void>(parse_luminosity_factor(command.payload));
    } else {
        throw std::invalid_argument("unknown run command type: " + command.type);
    }
    // Validate the text fields now, not when the manifest is written.
    RunManifest probe;
    probe.engine_version = "probe";
    probe.commands.push_back(command);
    static_cast<void>(format_run_manifest(probe));

    const auto position = std::upper_bound(
        pending_.begin(), pending_.end(), command.tick,
        [](SimulationTick tick, const RunCommand& queued) { return tick < queued.tick; });
    pending_.insert(position, std::move(command));
}

void PlanetRun::apply(const RunCommand& command) {
    if (command.type == run_command_set_mode) {
        scheduler_->request_mode(*parse_run_mode(command.payload));
    } else {
        parameters_.star_luminosity_W =
            base_parameters_.star_luminosity_W * parse_luminosity_factor(command.payload);
    }
    manifest_.commands.push_back(command);
}

void PlanetRun::run_until(SimulationTick target_tick) {
    while (scheduler_->next_step().end_tick <= target_tick) {
        const SimulationTick boundary = clock_.tick();
        while (!pending_.empty() && pending_.front().tick <= boundary) {
            RunCommand command = std::move(pending_.front());
            pending_.pop_front();
            command.tick = boundary;
            apply(command);
        }
        // A mode change may have lengthened the step past the target.
        if (scheduler_->next_step().end_tick > target_tick) {
            break;
        }
        static_cast<void>(scheduler_->step());
        checkpoint_if_due();
    }
}

void PlanetRun::checkpoint_if_due() {
    const SimulationTick tick = clock_.tick();
    if (tick < orbital_year_begin_tick(next_checkpoint_year_, parameters_)) {
        return;
    }
    while (tick >= orbital_year_begin_tick(next_checkpoint_year_, parameters_)) {
        ++next_checkpoint_year_;
    }
    manifest_.checkpoints.push_back({tick, state_hash()});
}

std::uint64_t PlanetRun::state_hash() const { return slow_state_hash(state_); }

RunManifest PlanetRun::manifest() const {
    RunManifest manifest = manifest_;
    manifest.end_tick = clock_.tick();
    return manifest;
}

ReplayResult replay_run(const RunManifest& manifest, std::size_t worker_count) {
    if (manifest.mesh_generator_version != mesh_generator_version) {
        throw std::runtime_error("run manifest mesh_generator_version " +
                                 std::to_string(manifest.mesh_generator_version) +
                                 " does not match this build's " +
                                 std::to_string(mesh_generator_version));
    }
    if (!manifest.end_tick) {
        throw std::runtime_error("run manifest has no end_tick; the run was not closed");
    }
    PlanetRun run(Scenario::from_entries(manifest.scenario), worker_count);
    for (const auto& command : manifest.commands) {
        run.submit(command);
    }
    run.run_until(*manifest.end_tick);
    const RunManifest replayed = run.manifest();

    ReplayResult result;
    result.end_tick = run.tick();
    result.final_state_hash = run.state_hash();
    const auto diverged_at = [&result](SimulationTick tick) {
        if (!result.first_divergence_tick || tick < *result.first_divergence_tick) {
            result.first_divergence_tick = tick;
        }
    };
    for (std::size_t index = 0; index < manifest.checkpoints.size(); ++index) {
        const auto& recorded = manifest.checkpoints[index];
        ++result.checkpoints_compared;
        if (index >= replayed.checkpoints.size() ||
            replayed.checkpoints[index].tick != recorded.tick ||
            replayed.checkpoints[index].state_hash != recorded.state_hash) {
            diverged_at(recorded.tick);
            break;
        }
    }
    if (replayed.checkpoints.size() > manifest.checkpoints.size()) {
        diverged_at(replayed.checkpoints[manifest.checkpoints.size()].tick);
    }
    for (std::size_t index = 0; index < manifest.commands.size(); ++index) {
        if (index >= replayed.commands.size() ||
            replayed.commands[index].tick != manifest.commands[index].tick) {
            diverged_at(manifest.commands[index].tick);
            break;
        }
    }
    if (result.end_tick != *manifest.end_tick) {
        diverged_at(std::min(result.end_tick, *manifest.end_tick));
    }
    result.matched = !result.first_divergence_tick;
    return result;
}

}  // namespace planetsim
