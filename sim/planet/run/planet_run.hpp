#pragma once

#include "sim/core/scheduler/scheduler.hpp"
#include "sim/core/serialization/run_manifest.hpp"
#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/dynamics/atmosphere_dynamics.hpp"
#include "sim/planet/dynamics/climate_circulation.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace planetsim {

// Everything that determines a run's initial state (ADR-0003 §3.3). The
// same scenario on the same build gives the same initial slow state.
struct Scenario {
    PlanetPreset preset = PlanetPreset::earth_like;
    std::uint64_t seed = 1;
    std::uint32_t subdivision = 5;
    int spin_up_years = 0;   // climate years before tick 0 (ADR-0006 §4.2)
    SimulationMode initial_mode = SimulationMode::climate;
    // ADR-0010 §4.1: the atmosphere's layer count; empty means the preset's.
    // The manifest records the resolved count. A preset without an
    // atmosphere cannot be given one.
    std::optional<std::uint32_t> atmosphere_layers;

    // The layer count the run uses.
    [[nodiscard]] std::uint32_t resolved_atmosphere_layers() const noexcept;
    [[nodiscard]] ScenarioEntries entries() const;
    // Throws std::runtime_error on a missing, unknown or invalid entry. A
    // manifest written before M5 has no atmosphere_layers entry and takes
    // the preset's.
    [[nodiscard]] static Scenario from_entries(const ScenarioEntries& entries);
};

// Command types (specification §7, P0 climate-lab controls):
//   set_mode                     payload "climate" or "reference"
//   set_solar_luminosity_factor  payload a decimal in (0, 10]: the star's
//                                luminosity as a multiple of the scenario's
inline constexpr std::string_view run_command_set_mode = "set_mode";
inline constexpr std::string_view run_command_set_solar_luminosity_factor =
    "set_solar_luminosity_factor";

// The first tick of orbital year `year` (sub-step 12·year, ADR-0006 §4.1).
[[nodiscard]] SimulationTick orbital_year_begin_tick(std::int64_t year,
                                                     const PlanetParameters& parameters);

// How a run uses the climate mode's circulation (ADR-0011 §4.4, §17).
//   none:       left out;
//   diagnostic: solved after each climate step's surface, writing derived
//               fields only (task M6-04): the slow state is as without it;
//   coupled:    solved first in each climate step, carrying the heat
//               (ADR-0011 §17.1; task M6-05).
enum class ClimateCirculationUse { none, diagnostic, coupled };

// A scenario run on the scheduler (ADR-0003 §3.3). Commands take effect at
// the start of the first step that begins at or after their tick; the state
// is hashed at tick 0 and at the first step boundary at or after the start
// of every orbital year. The step sequence, the commands' effective ticks and
// the hashes depend only on the scenario and the commands: not on the worker
// count or on how run_until calls are chunked.
class PlanetRun {
  public:
    explicit PlanetRun(const Scenario& scenario, std::size_t worker_count = 1U,
                       ClimateCirculationUse circulation = ClimateCirculationUse::coupled);
    PlanetRun(const PlanetRun&) = delete;
    PlanetRun& operator=(const PlanetRun&) = delete;
    PlanetRun(PlanetRun&&) = delete;
    PlanetRun& operator=(PlanetRun&&) = delete;
    ~PlanetRun();

    // Validates the type and payload (std::invalid_argument) and queues the
    // command. Commands are applied in (tick, submission) order.
    void submit(RunCommand command);

    // Runs whole steps while the next step ends at or before target_tick.
    void run_until(SimulationTick target_tick,
                   const std::function<void(const StateSnapshot&)>& snapshot_observer = {});

    [[nodiscard]] SimulationTick tick() const noexcept { return clock_.tick(); }
    [[nodiscard]] const Scenario& scenario() const noexcept { return scenario_; }
    [[nodiscard]] const PlanetParameters& parameters() const noexcept { return parameters_; }
    [[nodiscard]] const PlanetState& state() const noexcept { return state_; }
    [[nodiscard]] const SurfaceFractions& fractions() const noexcept { return fractions_; }
    [[nodiscard]] const SurfaceEnergyParameters& surface_parameters() const noexcept {
        return surface_;
    }
    [[nodiscard]] const Scheduler& scheduler() const noexcept { return *scheduler_; }
    [[nodiscard]] const SurfaceEnergyDiagnostics& last_step() const noexcept { return last_; }
    // Wall-clock seconds in the steps' transport solves (performance records).
    [[nodiscard]] double transport_solve_s() const noexcept { return transport_solve_s_; }
    // The winds of reference mode (ADR-0011 §4.3); null without an atmosphere.
    [[nodiscard]] const AtmosphereDynamics* dynamics() const noexcept { return dynamics_.get(); }
    // The climate mode's circulation (ADR-0011 §4.4); null without an
    // atmosphere, when left out, or below the mesh level that resolves its
    // bands (climate_circulation_resolves).
    [[nodiscard]] const ClimateCirculation* circulation() const noexcept {
        return circulation_.get();
    }
    [[nodiscard]] std::uint64_t state_hash() const;

    // The inputs so far and the checkpoints, with end_tick set to the
    // current tick.
    [[nodiscard]] RunManifest manifest() const;

  private:
    void apply(const RunCommand& command);
    void checkpoint_if_due();

    Scenario scenario_;
    std::size_t worker_count_;
    PlanetParameters base_parameters_;
    PlanetParameters parameters_;
    SurfaceEnergyParameters surface_;
    std::shared_ptr<const PlanetMesh> mesh_;
    PlanetState state_;
    SurfaceFractions fractions_;
    SimulationClock clock_;
    std::unique_ptr<AtmosphereDynamics> dynamics_;
    std::unique_ptr<ClimateCirculation> circulation_;
    std::unique_ptr<Scheduler> scheduler_;
    SurfaceEnergyDiagnostics last_;
    double transport_solve_s_ = 0.0;
    std::deque<RunCommand> pending_;
    RunManifest manifest_;
    std::int64_t next_checkpoint_year_ = 0;
};

struct ReplayResult {
    bool matched = false;
    std::size_t checkpoints_compared = 0;
    // The first recorded checkpoint whose hash differs or whose tick the
    // replay did not land on; absent when every checkpoint matched.
    std::optional<SimulationTick> first_divergence_tick;
    SimulationTick end_tick = 0;
    std::uint64_t final_state_hash = 0;
};

// ADR-0003 V2: re-simulates a run from its manifest alone. Throws
// std::runtime_error if the manifest's mesh generator differs from this
// build's or its scenario is invalid; an engine_version difference is the
// caller's to report (only L1 equivalence holds across builds).
[[nodiscard]] ReplayResult replay_run(const RunManifest& manifest, std::size_t worker_count = 1U);

}  // namespace planetsim
