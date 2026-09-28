#include "sim/core/random/counter_rng.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>

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

}  // namespace

int main() {
    planetsim::test::Context test;

    const auto golden_path =
        std::filesystem::path("tests/data/golden/psnap-v1-l0.psnap");
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(0U, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    const auto manifest = planetsim::read_snapshot(golden_path, state);

    PLANETSIM_EXPECT(test, manifest.schema_version == 1U);
    PLANETSIM_EXPECT(test, manifest.tick == synthetic_tick);
    PLANETSIM_EXPECT(test, manifest.mesh_level == 0U);
    PLANETSIM_EXPECT(test, manifest.cell_count == 12U);
    PLANETSIM_EXPECT(test, manifest.fields.size() == 2U);

    for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
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
    PLANETSIM_EXPECT(test, !state.has_fast_state());
    PLANETSIM_EXPECT_NEAR(test, state.forcing().incident_solar_flux_W_m2, 0.0, 0.0);

    return test.result();
}
