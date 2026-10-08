#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

// The coupled climate transport's solve (ADR-0011 §4.7, §17.1; task M6-05
// step A) on a mesh graph with synthetic columns and fluxes.
namespace {

constexpr double radius_m = 6'371'000.0;
constexpr double heat_capacity = 1004.64;
constexpr std::size_t layers = 3;

[[nodiscard]] double unit_random(std::size_t index, std::uint32_t sample) {
    return planetsim::keyed_random_unit_double(20'261'006U, planetsim::RandomStreamId::validation,
                                               0, static_cast<std::uint32_t>(index), sample);
}

// The CSR entry of the face b → a, for every entry a → b.
[[nodiscard]] std::vector<std::size_t> reverse_entries(const planetsim::TransportGraph& graph) {
    std::vector<std::size_t> reverse(graph.neighbour.size());
    for (std::size_t a = 0; a < graph.size(); ++a) {
        for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
            const std::size_t b = graph.neighbour[k];
            for (std::size_t q = graph.offset[b]; q < graph.offset[b + 1U]; ++q) {
                if (graph.neighbour[q] == a) {
                    reverse[k] = q;
                }
            }
        }
    }
    return reverse;
}

// Antisymmetric layer fluxes with no column divergence on any face (the
// bottom layer's flux returns in the others), of `scale` kg/s, and the
// eddies' conductance `g` times the faces' l/d.
[[nodiscard]] planetsim::AdvectionDiffusion transport_on(const planetsim::TransportGraph& graph,
                                                         double scale, double g) {
    const std::size_t entries = graph.neighbour.size();
    const auto reverse = reverse_entries(graph);
    planetsim::AdvectionDiffusion transport;
    transport.layers = layers;
    transport.conductance_W_K.resize(entries);
    transport.outflow_kg_s.assign(layers * entries, 0.0);
    for (std::size_t a = 0; a < graph.size(); ++a) {
        for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
            transport.conductance_W_K[k] = g * graph.weight[k];
            const std::size_t b = graph.neighbour[k];
            if (a < b) {
                const double f = scale * (2.0 * unit_random(k, 1U) - 1.0);
                const double split = unit_random(k, 2U);
                const double flux[layers] = {f, -split * f, -(1.0 - split) * f};
                for (std::size_t l = 0; l < layers; ++l) {
                    transport.outflow_kg_s[l * entries + k] = flux[l];
                    transport.outflow_kg_s[l * entries + reverse[k]] = -flux[l];
                }
            }
        }
    }
    return transport;
}

