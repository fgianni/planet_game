#include "sim/presentation/visual_frame.hpp"
#include "sim/presentation/presentation_record.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

using planetsim::StateSnapshot;
using planetsim::TerrainSnapshot;
using planetsim::presentation::ChannelId;

[[nodiscard]] std::size_t channel(ChannelId id) {
    return static_cast<std::size_t>(id) - 1U;
}

[[nodiscard]] StateSnapshot snapshot(float insolation, float temperature, float snow, float ice) {
    StateSnapshot result;
    result.simulation_tick = 42;
    result.top_of_atmosphere_insolation_W_m2 = {insolation, 0.0F, 50.0F};
    result.surface_temperature_K = {temperature, temperature, temperature};
    result.land_snow_water_equivalent_kg_m2 = {snow, 0.0F, snow};
    result.sea_ice_mass_kg_m2 = {0.0F, ice, ice};
    result.climatology_surface_temperature_mean_K = {270.0F, 270.0F, 270.0F};
    result.climatology_surface_temperature_variance_K2 = {4.0F, 4.0F, 4.0F};
    return result;
}

void check_channels(planetsim::test::Context& test) {
    TerrainSnapshot terrain;
    terrain.sea_level_m = 0.0;
    terrain.mean_elevation_m = {0.0F, 1'600.0F, 500.0F};
    terrain.land_fraction = {1.0F, 0.0F, 0.5F};
    const std::array frames{snapshot(100.0F, 266.0F, 20.0F, 600.0F),
                            snapshot(20.0F, 274.0F, 0.0F, 0.0F)};
    const auto reference = planetsim::presentation::make_presentation_reference(frames);
    const auto cold = planetsim::presentation::make_visual_frame(frames[0], terrain, reference);
    const auto warm = planetsim::presentation::make_visual_frame(frames[1], terrain, reference);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::daylight)].values[0] == 1.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::daylight)].values[1] == 0.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::relief)].values[0] == 0.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::snow_cover)].values[0] >
                               warm.channels[channel(ChannelId::snow_cover)].values[0] + 0.1F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::sea_ice)].values[1] >
                               warm.channels[channel(ChannelId::sea_ice)].values[1] + 0.1F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::temperature_anomaly)].values[0] < 0.0F);
    PLANETSIM_EXPECT(test, warm.channels[channel(ChannelId::temperature_anomaly)].values[0] > 0.0F);
    for (std::size_t cell = 0; cell < terrain.mean_elevation_m.size(); ++cell) {
        float sum = 0.0F;
        for (std::size_t weight = 0; weight < planetsim::presentation::surface_class_weight_count;
             ++weight) {
            sum += cold.channels[channel(ChannelId::surface_class)].values[
                cell * planetsim::presentation::surface_class_weight_count + weight];
        }
        PLANETSIM_EXPECT_NEAR(test, sum, 1.0F, 1e-6F);
    }
}

void check_recording_is_non_authoritative(planetsim::test::Context& test) {
    planetsim::Scenario scenario;
    scenario.subdivision = 1U;
    planetsim::PlanetRun baseline(scenario);
    const auto end = planetsim::orbital_year_begin_tick(1, baseline.parameters());
    std::vector<StateSnapshot> baseline_frames;
    baseline.run_until(end, [&baseline_frames](const StateSnapshot& frame) {
        baseline_frames.push_back(frame);
    });

    TerrainSnapshot terrain;
    terrain.mean_elevation_m.assign(baseline.state().mesh().cell_count(), 0.0F);
    terrain.land_fraction.assign(baseline.state().mesh().cell_count(), 0.5F);
    const auto reference = planetsim::presentation::make_presentation_reference(baseline_frames);

    planetsim::PlanetRun recorded(scenario);
    std::vector<StateSnapshot> frames;
    recorded.run_until(end, [&frames, &terrain, &reference](const StateSnapshot& frame) {
        frames.push_back(frame);
        static_cast<void>(planetsim::presentation::make_visual_frame(frame, terrain, reference));
    });
    const auto path = std::filesystem::temp_directory_path() / "planetsim_r1_test.pframe";
    planetsim::presentation::PresentationRecord record;
    record.mesh = planetsim::presentation::PresentationMeshDescriptor{
        scenario.subdivision, recorded.parameters().radius_m};
    record.terrain = recorded.terrain_snapshot();
    record.frames = frames;
    planetsim::presentation::write_presentation_record(path, record);
    const auto loaded = planetsim::presentation::read_presentation_record(path);
    std::filesystem::remove(path);
    PLANETSIM_EXPECT(test, loaded.is_playback_ready());
    PLANETSIM_EXPECT(test, loaded.frames.size() == frames.size());
    PLANETSIM_EXPECT(test, loaded.terrain.has_value() &&
                               loaded.terrain->mean_elevation_m ==
                                   recorded.terrain_snapshot().mean_elevation_m);
    PLANETSIM_EXPECT(test, !loaded.frames.empty() &&
                               loaded.frames.back().top_of_atmosphere_insolation_W_m2 ==
                                   frames.back().top_of_atmosphere_insolation_W_m2);
    PLANETSIM_EXPECT(test, baseline.state_hash() == recorded.state_hash());
    const auto baseline_manifest = baseline.manifest();
    const auto recorded_manifest = recorded.manifest();
    bool same_checkpoints = baseline_manifest.checkpoints.size() ==
                            recorded_manifest.checkpoints.size();
    for (std::size_t index = 0; index < baseline_manifest.checkpoints.size() && same_checkpoints;
         ++index) {
        same_checkpoints = baseline_manifest.checkpoints[index].tick ==
                               recorded_manifest.checkpoints[index].tick &&
                           baseline_manifest.checkpoints[index].state_hash ==
                               recorded_manifest.checkpoints[index].state_hash;
    }
    PLANETSIM_EXPECT(test, same_checkpoints);
}

void check_performance(planetsim::test::Context& test) {
    for (const auto level : {5U, 6U}) {
        const std::size_t cells = 10U * (1U << (2U * level)) + 2U;
        TerrainSnapshot terrain;
        terrain.mean_elevation_m.assign(cells, 300.0F);
        terrain.land_fraction.assign(cells, 0.6F);
        const auto state = snapshot(200.0F, 271.0F, 4.0F, 100.0F);
        StateSnapshot large = state;
        for (auto* values : {&large.top_of_atmosphere_insolation_W_m2, &large.surface_temperature_K,
                 &large.land_snow_water_equivalent_kg_m2, &large.sea_ice_mass_kg_m2,
                 &large.climatology_surface_temperature_mean_K,
                 &large.climatology_surface_temperature_variance_K2}) {
            values->resize(cells, values->front());
        }
        const std::array frames{large};
        const auto reference = planetsim::presentation::make_presentation_reference(frames);
        constexpr int repetitions = 8;
        const auto begin = std::chrono::steady_clock::now();
        std::size_t values = 0U;
        for (int iteration = 0; iteration < repetitions; ++iteration) {
            values += planetsim::presentation::make_visual_frame(large, terrain, reference)
                          .channels[channel(ChannelId::surface_class)].values.size();
        }
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count() / repetitions;
        std::cout << "presentation L" << level << " make_visual_frame_ms=" << milliseconds
                  << " values=" << values << '\n';
        PLANETSIM_EXPECT(test, values == repetitions * cells *
                               planetsim::presentation::surface_class_weight_count);
        if (level == 6U) PLANETSIM_EXPECT(test, milliseconds < 10.0);
    }
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_channels(test);
    check_recording_is_non_authoritative(test);
    check_performance(test);
    return test.result();
}
