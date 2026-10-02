#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/dynamics/orography.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

// ADR-0011 §13: the orography the winds see.
int main() {
    planetsim::test::Context test;
    const auto mesh = planetsim::make_icosphere(4U, 6'371'000.0);
    const std::size_t cells = mesh.cell_count();

    // Rough terrain: random heights up to 6 km on a quarter of the cells.
    planetsim::Field2D<double> height(cells, 0.0);
    for (std::size_t i = 0; i < cells; ++i) {
        const double draw = planetsim::keyed_random_unit_double(
            0x0613U, planetsim::RandomStreamId::validation, 0, static_cast<std::uint32_t>(i));
        height[i] = draw < 0.25 ? 24'000.0 * draw : 0.0;
    }
    const auto mean = [&](const planetsim::Field2D<double>& field) {
        double sum = 0.0;
        double area = 0.0;
        for (const auto& cell : mesh.cells()) {
            sum += cell.area_m2 * field[cell.id];
            area += cell.area_m2;
        }
        return sum / area;
    };
    const auto steepest = [&](const planetsim::Field2D<double>& field) {
        double step = 0.0;
        for (const auto& edge : mesh.edges()) {
            step = std::max(step, std::abs(field[edge.first_cell] - field[edge.second_cell]));
        }
        return step;
    };

    const auto smoothed = planetsim::smooth_dynamics_orography(mesh, height, 5U);
    // Mean exact; no new extremes.
    PLANETSIM_EXPECT_NEAR(test, mean(smoothed), mean(height), 1.0e-12 * mean(height));
    double low = 1.0e30;
    double high = -1.0e30;
    for (const double value : smoothed.values()) {
        low = std::min(low, value);
        high = std::max(high, value);
    }
    PLANETSIM_EXPECT(test, low >= 0.0);
    PLANETSIM_EXPECT(test, high <= *std::max_element(height.values().begin(), height.values().end()));
    PLANETSIM_EXPECT(test, steepest(smoothed) < steepest(height));

    // The step rule reaches its limit with the fewest passes, and is
    // bit-identical on any worker count.
    const auto limited = planetsim::limit_dynamics_orography_steps(mesh, height, 800.0, 256U, 1U);
    PLANETSIM_EXPECT(test, limited.max_step_m <= 800.0);
    PLANETSIM_EXPECT(test, limited.passes > 0U);
    PLANETSIM_EXPECT(test, steepest(planetsim::smooth_dynamics_orography(
                               mesh, height, limited.passes - 1U)) > 800.0);
    PLANETSIM_EXPECT_NEAR(test, mean(limited.height_m), mean(height), 1.0e-12 * mean(height));
    const auto parallel = planetsim::limit_dynamics_orography_steps(mesh, height, 800.0, 256U, 8U);
    bool equal = parallel.passes == limited.passes;
    for (std::size_t i = 0; i < cells; ++i) {
        equal = equal && parallel.height_m[i] == limited.height_m[i];
    }
    PLANETSIM_EXPECT(test, equal);

    // Too few passes allowed: refuse rather than return steep terrain.
    PLANETSIM_EXPECT_THROWS(test, std::runtime_error,
                            planetsim::limit_dynamics_orography_steps(mesh, height, 800.0, 1U));

    // Flat terrain needs no passes.
    const auto flat = planetsim::limit_dynamics_orography_steps(
        mesh, planetsim::Field2D<double>(cells, 100.0));
    PLANETSIM_EXPECT(test, flat.passes == 0U);

    return test.result();
}