// A nonlinear column: a θ + r θ⁴ = b + h (ADR-0009's test column), and
// s_l = c_p θ (1 + ε_l) + Φ_l with fixed layer offsets.
struct Column {
    std::size_t nodes = 0;
    double a = 3.0e5 / (30.0 * 86'400.0);
    std::vector<double> b;
    double radiative = 0.75 * 0.97 * planetsim::stefan_boltzmann_W_m2_K4;
    double epsilon[layers] = {0.0, -0.08, -0.16};
    double geopotential[layers] = {3.0e4, 4.5e4, 7.0e4};

    void operator()(const planetsim::Field2D<double>& source, planetsim::Field2D<double>& mean,
                    planetsim::Field2D<double>& slope, std::vector<double>& energy,
                    std::vector<double>& energy_slope) const {
        mean = planetsim::Field2D<double>(nodes, 0.0);
        slope = planetsim::Field2D<double>(nodes, 0.0);
        energy.assign(layers * nodes, 0.0);
        energy_slope.assign(layers * nodes, 0.0);
        for (std::size_t i = 0; i < nodes; ++i) {
            planetsim::ColumnSystem system;
            system.a = a;
            system.b = b[i] + source[i];
            system.radiative = radiative;
            const double x = planetsim::solve_column_surface(system);
            mean[i] = x;
            slope[i] = system.slope_K_m2_W(x);
            for (std::size_t l = 0; l < layers; ++l) {
                energy[l * nodes + i] = heat_capacity * x * (1.0 + epsilon[l]) + geopotential[l];
                energy_slope[l * nodes + i] = heat_capacity * slope[i] * (1.0 + epsilon[l]);
            }
        }
    }
};

[[nodiscard]] Column column_for(std::size_t nodes) {
    Column column;
    column.nodes = nodes;
    column.b.resize(nodes);
    for (std::size_t i = 0; i < nodes; ++i) {
        const double target = 200.0 + 120.0 * unit_random(i, 0U);
        column.b[i] = column.a * target + column.radiative * std::pow(target, 4);
    }
    return column;
}

[[nodiscard]] planetsim::Field2D<double> floor_for(const Column& column) {
    planetsim::Field2D<double> floor(column.nodes, 0.0);
    for (std::size_t i = 0; i < column.nodes; ++i) {
        const double x = 100.0;
        floor[i] = column.a * x + column.radiative * std::pow(x, 4) - column.b[i];
    }
    return floor;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = planetsim::make_icosphere(3, radius_m);
    const auto graph = planetsim::mesh_transport_graph(mesh);
    const std::size_t nodes = graph.size();
    const double conductance = 0.5 * radius_m * radius_m;   // D = 0.5 W/m²/K
    const auto column = column_for(nodes);
    const auto floor = floor_for(column);

    // 1. A uniform s with divergence-free column fluxes carries nothing; the
    // source conserves energy for any θ and s.
    {
        const auto transport = transport_on(graph, 1.0e9, 0.0);
        planetsim::Field2D<double> mean(nodes, 260.0);
        std::vector<double> uniform(layers * nodes, 2.9e5);
        planetsim::Field2D<double> eddy;
        planetsim::Field2D<double> advective;
        planetsim::advection_diffusion_source(graph, transport, mean, uniform, eddy, advective);
        double largest = 0.0;
        for (std::size_t i = 0; i < nodes; ++i) {
            largest = std::max(largest, std::abs(advective[i]));
        }
        PLANETSIM_EXPECT(test, largest < 1.0e-9);
        std::vector<double> varied(layers * nodes);
        for (std::size_t i = 0; i < varied.size(); ++i) {
            varied[i] = 2.5e5 + 1.0e5 * unit_random(i, 3U);
        }
        planetsim::advection_diffusion_source(graph, transport, mean, varied, eddy, advective);
        double sum = 0.0;
        double absolute = 0.0;
        for (std::size_t i = 0; i < nodes; ++i) {
            sum += graph.area_m2[i] * advective[i];
            absolute += graph.area_m2[i] * std::abs(advective[i]);
        }
        PLANETSIM_EXPECT(test, absolute > 0.0 && std::abs(sum) <= 1.0e-12 * absolute);
    }

    // 2. Without fluxes, with the conductance of ADR-0009, it is ADR-0009's
    // solve.
    {
        const auto transport = transport_on(graph, 0.0, conductance);
        const auto coupled = planetsim::solve_implicit_advection_diffusion(
            graph, transport, floor, column, {}, 4U);
        const planetsim::TransportResponse scalar = [&](const planetsim::Field2D<double>& h,
                                                        planetsim::Field2D<double>& mean,
                                                        planetsim::Field2D<double>& slope) {
            std::vector<double> energy;
            std::vector<double> energy_slope;
            column(h, mean, slope, energy, energy_slope);
        };
        const auto diffusive =
            planetsim::solve_implicit_transport(graph, conductance, floor, scalar, {}, 4U);
        double largest = 0.0;
        for (std::size_t i = 0; i < nodes; ++i) {
            largest = std::max(largest, std::abs(coupled.source_W_m2[i] - diffusive.source_W_m2[i]));
        }
        std::cout << "diffusion only: max |H - H_ADR-0009| = " << largest
                  << " W/m2, newton=" << coupled.newton_iterations
                  << " linear=" << coupled.linear_iterations << '\n';
        PLANETSIM_EXPECT(test, coupled.consistency_residual_W_m2 <= 1.0e-6);
        PLANETSIM_EXPECT(test, largest < 1.0e-5);
    }

    // 3. Advection and diffusion together: Newton converges, conserves
    // energy, and is bit-identical for 1 and 4 workers.
    {
        const auto transport = transport_on(graph, 5.0e9, conductance);
        const auto serial =
            planetsim::solve_implicit_advection_diffusion(graph, transport, floor, column, {}, 1U);
        const auto parallel =
            planetsim::solve_implicit_advection_diffusion(graph, transport, floor, column, {}, 4U);
        std::cout << "coupled: newton=" << parallel.newton_iterations
                  << " linear=" << parallel.linear_iterations
                  << " consistency=" << parallel.consistency_residual_W_m2
                  << " eddy_W=" << parallel.eddy_absolute_sum_W
                  << " advective_W=" << parallel.advective_absolute_sum_W << '\n';
        PLANETSIM_EXPECT(test, parallel.consistency_residual_W_m2 <= 1.0e-6);
        PLANETSIM_EXPECT(test, parallel.advective_absolute_sum_W > 0.1 * parallel.eddy_absolute_sum_W);
        PLANETSIM_EXPECT(test, std::abs(parallel.sum_W) <= 1.0e-12 * parallel.absolute_sum_W);
        bool same = true;
        for (std::size_t i = 0; i < nodes; ++i) {
            same = same && serial.source_W_m2[i] == parallel.source_W_m2[i];
        }
        PLANETSIM_EXPECT(test, same);
        // The source is the operator applied to the columns it was solved
        // with: h = H(θ(h), s(h)) at the solution.
        planetsim::Field2D<double> mean;
        planetsim::Field2D<double> slope;
        std::vector<double> energy;
        std::vector<double> energy_slope;
        column(parallel.source_W_m2, mean, slope, energy, energy_slope);
        planetsim::Field2D<double> eddy;
        planetsim::Field2D<double> advective;
        planetsim::advection_diffusion_source(graph, transport, mean, energy, eddy, advective);
        double largest = 0.0;
        for (std::size_t i = 0; i < nodes; ++i) {
            largest = std::max(largest, std::abs(parallel.source_W_m2[i] - eddy[i] - advective[i]));
        }
        PLANETSIM_EXPECT(test, largest <= 1.0e-6);
    }

    // 4. Invalid inputs are refused.
    {
        auto transport = transport_on(graph, 0.0, conductance);
        transport.conductance_W_K[0] = -1.0;
        PLANETSIM_EXPECT_THROWS(
            test, std::invalid_argument,
            planetsim::solve_implicit_advection_diffusion(graph, transport, floor, column));
    }
    return test.result();
}
