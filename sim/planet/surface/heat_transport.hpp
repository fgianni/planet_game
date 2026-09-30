#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace planetsim {

// Diffusive horizontal heat transport, integrated implicitly (ADR-0009):
//
//   H_c = K · (1/A_c) Σ_e (l_e / d_e) (T̄_n − T̄_c)
//
// where T̄ is the cells' surface temperature after a local step that
// receives H as a source. The solver knows nothing about tiles: a response
// callback returns, for given sources h, each cell's temperature T̄(h) and
// its slope dT̄/dh ≥ 0 (0 where a phase change absorbs extra heat).

// The graph the transport runs on: nodes with areas, and for each node its
// neighbours with the two-point weights l/d (boundary length over the
// distance between centres), in CSR form; fixed blocks of nodes for
// parallel work. Either the mesh itself or its agglomeration (ADR-0009 §12).
struct TransportGraph {
    std::vector<double> area_m2;
    std::vector<std::size_t> offset;      // size() + 1
    std::vector<std::size_t> neighbour;
    std::vector<double> weight;           // l / d
    std::vector<double> weight_sum;       // Σ weight per node
    std::vector<CellBlock> blocks;

    [[nodiscard]] std::size_t size() const noexcept { return area_m2.size(); }
};

[[nodiscard]] TransportGraph mesh_transport_graph(const PlanetMesh& mesh);

// The transport graph one mesh level coarser (ADR-0009 §12): every fine cell
// belongs to the nearest cell of the mesh one level coarser (about four
// fine cells each); groups take the fine cells' areas and the coarse mesh's
// two-point weights. Built once per mesh level and radius and cached for
// the process; `group_of_cell` points at each fine cell's group.
[[nodiscard]] const TransportGraph& agglomerated_transport_graph(
    const PlanetMesh& mesh, const std::vector<std::size_t>*& group_of_cell);

using TransportResponse = std::function<void(const Field2D<double>& source_W_m2,
                                             Field2D<double>& mean_K,
                                             Field2D<double>& slope_K_m2_W)>;

struct ImplicitTransportSettings {
    int max_newton_iterations = 30;
    int max_line_search_halvings = 12;
    // A failed line search ends the solve only below this residual (the
    // rounding floor); above it, up to max_non_monotone_steps full steps are
    // taken across kinks of the tiles' response.
    double rounding_floor_W_m2 = 1e-3;
    int max_non_monotone_steps = 4;
    // Stop once max |h − K ∇² T̄(h)| is below: ADR-0009 V7's gate. The cell
    // solves converge T̄ to about 1e-12 relative, so the residual cannot fall
    // much below 1e-7 W/m² at Earth-like conductance.
    double newton_tolerance_W_m2 = 1e-6;
    double cg_relative_tolerance = 1e-6;   // inexact Newton: the outer loop converges regardless
    int max_cg_iterations = 2'000;
};

struct ImplicitTransportResult {
    Field2D<double> source_W_m2;   // H = K ∇² T̄: the transport to apply
    Field2D<double> mean_K;        // the T̄ it was computed from
    double consistency_residual_W_m2 = 0.0;   // max |H − h| at the last iterate
    int newton_iterations = 0;
    int cg_iterations = 0;         // summed over Newton iterations
    double sum_W = 0.0;            // Σ A H: zero to rounding
    double absolute_sum_W = 0.0;   // Σ A |H|
    double dissipation_W_K = 0.0;  // Σ A H T̄ ≤ 0: transport runs down the gradient
};

// The two-point Laplacian of ADR-0002 on the graph times K (W/K), in W/m².
void diffusion_source(const TransportGraph& graph, double conductance_W_K,
                      const Field2D<double>& temperature_K, Field2D<double>& source_W_m2,
                      std::size_t worker_count = 1U);

// ADR-0009 §4.3. Every Newton iterate is kept at or above `source_floor`
// (per cell), where the caller's local solves still have a positive root.
// Bit-identical for any worker count: all parallel work runs over the
// mesh's fixed blocks with block-ordered reductions.
[[nodiscard]] ImplicitTransportResult solve_implicit_transport(
    const TransportGraph& graph, double conductance_W_K, const Field2D<double>& source_floor_W_m2,
    const TransportResponse& response, const ImplicitTransportSettings& settings = {},
    std::size_t worker_count = 1U);

}  // namespace planetsim
