#include "sim/core/random/counter_rng.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

struct TestBlock {
    std::size_t begin = 0;
    std::size_t end = 0;
};

}  // namespace

int main() {
    planetsim::test::Context test;

    planetsim::SimulationClock clock;
    PLANETSIM_EXPECT(test, clock.tick() == 0);
    PLANETSIM_EXPECT_NEAR(test, clock.time_s(), 0.0, 0.0);

    clock.advance_ticks(1);
    clock.advance_ticks(2);
    PLANETSIM_EXPECT(test, clock.tick() == 3);
    PLANETSIM_EXPECT_NEAR(test, clock.time_s(), 180.0, 0.0);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, clock.advance_ticks(0));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, clock.advance_ticks(-1));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, clock.set_tick(-1));

    clock.set_tick(std::numeric_limits<planetsim::SimulationTick>::max());
    PLANETSIM_EXPECT_THROWS(test, std::overflow_error, clock.advance_ticks(1));
    clock.reset();
    PLANETSIM_EXPECT(test, clock.tick() == 0);

    constexpr std::uint64_t seed = 0x1234'5678'9abc'def0ULL;
    constexpr auto stream = planetsim::RandomStreamId::validation;
    const auto first = planetsim::keyed_random_u64(seed, stream, 42, 7U);
    PLANETSIM_EXPECT(test, first == planetsim::keyed_random_u64(seed, stream, 42, 7U));
    PLANETSIM_EXPECT(test, first != planetsim::keyed_random_u64(seed, stream, 43, 7U));
    PLANETSIM_EXPECT(test, first != planetsim::keyed_random_u64(seed, stream, 42, 8U));
    const double unit_random = planetsim::keyed_random_unit_double(seed, stream, 42, 7U);
    PLANETSIM_EXPECT(test, unit_random >= 0.0);
    PLANETSIM_EXPECT(test, unit_random < 1.0);

    constexpr std::size_t random_cell_count = 1'024U;
    std::array<TestBlock, 16> random_blocks{};
    for (std::size_t block = 0; block < random_blocks.size(); ++block) {
        random_blocks[block] = {block * 64U, (block + 1U) * 64U};
    }
    std::vector<std::uint64_t> serial_random(random_cell_count);
    std::vector<std::uint64_t> threaded_random(random_cell_count);
    const auto fill_random = [&](std::vector<std::uint64_t>& output, std::size_t workers) {
        planetsim::for_each_deterministic_block(
            std::span<const TestBlock>{random_blocks}, workers,
            [&](std::size_t, const TestBlock& block) {
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    output[cell] = planetsim::keyed_random_u64(
                        seed, stream, 42, static_cast<std::uint32_t>(cell), 3U);
                }
            });
    };
    fill_random(serial_random, 1U);
    fill_random(threaded_random, 16U);
    PLANETSIM_EXPECT(test, serial_random == threaded_random);

    auto parameters = planetsim::PlanetParameters::earth_reference();
    parameters.validate();
    PLANETSIM_EXPECT_NEAR(test, parameters.radius_m, 6'371'000.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.mass_kg, 5.9722e24, 0.0);
    PLANETSIM_EXPECT(test, parameters.mesh_subdivision == 6);
    PLANETSIM_EXPECT_NEAR(test, parameters.sidereal_rotation_period_s, 86'164.0905, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.orbital_period_s, 31'556'925.216, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.axial_tilt_rad, 0.4090926006005829, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.semi_major_axis_m, 149'597'870'700.0, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.eccentricity, 0.0167086, 0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.longitude_periapsis_rad, std::numbers::pi_v<double>,
                          0.0);
    PLANETSIM_EXPECT_NEAR(test, parameters.star_luminosity_W, 3.827531850364947e26, 0.0);
    PLANETSIM_EXPECT(test, planetsim::PlanetParameters::earth_development().mesh_subdivision == 5);

    parameters.radius_m = -1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.sidereal_rotation_period_s = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.orbital_period_s = std::numeric_limits<double>::infinity();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.axial_tilt_rad = std::numbers::pi_v<double> + 0.01;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.mass_kg = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.semi_major_axis_m = 0.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.eccentricity = 1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.longitude_periapsis_rad = std::numeric_limits<double>::quiet_NaN();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.star_luminosity_W = -1.0;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());
    parameters = planetsim::PlanetParameters::earth_reference();
    parameters.initial_rotation_angle_rad = std::numeric_limits<double>::quiet_NaN();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, parameters.validate());

    return test.result();
}
