#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
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

// The mesh one level coarser that the agglomerated graph's groups are the
// cells of (the mesh itself at level 0), cached with the graph.
[[nodiscard]] const PlanetMesh& agglomerated_mesh(const PlanetMesh& mesh);

// The transport's aggregation-multigrid V-cycle (ADR-0009 §4.3), for other
// solves on a transport graph (ADR-0011 §4.6 preconditions the balanced
// surface pressure with it). The hierarchy follows the graph's two-point
// weights; set_matrix gives the fine matrix (diagonal, and off-diagonals in
// the graph's CSR order), which must be symmetric with a positive definite
// Galerkin coarsest level. One V-cycle with symmetric Gauss–Seidel
// smoothing approximates its inverse. Fine-level work runs over the graph's
// fixed blocks, so the result does not depend on the worker count.
class GraphMultigrid {
  public:
    explicit GraphMultigrid(const TransportGraph& graph);
    ~GraphMultigrid();
    GraphMultigrid(const GraphMultigrid&) = delete;
    GraphMultigrid& operator=(const GraphMultigrid&) = delete;
    GraphMultigrid(GraphMultigrid&&) noexcept;
    GraphMultigrid& operator=(GraphMultigrid&&) noexcept;

    void set_matrix(std::span<const double> diagonal, std::span<const double> off_diagonal);
    void precondition(const std::vector<double>& rhs, std::vector<double>& solution,
                      std::size_t worker_count = 1U) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

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

// The coupled climate transport (ADR-0011 §3.5, §4.7, §17): on a transport
// graph, the eddies' diffusion of the columns' θ_c with per-face
// conductances, and the advection of each layer's dry static energy s_k by
// fixed layer mass fluxes, upwind. Per CSR entry k of node a (graph.neighbour
// order):
//   conductance_W_K[k]        G: a gains G (θ_b − θ_a); symmetric;
//   outflow_kg_s[l · E + k]   F: the mass of layer l leaving a across the
//                             face (kg/s); antisymmetric.
// The source at a, H_a = (1/A_a) [Σ_k G (θ_b − θ_a) − Σ_l Σ_k F s_l,up],
// conserves energy exactly (Σ A H = 0) and vanishes for a uniform s when the
// column's fluxes have no divergence.
struct AdvectionDiffusion {
    std::size_t layers = 0;
    std::vector<double> conductance_W_K;
    std::vector<double> outflow_kg_s;
};

// The columns' response to a source h (W/m²) per node: θ_c and dθ_c/dh, and
// each layer's s_l (J/kg) and ds_l/dh (layer-major, layers × nodes).
using AdvectionResponse = std::function<void(
    const Field2D<double>& source_W_m2, Field2D<double>& mean_K, Field2D<double>& slope_K_m2_W,
    std::vector<double>& energy_J_kg, std::vector<double>& energy_slope_J_kg_m2_W)>;

struct AdvectionDiffusionResult {
    Field2D<double> source_W_m2;   // H at the last iterate: the transport to apply
    Field2D<double> mean_K;        // the θ_c it was computed from
    std::vector<double> energy_J_kg;   // the s_l it was computed from
    double consistency_residual_W_m2 = 0.0;   // max |H − h| at the last iterate
    int newton_iterations = 0;
    int linear_iterations = 0;     // BiCGSTAB's, summed over Newton iterations
    double sum_W = 0.0;            // Σ A H: zero to rounding
    double absolute_sum_W = 0.0;   // Σ A |H|
    double eddy_absolute_sum_W = 0.0;        // Σ A |H_eddy|
    double advective_absolute_sum_W = 0.0;   // Σ A |H_advective|
};

// The source of `transport` for given θ_c and s_l (no response): W/m², and
// its eddy and advective parts.
void advection_diffusion_source(const TransportGraph& graph, const AdvectionDiffusion& transport,
                                const Field2D<double>& mean_K,
                                const std::vector<double>& energy_J_kg,
                                Field2D<double>& eddy_W_m2, Field2D<double>& advective_W_m2,
                                std::size_t worker_count = 1U);

// What the climate circulation hands the surface step each climate month
// (ADR-0011 §17.1, §17.4), on the agglomerated graph of the step's mesh.
// Inactive when the month's circulation failed: the step then uses ADR-0009's
// diffusion with its calibrated D.
struct CirculationTransport {
    bool active = false;
    AdvectionDiffusion transport;
    // Per fine cell: Φ_s of the layers' dry static energy (the winds'
    // orography, as the fluxes'; ADR-0011 §13).
    std::vector<double> surface_geopotential_m2_s2;
};

// ADR-0011 §17.1: ADR-0009's Newton on h, h = H(θ_c(h), s(h)), with its
// floors and line search. The Newton system J = I − ∂H/∂h is assembled on
// the graph's pattern and solved by BiCGSTAB preconditioned with its ILU(0)
// (settings' CG tolerance and iteration cap apply to it). Bit-identical for
// any worker count.
[[nodiscard]] AdvectionDiffusionResult solve_implicit_advection_diffusion(
    const TransportGraph& graph, const AdvectionDiffusion& transport,
    const Field2D<double>& source_floor_W_m2, const AdvectionResponse& response,
    const ImplicitTransportSettings& settings = {}, std::size_t worker_count = 1U);

}  // namespace planetsim
