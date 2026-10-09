#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// The implicit tracer transport (ADR-0021 §4.5, V6; task M7-04) on the
// agglomerated graph of an L3 mesh with synthetic fluxes.
namespace {

// A deterministic pseudo-random value in [−1, 1) per integer.
[[nodiscard]] double noise(std::uint64_t i) {
    std::uint64_t x = i * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) / static_cast<double>(1ULL << 52) - 1.0;
}

struct Setup {
    planetsim::PlanetMesh mesh = planetsim::make_icosphere(3U, 6'371'000.0);
    const planetsim::TransportGraph* graph = nullptr;
    planetsim::AdvectionDiffusion transport;
    std::vector<double> mass;   // per layer and node, kg

    // Fluxes of about `scale` kg/s per face, antisymmetric, and with
    // `divergent` false, summing to zero over each face's column.
    Setup(std::size_t layers, double scale, double eddy_W_K, bool divergent) {
        const std::vector<std::size_t>* groups = nullptr;
        graph = &planetsim::agglomerated_transport_graph(mesh, groups);
        const std::size_t nodes = graph->size();
        const std::size_t entries = graph->neighbour.size();
        transport.layers = layers;
        transport.conductance_W_K.assign(entries, eddy_W_K);
        transport.outflow_kg_s.assign(layers * entries, 0.0);
        for (std::size_t a = 0; a < nodes; ++a) {
            for (std::size_t k = graph->offset[a]; k < graph->offset[a + 1U]; ++k) {
                const std::size_t b = graph->neighbour[k];
                const std::size_t low = std::min(a, b);
                const std::size_t high = std::max(a, b);
                const double sign = a < b ? 1.0 : -1.0;
                double mean = 0.0;
                std::vector<double> flow(layers, 0.0);
                for (std::size_t l = 0; l < layers; ++l) {
                    flow[l] = scale * noise((low * 100'003U + high) * 16U + l);
                    mean += flow[l] / static_cast<double>(layers);
                }
                for (std::size_t l = 0; l < layers; ++l) {
                    transport.outflow_kg_s[l * entries + k] =
                        sign * (divergent ? flow[l] : flow[l] - mean);
                }
            }
        }
        mass.resize(nodes);
        for (std::size_t a = 0; a < nodes; ++a) {
            mass[a] = graph->area_m2[a] * 100'000.0 / (9.81 * static_cast<double>(layers));
        }
    }
};

[[nodiscard]] double total(const Setup& setup, const std::vector<double>& tracer) {
    const std::size_t nodes = setup.graph->size();
    double sum = 0.0;
    for (std::size_t i = 0; i < tracer.size(); ++i) {
        sum += setup.mass[i % nodes] * tracer[i];
    }
    return sum;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    constexpr double month_s = 2.63e6;

    // V6: a uniform tracer with column-divergence-free fluxes stays uniform;
    // with any fluxes its mass is conserved to rounding and it stays
    // non-negative; the result does not depend on the worker count.
    for (const bool divergent : {false, true}) {
        Setup setup(3U, 2e9, 2e8, divergent);
        const std::size_t nodes = setup.graph->size();
        std::vector<double> uniform(3U * nodes, 0.01);
        const auto kept = planetsim::solve_implicit_tracer(*setup.graph, setup.transport,
                                                           setup.mass, uniform, month_s);
        double spread = 0.0;
        for (const double c : kept.tracer) {
            spread = std::max(spread, std::abs(c / 0.01 - 1.0));
        }
        std::vector<double> field(3U * nodes, 0.0);
        for (std::size_t i = 0; i < field.size(); ++i) {
            field[i] = 0.01 * (1.0 + noise(7U * i + 3U)) + (i % 17U == 0U ? 0.0 : 1e-6);
        }
        const auto moved = planetsim::solve_implicit_tracer(*setup.graph, setup.transport,
                                                            setup.mass, field, month_s);
        const auto parallel = planetsim::solve_implicit_tracer(
            *setup.graph, setup.transport, setup.mass, field, month_s, 1e-12, 2'000, 4U);
        const double before = total(setup, field);
        const double conservation = std::abs(total(setup, moved.tracer) - before) / before;
        const double lowest = *std::min_element(moved.tracer.begin(), moved.tracer.end());
        std::cout << (divergent ? "divergent" : "column-balanced")
                  << ": uniform spread " << spread << ", conservation " << conservation
                  << ", iterations " << moved.iterations << ", residual "
                  << moved.relative_residual << ", clipped " << moved.clipped_kg / before
                  << ", column divergence " << moved.column_divergence_kg_s << " kg/s\n";
        if (!divergent) {
            PLANETSIM_EXPECT(test, spread <= 1e-9);
            PLANETSIM_EXPECT(test, moved.column_divergence_kg_s <= 1e-3);
        }
        PLANETSIM_EXPECT(test, conservation <= 1e-13);
        PLANETSIM_EXPECT(test, lowest >= 0.0);
        PLANETSIM_EXPECT(test, moved.relative_residual <= 1e-10);
        PLANETSIM_EXPECT(test, moved.clipped_kg <= 1e-12 * before);
        PLANETSIM_EXPECT(test, parallel.tracer == moved.tracer);
    }

    // The eddies alone smooth: the range of a field shrinks and its mass
    // stays.
    {
        Setup setup(2U, 0.0, 1e13, false);   // about c_p μ̄ D̄_e w
        const std::size_t nodes = setup.graph->size();
        std::vector<double> field(2U * nodes, 0.0);
        for (std::size_t i = 0; i < field.size(); ++i) {
            field[i] = 0.01 * (1.0 + noise(i));
        }
        const auto smoothed = planetsim::solve_implicit_tracer(*setup.graph, setup.transport,
                                                               setup.mass, field, month_s);
        const auto [low0, high0] = std::minmax_element(field.begin(), field.end());
        const auto [low1, high1] =
            std::minmax_element(smoothed.tracer.begin(), smoothed.tracer.end());
        PLANETSIM_EXPECT(test, *high1 - *low1 < 0.5 * (*high0 - *low0));
        PLANETSIM_EXPECT(test, std::abs(total(setup, smoothed.tracer) / total(setup, field) - 1.0) <=
                                   1e-13);
    }
    return test.result();
}
