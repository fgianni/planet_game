#include "sim/core/random/counter_rng.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t synthetic_seed = 0x9B97'F4A7'C150'0011ULL;
constexpr planetsim::SimulationTick synthetic_tick = 123'456;

void populate_synthetic_state(planetsim::PlanetState& state) {
    for (std::size_t cell = 0; cell < state.mesh().cell_count(); ++cell) {
        std::array<float, planetsim::hypsometry_layer_count> quantiles{};
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            const double unit = planetsim::keyed_random_unit_double(
                synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick,
                static_cast<std::uint32_t>(cell), static_cast<std::uint32_t>(layer));
            quantiles[layer] = static_cast<float>(-8'000.0 + unit * 16'000.0);
        }
        std::sort(quantiles.begin(), quantiles.end());
        for (std::size_t layer = 0; layer < quantiles.size(); ++layer) {
            state.slow().hypsometry_m.at(
                layer, planetsim::CellId{static_cast<std::uint32_t>(cell)}) = quantiles[layer];
        }
    }
    const double sea_unit = planetsim::keyed_random_unit_double(
        synthetic_seed, planetsim::RandomStreamId::validation, synthetic_tick, 0U, 99U);
    state.slow().sea_level_m = -200.0 + sea_unit * 400.0;
}

[[nodiscard]] std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    planetsim::test::Context test;

    const auto first_path =
        std::filesystem::temp_directory_path() / "planetsim_snapshot_writer_first.psnap";
    const auto second_path =
        std::filesystem::temp_directory_path() / "planetsim_snapshot_writer_second.psnap";

    auto first_mesh =
        std::make_shared<const planetsim::PlanetMesh>(planetsim::make_icosphere(0, 1.0));
    auto second_mesh =
        std::make_shared<const planetsim::PlanetMesh>(planetsim::make_icosphere(0, 1.0));
    planetsim::PlanetState first(first_mesh);
    planetsim::PlanetState second(second_mesh);
    populate_synthetic_state(first);
    populate_synthetic_state(second);
    first.open_fast_state();
    first.forcing().top_of_atmosphere_insolation_W_m2[0] = 777.0F;

    planetsim::write_snapshot(first_path, first, synthetic_tick);
    planetsim::write_snapshot(second_path, second, synthetic_tick);

    const auto first_bytes = read_file(first_path);
    const auto second_bytes = read_file(second_path);
    PLANETSIM_EXPECT(test, first_bytes == second_bytes);
    PLANETSIM_EXPECT(test, first_bytes.size() > 16U);
    if (first_bytes.size() >= 8U) {
        const std::array<char, 8> expected_magic{'P', 'S', 'N', 'A', 'P', 'v', '1', '\0'};
        PLANETSIM_EXPECT(test,
                         std::equal(expected_magic.begin(), expected_magic.end(),
                                    first_bytes.begin()));
    }
    const std::string file_text(first_bytes.begin(), first_bytes.end());
    PLANETSIM_EXPECT(test, file_text.find("\"compression\":\"none\"") != std::string::npos);
    PLANETSIM_EXPECT(test, file_text.find("hypsometry_m") != std::string::npos);
    PLANETSIM_EXPECT(test, file_text.find("sea_level_m") != std::string::npos);
    PLANETSIM_EXPECT(test,
                     file_text.find("top_of_atmosphere_insolation") == std::string::npos);

    std::filesystem::remove(first_path);
    std::filesystem::remove(second_path);
    return test.result();
}
