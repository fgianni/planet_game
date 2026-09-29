#include "sim/core/random/counter_rng.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::uint64_t synthetic_seed = 0x9B97'F4A7'C150'0011ULL;
constexpr planetsim::SimulationTick synthetic_tick = 123'456;

[[nodiscard]] std::array<float, planetsim::hypsometry_layer_count>
expected_quantiles(std::uint32_t cell) {
    std::array<float, planetsim::hypsometry_layer_count> quantiles{};
    for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
        const double unit = planetsim::keyed_random_unit_double(
            synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick, cell,
            static_cast<std::uint32_t>(layer));
        quantiles[layer] = static_cast<float>(-8'000.0 + unit * 16'000.0);
    }
    std::sort(quantiles.begin(), quantiles.end());
    return quantiles;
}

// Hypsometry and sea level shared by every golden file.
void check_schema_1_fields(planetsim::test::Context& test, const planetsim::PlanetState& state) {
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        const auto quantiles = expected_quantiles(static_cast<std::uint32_t>(cell));
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            const float actual = state.slow().hypsometry_m.at(
                layer, planetsim::CellId{static_cast<std::uint32_t>(cell)});
            PLANETSIM_EXPECT(test, std::bit_cast<std::uint32_t>(actual) ==
                                       std::bit_cast<std::uint32_t>(quantiles[layer]));
        }
    }
    const double sea_unit = planetsim::keyed_random_unit_double(
        synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick, 0U, 99U);
    const double expected_sea_level_m = -200.0 + sea_unit * 400.0;
    PLANETSIM_EXPECT(test,
                     std::bit_cast<std::uint64_t>(state.slow().sea_level_m) ==
                         std::bit_cast<std::uint64_t>(expected_sea_level_m));
}

[[nodiscard]] double expected_temperature(std::size_t cell, std::uint32_t sample) {
    return 220.0 + 100.0 * planetsim::keyed_random_unit_double(
                               synthetic_seed, planetsim::RandomStreamId::validation,
                               synthetic_tick, static_cast<std::uint32_t>(cell), sample);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(0U, 6'371'000.0));
    const auto parameters = planetsim::PlanetParameters::earth_development();
    const auto surface =
        planetsim::surface_energy_parameters_for(planetsim::PlanetPreset::earth_like);

    // Schema 1 (M2): loads only through the schema 1 -> 2 migration, which
    // initialises the surface temperatures at their closed-form equilibrium.
    {
        const auto golden_path = std::filesystem::path("tests/data/golden/psnap-v1-l0.psnap");
        planetsim::PlanetState unmigrated(mesh);
        bool refused = false;
        try {
            static_cast<void>(planetsim::read_snapshot(golden_path, unmigrated));
        } catch (const std::runtime_error& error) {
            refused = std::string_view{error.what()}.find("migration") != std::string_view::npos;
        }
        PLANETSIM_EXPECT(test, refused);
        PLANETSIM_EXPECT(test, unmigrated.slow().sea_level_m == 0.0);   // untouched

        planetsim::PlanetState state(mesh);
        const auto manifest = planetsim::read_snapshot(
            golden_path, state, planetsim::surface_energy_migration(parameters, surface));
        PLANETSIM_EXPECT(test, manifest.schema_version == 1U);
        PLANETSIM_EXPECT(test, manifest.tick == synthetic_tick);
        PLANETSIM_EXPECT(test, manifest.mesh_level == 0U);
        PLANETSIM_EXPECT(test, manifest.cell_count == 12U);
        PLANETSIM_EXPECT(test, manifest.fields.size() == 2U);
        PLANETSIM_EXPECT(test, manifest.mesh_generator_version == 2U);
        PLANETSIM_EXPECT(test,
                         manifest.mesh_checksum == planetsim::mesh_geometry_checksum(*mesh));
        check_schema_1_fields(test, state);

        planetsim::SlowState expected = state.slow();
        planetsim::initialise_surface_temperatures(*mesh, expected, parameters, surface);
        bool migrated = true;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            migrated = migrated &&
                       state.slow().land_surface_temperature_K[cell] ==
                           expected.land_surface_temperature_K[cell] &&
                       state.slow().ocean_deep_temperature_K[cell] ==
                           expected.ocean_deep_temperature_K[cell] &&
                       state.slow().land_surface_temperature_K[cell] > 150.0F &&
                       state.slow().ocean_mixed_layer_temperature_K[cell] > 150.0;
        }
        PLANETSIM_EXPECT(test, migrated);
        PLANETSIM_EXPECT(test, !state.has_fast_state());
        PLANETSIM_EXPECT_NEAR(test, state.forcing().incident_solar_flux_W_m2, 0.0, 0.0);
    }

    // Schema 2 (M3-02): the mixed layer is stored as float32 under the retired
    // ID 0x0003'0003; the schema 2 -> 3 step widens it exactly, with no
    // initialiser (ADR-0007 §10).
    {
        planetsim::PlanetState state(mesh);
        const auto manifest = planetsim::read_snapshot(
            std::filesystem::path("tests/data/golden/psnap-v2-l0.psnap"), state);
        PLANETSIM_EXPECT(test, manifest.schema_version == 2U);
        PLANETSIM_EXPECT(test, manifest.fields.size() == 6U);
        PLANETSIM_EXPECT(test, manifest.fields.size() == 6U &&
                                   manifest.fields[4].field_id == 0x0003'0003U &&
                                   manifest.fields[4].dtype == "float32");
        check_schema_1_fields(test, state);
        bool exact = true;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            const double widened =
                static_cast<double>(static_cast<float>(expected_temperature(cell, 22U)));
            exact = exact &&
                    state.slow().land_surface_temperature_K[cell] ==
                        static_cast<float>(expected_temperature(cell, 20U)) &&
                    state.slow().land_ground_temperature_K[cell] ==
                        static_cast<float>(expected_temperature(cell, 21U)) &&
                    std::bit_cast<std::uint64_t>(
                        state.slow().ocean_mixed_layer_temperature_K[cell]) ==
                        std::bit_cast<std::uint64_t>(widened) &&
                    std::bit_cast<std::uint64_t>(state.slow().ocean_deep_temperature_K[cell]) ==
                        std::bit_cast<std::uint64_t>(expected_temperature(cell, 23U));
        }
        PLANETSIM_EXPECT(test, exact);
    }

    // Schema 3 (M3 close): the mixed layer is float64; every field stored.
    {
        planetsim::PlanetState state(mesh);
        const auto manifest = planetsim::read_snapshot(
            std::filesystem::path("tests/data/golden/psnap-v3-l0.psnap"), state);
        PLANETSIM_EXPECT(test, manifest.schema_version == 3U);
        PLANETSIM_EXPECT(test, manifest.fields.size() == 6U);
        check_schema_1_fields(test, state);
        bool exact = true;
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            exact = exact &&
                    state.slow().land_surface_temperature_K[cell] ==
                        static_cast<float>(expected_temperature(cell, 20U)) &&
                    state.slow().land_ground_temperature_K[cell] ==
                        static_cast<float>(expected_temperature(cell, 21U)) &&
                    std::bit_cast<std::uint64_t>(
                        state.slow().ocean_mixed_layer_temperature_K[cell]) ==
                        std::bit_cast<std::uint64_t>(expected_temperature(cell, 22U)) &&
                    std::bit_cast<std::uint64_t>(state.slow().ocean_deep_temperature_K[cell]) ==
                        std::bit_cast<std::uint64_t>(expected_temperature(cell, 23U));
        }
        PLANETSIM_EXPECT(test, exact);
    }
    return test.result();
}
