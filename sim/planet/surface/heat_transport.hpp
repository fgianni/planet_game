#pragma once

#include "sim/core/fields/field.hpp"

#include <cstddef>
#include <functional>

namespace planetsim {

class PlanetMesh;

// Diffusive horizontal heat transport, integrated implicitly (ADR-0009):
//
//   H_c = K · (1/A_c) Σ_e (l_e / d_e) (T̄_n − T̄_c)
//
// where T̄ is the cells' surface temperature after a local step that
// receives H as a source. The solver knows nothing about tiles: a response
// callback returns, for given sources h, each cell's temperature T̄(h) and
// its slope dT̄/dh ≥ 0 (0 where a phase change absorbs extra heat).

using TransportResponse = std::function<void(const Field2D<double>& source_W_m2,
                                             Field2D<double>& mean_K,
                                             Field2D<double>& slope_K_m2_W)>;

struct ImplicitTransportSettings {
    int max_newton_iterations = 30;
    int max_line_search_halvings = 12;
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

// The two-point Laplacian of ADR-0002 times K (W/K), in W/m² per cell.
void diffusion_source(const PlanetMesh& mesh, double conductance_W_K,
                      const Field2D<double>& temperature_K, Field2D<double>& source_W_m2,
                      std::size_t worker_count = 1U);

// ADR-0009 §4.3. Every Newton iterate is kept at or above `source_floor`
// (per cell), where the caller's local solves still have a positive root.
// Bit-identical for any worker count: all parallel work runs over the
// mesh's fixed blocks with block-ordered reductions.
[[nodiscard]] ImplicitTransportResult solve_implicit_transport(
    const PlanetMesh& mesh, double conductance_W_K, const Field2D<double>& source_floor_W_m2,
    const TransportResponse& response, const ImplicitTransportSettings& settings = {},
    std::size_t worker_count = 1U);

}  // namespace planetsim
