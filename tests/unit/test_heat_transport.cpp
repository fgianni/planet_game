#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr double radius_m = 6'371'000.0;
constexpr double conductance_W_K = 0.5 * radius_m * radius_m;   // D = 0.5 W/m²/K

[[nodiscard]] double unit_random(std::size_t cell, std::uint32_t sample) {
    return planetsim::keyed_random_unit_double(20'260'930U, planetsim::RandomStreamId::validation,
                                               0, static_cast<std::uint32_t>(cell), sample);
}

// A tile-like response: a x + r x⁴ = b + h, one per cell, with a random
// local forcing. Cells whose index is a multiple of seven carry "snow": they
// are held at the melting point (slope 0) whenever they would warm past it,
// as ADR-0008 §4.3 holds a snow-covered surface.
struct QuarticResponse {
    const planetsim::PlanetMesh* mesh;
    double a;
    std::vector<double> b;
    double radiative = 0.75 * 0.97 * planetsim::stefan_boltzmann_W_m2_K4;

    void operator()(const planetsim::Field2D<double>& source, planetsim::Field2D<double>& mean,
                    planetsim::Field2D<double>& slope) const {
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            planetsim::ColumnSystem system;
            system.a = a;
            system.b = b[cell] + source[cell];
            system.radiative = radiative;
            const double x = planetsim::solve_column_surface(system);
            if (cell % 7U == 0U && x > 273.15) {
                mean[cell] = 273.15;
                slope[cell] = 0.0;
                continue;
            }
            mean[cell] = x;
            slope[cell] = system.slope_K_m2_W(x);
        }
    }

    [[nodiscard]] planetsim::Field2D<double> floor() const {
        planetsim::Field2D<double> values(mesh->cell_count(), 0.0);
        for (std::size_t cell = 0; cell < mesh->cell_count(); ++cell) {
            const double x = 100.0;
            values[cell] = a * x + radiative * x * x * x * x - b[cell];
        }
        return values;
    }
};

[[nodiscard]] QuarticResponse quartic(const planetsim::PlanetMesh& mesh, double a) {
    QuarticResponse response{&mesh, a, std::vector<double>(mesh.cell_count(), 0.0)};
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        // Local equilibria between about 200 and 320 K.
        const double target = 200.0 + 120.0 * unit_random(cell, 0U);
        response.b[cell] = a * target + response.radiative * std::pow(target, 4);
    }
    return response;
}

// A linear response makes the implicit problem linear: Newton solves it in
// one step up to the inner solve's tolerance, whatever the conductance.
void check_linear(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    std::vector<double> base(mesh.cell_count());
    std::vector<double> slopes(mesh.cell_count());
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        base[cell] = 250.0 + 50.0 * unit_random(cell, 1U);
        slopes[cell] = cell % 5U == 0U ? 0.0 : 0.01 + 0.2 * unit_random(cell, 2U);
    }
    const planetsim::TransportResponse response =
        [&](const planetsim::Field2D<double>& source, planetsim::Field2D<double>& mean,
            planetsim::Field2D<double>& slope) {
            for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
                mean[cell] = base[cell] + slopes[cell] * source[cell];
                slope[cell] = slopes[cell];
            }
        };
    const planetsim::Field2D<double> floor(mesh.cell_count(),
                                           -std::numeric_limits<double>::infinity());
    const auto result =
        planetsim::solve_implicit_transport(mesh, conductance_W_K, floor, response, {}, 4U);
    std::cout << "linear newton=" << result.newton_iterations << " cg=" << result.cg_iterations
              << " residual_W_m2=" << result.consistency_residual_W_m2
              << " sum_ratio=" << std::abs(result.sum_W) / result.absolute_sum_W << '\n';
    PLANETSIM_EXPECT(test, result.newton_iterations <= 3);   // inexact inner solves
    PLANETSIM_EXPECT(test, result.consistency_residual_W_m2 <= 1e-6);
    PLANETSIM_EXPECT(test, std::abs(result.sum_W) <= 1e-13 * result.absolute_sum_W);
    PLANETSIM_EXPECT(test, result.dissipation_W_K <= 0.0);
}

// The quartic response on monthly and ten-minute heat capacities, and at ten
// times the conductance: Newton converges, energy is conserved, heat runs
// down the gradient, held cells stay held, and the iterates respect the floor.
void check_quartic(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    for (const double a : {0.04 + 0.37, 1.0e5 / 600.0}) {
        for (const double factor : {1.0, 10.0}) {
            const auto response = quartic(mesh, a);
            const auto floor = response.floor();
            const auto result = planetsim::solve_implicit_transport(
                mesh, factor * conductance_W_K, floor, response, {}, 4U);
            bool held = true;
            for (std::size_t cell = 0; cell < mesh.cell_count(); cell += 7U) {
                held = held && result.mean_K[cell] <= 273.15;
            }
            double spread_before = 0.0;
            double spread_after = 0.0;
            {
                planetsim::Field2D<double> zero(mesh.cell_count(), 0.0);
                planetsim::Field2D<double> mean(mesh.cell_count(), 0.0);
                planetsim::Field2D<double> slope(mesh.cell_count(), 0.0);
                response(zero, mean, slope);
                const auto [low, high] =
                    std::minmax_element(mean.values().begin(), mean.values().end());
                spread_before = *high - *low;
                const auto [low_after, high_after] = std::minmax_element(
                    result.mean_K.values().begin(), result.mean_K.values().end());
                spread_after = *high_after - *low_after;
            }
            std::cout << "quartic a=" << a << " factor=" << factor
                      << " newton=" << result.newton_iterations << " cg=" << result.cg_iterations
                      << " residual_W_m2=" << result.consistency_residual_W_m2
                      << " spread_K " << spread_before << " -> " << spread_after << '\n';
            PLANETSIM_EXPECT(test, result.consistency_residual_W_m2 <= 1e-6);
            PLANETSIM_EXPECT(test, std::abs(result.sum_W) <= 1e-13 * result.absolute_sum_W);
            PLANETSIM_EXPECT(test, result.dissipation_W_K <= 0.0);
            PLANETSIM_EXPECT(test, held);
            PLANETSIM_EXPECT(test, spread_after <= spread_before);
        }
    }
}

// Bit-identical for any worker count.
void check_workers(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    const auto response = quartic(mesh, 0.41);
    const auto floor = response.floor();
    const auto reference =
        planetsim::solve_implicit_transport(mesh, conductance_W_K, floor, response, {}, 1U);
    bool identical = true;
    for (const std::size_t workers : {2U, 8U, 16U}) {
        const auto result =
            planetsim::solve_implicit_transport(mesh, conductance_W_K, floor, response, {},
                                                workers);
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            identical = identical && std::bit_cast<std::uint64_t>(result.source_W_m2[cell]) ==
                                         std::bit_cast<std::uint64_t>(reference.source_W_m2[cell]);
        }
        identical = identical && result.cg_iterations == reference.cg_iterations;
    }
    PLANETSIM_EXPECT(test, identical);
}

void check_validation(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    const auto response = quartic(mesh, 0.41);
    const auto floor = response.floor();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::solve_implicit_transport(mesh, 0.0, floor, response));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::solve_implicit_transport(
                                mesh, conductance_W_K, planetsim::Field2D<double>(3U, 0.0),
                                response));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = planetsim::make_icosphere(4U, radius_m);
    check_linear(test, mesh);
    check_quartic(test, mesh);
    check_workers(test, mesh);
    check_validation(test, mesh);
    return test.result();
}
