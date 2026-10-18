#include "sim/presentation/visual_frame.hpp"
#include "sim/presentation/presentation_record.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
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
    std::array frames{snapshot(100.0F, 266.0F, 20.0F, 600.0F),
                      snapshot(20.0F, 274.0F, 0.0F, 0.0F)};
    frames[0].sea_level_pressure_Pa = {101'325.0F, 50'662.5F, 0.0F};
    frames[0].surface_eastward_wind_m_s = {12.0F, -3.0F, 0.0F};
    frames[0].surface_northward_wind_m_s = {-5.0F, 4.0F, 0.0F};
    frames[0].precipitation_kg_m2_s = {
        0.0F, 0.5F * planetsim::presentation::precipitation_display_maximum_kg_m2_s,
        2.0F * planetsim::presentation::precipitation_display_maximum_kg_m2_s};
    frames[0].sun_direction_body_unit = {0.0, 0.6, 0.8};
    // ADR-0023 V12: clear, half-covered and overcast (beyond 1: clamped).
    frames[0].cloud_cover = {0.0F, 0.5F, 1.2F};
    frames[0].cloud_optical_depth = {0.0F, 10.0F, 60.0F};
    const auto reference = planetsim::presentation::make_presentation_reference(frames);
    const auto cold = planetsim::presentation::make_visual_frame(frames[0], terrain, reference);
    const auto warm = planetsim::presentation::make_visual_frame(frames[1], terrain, reference);
    // The sun's direction reaches the frame for lighting, unchanged.
    PLANETSIM_EXPECT(test, cold.sun_direction_body_unit.y == 0.6 &&
                               cold.sun_direction_body_unit.z == 0.8);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::daylight)].values[0] == 1.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::daylight)].values[1] == 0.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::relief)].values[0] == 0.0F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::snow_cover)].values[0] >
                               warm.channels[channel(ChannelId::snow_cover)].values[0] + 0.1F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::sea_ice)].values[1] >
                               warm.channels[channel(ChannelId::sea_ice)].values[1] + 0.1F);
    PLANETSIM_EXPECT(test, cold.channels[channel(ChannelId::temperature_anomaly)].values[0] < 0.0F);
    PLANETSIM_EXPECT(test, warm.channels[channel(ChannelId::temperature_anomaly)].values[0] > 0.0F);
    const auto& density = cold.channels[channel(ChannelId::atmosphere_density)].values;
    PLANETSIM_EXPECT(test, density.size() == 3U && density[0] == 1.0F && density[1] == 0.5F &&
                               density[2] == 0.0F);
    const auto& wind = cold.channels[channel(ChannelId::wind)].values;
    PLANETSIM_EXPECT(test, wind.size() == 3U * planetsim::presentation::wind_vector_component_count);
    PLANETSIM_EXPECT(test, wind[0] == 12.0F && wind[1] == -5.0F && wind[2] == -3.0F &&
                               wind[3] == 4.0F);
    const auto& precipitation = cold.channels[channel(ChannelId::precipitation)].values;
    PLANETSIM_EXPECT(test, precipitation.size() == 3U && precipitation[0] == 0.0F &&
                               precipitation[1] == 0.5F && precipitation[2] == 1.0F);
    // The cloud channels rise with the cover and the optical depth.
    const auto& cover = cold.channels[channel(ChannelId::cloud_cover)].values;
    const auto& thickness = cold.channels[channel(ChannelId::cloud_thickness)].values;
    PLANETSIM_EXPECT(test, cover.size() == 3U && cover[0] == 0.0F && cover[1] == 0.5F &&
                               cover[2] == 1.0F);
    PLANETSIM_EXPECT(test, thickness.size() == 3U && thickness[0] == 0.0F);
    PLANETSIM_EXPECT_NEAR(test, thickness[1], 1.0F - std::exp(-1.0F), 1e-6F);
    PLANETSIM_EXPECT(test, thickness[2] > thickness[1] && thickness[2] <= 1.0F);
    PLANETSIM_EXPECT(test,
                     warm.channels[channel(ChannelId::atmosphere_density)].values.empty() &&
                         warm.channels[channel(ChannelId::wind)].values.empty() &&
                         warm.channels[channel(ChannelId::precipitation)].values.empty() &&
                         warm.channels[channel(ChannelId::cloud_cover)].values.empty() &&
                         warm.channels[channel(ChannelId::cloud_thickness)].values.empty());
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
    // Schema 3 terrain carries the hypsometry (ADR-0022 §4.6): nine
    // quantiles per cell, rising, whose mean is the cell's mean elevation.
    const auto& hypsometry = recorded.terrain_snapshot().hypsometry_m;
    const std::size_t terrain_cells = recorded.terrain_snapshot().mean_elevation_m.size();
    bool rising = hypsometry.size() == terrain_cells * planetsim::hypsometry_quantile_count;
    for (std::size_t index = 0; rising && index + 1U < hypsometry.size(); ++index) {
        if ((index + 1U) % planetsim::hypsometry_quantile_count != 0U)
            rising = hypsometry[index] <= hypsometry[index + 1U];
    }
    PLANETSIM_EXPECT(test, rising && loaded.terrain->hypsometry_m == hypsometry);
    {
        // An older record's schema-2 terrain still reads, without hypsometry.
        planetsim::presentation::PresentationRecord old_record = record;
        old_record.terrain->schema_version = 2U;
        planetsim::presentation::write_presentation_record(path, old_record);
        const auto loaded_old = planetsim::presentation::read_presentation_record(path);
        std::filesystem::remove(path);
        PLANETSIM_EXPECT(test, loaded_old.terrain.has_value() &&
                                   loaded_old.terrain->schema_version == 2U &&
                                   loaded_old.terrain->hypsometry_m.empty() &&
                                   loaded_old.frames.size() == frames.size());
    }
    PLANETSIM_EXPECT(test, !loaded.frames.empty() &&
                               loaded.frames.back().top_of_atmosphere_insolation_W_m2 ==
                                   frames.back().top_of_atmosphere_insolation_W_m2);
    // Schema 6 carries the clouds, schema 5 precipitation, schema 4 the
    // circulation (ADR-0011 §8), and an older schema 3 frame still
    // round-trips. (L1 resolves no circulation, so those fields are set by
    // hand.)
    PLANETSIM_EXPECT(test, !loaded.frames.empty() && loaded.frames.back().schema_version == 6U &&
                               loaded.frames.back().sea_level_pressure_Pa.empty() &&
                               !loaded.frames.back().precipitation_kg_m2_s.empty() &&
                               loaded.frames.back().cloud_cover == frames.back().cloud_cover &&
                               loaded.frames.back().cloud_optical_depth ==
                                   frames.back().cloud_optical_depth);
    if (!frames.empty()) {
        StateSnapshot current = frames.back();
        const std::size_t cells = current.surface_temperature_K.size();
        current.sea_level_pressure_Pa.assign(cells, 101'000.0F);
        current.surface_eastward_wind_m_s.assign(cells, 5.0F);
        current.surface_northward_wind_m_s.assign(cells, -1.0F);
        current.precipitation_kg_m2_s.assign(cells, 0.0001F);
        current.cloud_cover.assign(cells, 0.6F);
        current.cloud_optical_depth.assign(cells, 4.0F);
        planetsim::presentation::write_presentation_record(path, {current});
        const auto loaded_current = planetsim::presentation::read_presentation_record(path);
        std::filesystem::remove(path);
        PLANETSIM_EXPECT(test, loaded_current.frames.size() == 1U &&
                                   loaded_current.frames[0].sea_level_pressure_Pa ==
                                       current.sea_level_pressure_Pa &&
                                   loaded_current.frames[0].surface_eastward_wind_m_s ==
                                       current.surface_eastward_wind_m_s &&
                                   loaded_current.frames[0].surface_northward_wind_m_s ==
                                       current.surface_northward_wind_m_s &&
                                   loaded_current.frames[0].precipitation_kg_m2_s ==
                                       current.precipitation_kg_m2_s &&
                                   loaded_current.frames[0].cloud_cover == current.cloud_cover &&
                                   loaded_current.frames[0].cloud_optical_depth ==
                                       current.cloud_optical_depth);

        StateSnapshot without_clouds = current;
        without_clouds.schema_version = 5U;
        without_clouds.cloud_cover.clear();
        without_clouds.cloud_optical_depth.clear();
        planetsim::presentation::write_presentation_record(path, {without_clouds});
        const auto loaded_without_clouds = planetsim::presentation::read_presentation_record(path);
        std::filesystem::remove(path);
        PLANETSIM_EXPECT(test, loaded_without_clouds.frames.size() == 1U &&
                                   loaded_without_clouds.frames[0].schema_version == 5U &&
                                   loaded_without_clouds.frames[0].precipitation_kg_m2_s ==
                                       current.precipitation_kg_m2_s &&
                                   loaded_without_clouds.frames[0].cloud_cover.empty());

        StateSnapshot circulation_only = current;
        circulation_only.schema_version = 4U;
        circulation_only.precipitation_kg_m2_s.clear();
        circulation_only.cloud_cover.clear();
        circulation_only.cloud_optical_depth.clear();
        planetsim::presentation::write_presentation_record(path, {circulation_only});
        const auto loaded_circulation_only =
            planetsim::presentation::read_presentation_record(path);
        std::filesystem::remove(path);
        PLANETSIM_EXPECT(test, loaded_circulation_only.frames.size() == 1U &&
                                   loaded_circulation_only.frames[0].schema_version == 4U &&
                                   loaded_circulation_only.frames[0].surface_eastward_wind_m_s ==
                                       circulation_only.surface_eastward_wind_m_s &&
                                   loaded_circulation_only.frames[0].precipitation_kg_m2_s.empty());

        StateSnapshot old = frames.back();
        old.schema_version = 3U;
        old.sea_level_pressure_Pa.clear();
        old.surface_eastward_wind_m_s.clear();
        old.surface_northward_wind_m_s.clear();
        planetsim::presentation::write_presentation_record(path, {old});
        const auto loaded_old = planetsim::presentation::read_presentation_record(path);
        std::filesystem::remove(path);
        PLANETSIM_EXPECT(test, loaded_old.frames.size() == 1U && loaded_old.frames[0].schema_version == 3U &&
                                   loaded_old.frames[0].sea_level_pressure_Pa.empty() &&
                                   loaded_old.frames[0].surface_temperature_K == old.surface_temperature_K);
    }
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
