#include "sim/core/serialization/run_manifest.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

// ADR-0003 V1 (run determinism) and V2 (replay) on the M3 surface-energy run.
namespace {

using planetsim::RunCommand;
using planetsim::SimulationTick;

[[nodiscard]] planetsim::Scenario test_scenario() {
    planetsim::Scenario scenario;
    scenario.preset = planetsim::PlanetPreset::earth_like;
    scenario.seed = 20'260'929U;
    scenario.subdivision = 2U;
    scenario.spin_up_years = 1;
    return scenario;
}

struct Timeline {
    SimulationTick reference_boundary = 0;   // sub-step 13 begins
    SimulationTick reference_on = 0;    // switch to ten-minute steps
    SimulationTick reference_off = 0;   // two days later, back to climate
    SimulationTick brighter = 0;        // luminosity +5 %
    SimulationTick end = 0;
};

[[nodiscard]] Timeline timeline() {
    const auto parameters = planetsim::PlanetParameters::earth_development();
    Timeline times;
    // Requested mid-month, so it waits for the next sub-step boundary; the
    // return is requested between ten-minute steps.
    times.reference_boundary = planetsim::climate_substep(13, parameters).begin_tick;
    times.reference_on = times.reference_boundary - 20'000;
    times.reference_off = times.reference_boundary + 2 * 1'440 + 3;
    times.brighter = planetsim::orbital_year_begin_tick(2, parameters);
    times.end = planetsim::orbital_year_begin_tick(4, parameters);
    return times;
}

// One run: the commands submitted at the start, or the luminosity command
// only once the run has passed year 1 (as a live player would), advanced in
// the given run_until chunks.
[[nodiscard]] planetsim::RunManifest run(std::size_t workers,
                                         const std::vector<SimulationTick>& chunks,
                                         bool late_submission) {
    const auto times = timeline();
    planetsim::PlanetRun planet(test_scenario(), workers);
    planet.submit({times.reference_on, "test", "set_mode", "reference"});
    planet.submit({times.reference_off, "test", "set_mode", "climate"});
    if (!late_submission) {
        planet.submit({times.brighter, "test", "set_solar_luminosity_factor", "1.05"});
    }
    for (const auto target : chunks) {
        planet.run_until(target);
        if (late_submission && planet.tick() > times.reference_off) {
            planet.submit({times.brighter, "test", "set_solar_luminosity_factor", "1.05"});
            late_submission = false;
        }
    }
    planet.run_until(times.end);
    return planet.manifest();
}

[[nodiscard]] bool same_record(const planetsim::RunManifest& a, const planetsim::RunManifest& b) {
    return planetsim::format_run_manifest(a) == planetsim::format_run_manifest(b);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto times = timeline();

    // V1: worker counts and pacing (one call, uneven chunks, chunks ending
    // inside the reference window and inside climate sub-steps).
    const auto reference = run(1U, {}, false);
    const std::vector<SimulationTick> chunks{17'000,
                                             times.reference_on - 3,
                                             times.reference_on + 7,
                                             times.reference_on + 1'000,
                                             times.reference_off + 11,
                                             times.brighter - 1,
                                             times.brighter + 40'000};
    bool identical = true;
    for (const std::size_t workers : {2U, 8U, 16U}) {
        identical = identical && same_record(reference, run(workers, {}, false));
    }
    identical = identical && same_record(reference, run(8U, chunks, false));
    identical = identical && same_record(reference, run(2U, chunks, true));
    PLANETSIM_EXPECT(test, identical);

    // The record: checkpoints at tick 0 and each orbital year, the mode
    // commands at step boundaries, the luminosity command at year 2.
    PLANETSIM_EXPECT(test, reference.checkpoints.size() == 5U);
    PLANETSIM_EXPECT(test, reference.commands.size() == 3U);
    PLANETSIM_EXPECT(test, reference.end_tick == times.end);
    if (reference.commands.size() == 3U && reference.checkpoints.size() == 5U) {
        const auto& on = reference.commands[0];
        const auto& off = reference.commands[1];
        PLANETSIM_EXPECT(test, on.tick == times.reference_boundary);
        PLANETSIM_EXPECT(test, off.tick >= times.reference_off && off.tick % 10 == 0 &&
                                   off.tick < times.reference_off + 10);
        PLANETSIM_EXPECT(test, reference.commands[2].tick == times.brighter);
        PLANETSIM_EXPECT(test, reference.checkpoints[2].tick == times.brighter);
        bool distinct = true;
        for (std::size_t index = 1; index < reference.checkpoints.size(); ++index) {
            distinct = distinct && reference.checkpoints[index].state_hash !=
                                       reference.checkpoints[index - 1U].state_hash;
        }
        PLANETSIM_EXPECT(test, distinct);
        std::cout << "checkpoints";
        for (const auto& checkpoint : reference.checkpoints) {
            std::cout << ' ' << checkpoint.tick;
        }
        std::cout << "\nmode commands at " << on.tick << " and " << off.tick << '\n';
    }

    // V2: replay from the manifest alone, through a file, on other workers.
    const auto path = std::filesystem::temp_directory_path() / "planetsim_test_replay.prun";
    planetsim::write_run_manifest(path, reference);
    const auto loaded = planetsim::read_run_manifest(path);
    std::filesystem::remove(path);
    const auto replayed = planetsim::replay_run(loaded, 4U);
    PLANETSIM_EXPECT(test, replayed.matched);
    PLANETSIM_EXPECT(test, replayed.checkpoints_compared == reference.checkpoints.size());
    PLANETSIM_EXPECT(test, !reference.checkpoints.empty() &&
                               replayed.final_state_hash == reference.checkpoints.back().state_hash);

    if (reference.checkpoints.size() == 5U && reference.commands.size() == 3U) {
        // A different input diverges from the first checkpoint after it.
        auto altered_input = loaded;
        altered_input.commands[2].payload = "1.04";
        const auto from_input = planetsim::replay_run(altered_input, 2U);
        PLANETSIM_EXPECT(test, !from_input.matched);
        PLANETSIM_EXPECT(test, from_input.first_divergence_tick == reference.checkpoints[3].tick);

        // A corrupted record is located at its own tick.
        auto altered_hash = loaded;
        altered_hash.checkpoints[1].state_hash ^= 1U;
        const auto from_hash = planetsim::replay_run(altered_hash, 2U);
        PLANETSIM_EXPECT(test, !from_hash.matched);
        PLANETSIM_EXPECT(test, from_hash.first_divergence_tick == reference.checkpoints[1].tick);

        // A command that never took effect in the original cannot replay
        // at its recorded tick.
        auto shifted = loaded;
        shifted.commands[0].tick += 3;
        const auto from_shift = planetsim::replay_run(shifted, 2U);
        PLANETSIM_EXPECT(test, !from_shift.matched);
        PLANETSIM_EXPECT(test, from_shift.first_divergence_tick <= reference.checkpoints[2].tick);
    }

    // Invalid commands are refused before they reach the record.
    planetsim::PlanetRun planet(test_scenario(), 2U);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planet.submit({0, "test", "set_mode", "weather_window"}));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planet.submit({0, "test", "set_solar_luminosity_factor", "0"}));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planet.submit({0, "test", "set_co2", "560"}));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planet.submit({0, "", "set_mode", "climate"}));
    return test.result();
}
