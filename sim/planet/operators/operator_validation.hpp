#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// ADR-0002 V3/V4 validation of the finite-volume operators against analytic
// fields on the sphere.
//
// The scalar test field is g = Y3 + Y2/2, built from degree-3 and degree-2
// harmonic polynomials evaluated in a fixed, generically rotated frame so that
// no error pattern can align with the icosahedron's symmetry axes. Its surface
// Laplacian is -(12 Y3 + 3 Y2) / R^2. The vector test field is
// u = grad(g) + r x grad(h) with a second harmonic h, so div(u) = lap(g) and
// the rotational part must contribute nothing.

struct OperatorErrorNorms {
    // Area-weighted RMS error over RMS exact value, pentagons excluded.
    double relative_l2 = 0.0;
    // Maximum error over maximum exact value, pentagons excluded.
    double relative_max = 0.0;
    // Maximum error at the twelve pentagons over the same maximum exact value.
    double pentagon_relative_max = 0.0;
    // Error-map pattern check. Maximum error over the maximum exact value in
    // cells within one mean spacing of an icosahedron edge (the subdivision
    // seams) and in the interior more than three spacings from any seam; both
    // regions exclude cells within eight spacings of a pentagon.
    double seam_relative_max = 0.0;
    double interior_relative_max = 0.0;
};

struct CellOperatorError {
    // Errors normalized by the operator's maximum exact value, for error maps.
    // The gradient error is a vector magnitude; the others are signed.
    double gradient = 0.0;
    double divergence = 0.0;
    double laplacian = 0.0;
    double poisson_solution = 0.0;
};

struct OperatorValidation {
    std::size_t cell_count = 0;
    // Least-squares gradient of g.
    OperatorErrorNorms gradient;
    // Divergence of u from exact edge-midpoint normal fluxes.
    OperatorErrorNorms divergence;
    // Pointwise truncation error of the two-point Laplacian of g. Reported,
    // not gated: it does not vanish next to pentagons (ADR-0002 §9).
    OperatorErrorNorms laplacian;
    // Solution error of the discrete Poisson problem lap(u) = lap(g), with u
    // and g both reduced to zero area-weighted mean. This is the Laplacian's
    // V3 accuracy measure.
    OperatorErrorNorms poisson_solution;
    std::size_t poisson_iterations = 0;
    // Per-cell normalized errors in mesh order.
    std::vector<CellOperatorError> cells;
};

[[nodiscard]] OperatorValidation validate_operators(const PlanetMesh& mesh,
                                                    std::size_t worker_count = 1U);

struct NondivergentFluxCheck {
    // max_i |div_i| / max_i (Σ_e |F_e l_e| / A_i): zero apart from rounding
    // when the discrete divergence telescopes exactly.
    double relative_max_divergence = 0.0;
    // |Σ_i A_i div_i| / Σ_i A_i |div_i| for arbitrary edge fluxes.
    double relative_global_imbalance = 0.0;
};

// V4: edge fluxes from differences of a streamfunction at the dual corners are
// discretely divergence-free, and any edge fluxes conserve the global integral.
[[nodiscard]] NondivergentFluxCheck check_nondivergent_flux(const PlanetMesh& mesh,
                                                            std::size_t worker_count = 1U);

// ADR-0011 V1: discrete identities of the C-grid (task M6-01). Each is zero
// apart from rounding.
struct CGridIdentityChecks {
    // max_i |Σ_v R_iv − A_i| / A_i: the kites tile every cell.
    double kite_cell_area = 0.0;
    // max_v |A_v − T_v| / A_v, T_v the spherical triangle of the corner's
    // three cell centres: the kites tile every dual triangle.
    double kite_triangle_area = 0.0;
    // max |W_ee' + W_e'e| over all pairs: the weights are antisymmetric.
    double weight_antisymmetry = 0.0;
    // |Σ_e l_e d_e u_e u⊥_e| / Σ_e |l_e d_e u_e u⊥_e| for arbitrary u: the
    // Coriolis term does no work.
    double coriolis_work = 0.0;
    // max_v |ζ(∇φ)_v| / max_v (Σ_e |d_e ∂_n φ_e| / A_v): curl of a gradient.
    double curl_of_gradient = 0.0;
    // max_v |ζ(u⊥)_v + Σ_i R_iv div(u)_i / A_v| over its scale, arbitrary u,
    // with div taken over the kite sum Σ_v R_iv (the area the TRiSK weights
    // use): the steady-geostrophic-mode condition (Thuburn et al., 2009).
    double perp_vorticity = 0.0;
};

// ADR-0011 V2: accuracy of the C-grid operators on u = grad(g) + r x grad(h)
// with velocities at the Voronoi edge midpoints. Edges are weighted by
// l_e d_e / 2 and corners by A_v; an edge or corner touching a pentagon
// counts as a pentagon.
struct CGridValidation {
    std::size_t edge_count = 0;
    std::size_t corner_count = 0;
    CGridIdentityChecks identities;
    OperatorErrorNorms tangential_velocity;   // u⊥ against u · t_e
    OperatorErrorNorms vorticity;             // ζ against lap(h) at corners
    OperatorErrorNorms kinetic_energy;        // K against |u|² / 2 at cells
    OperatorErrorNorms reconstruction;        // |U − u| at cells (vector)
    OperatorErrorNorms normal_gradient;       // against grad(g) · n_e
    OperatorErrorNorms tangential_gradient;   // of g interpolated to corners (barycentric)
};

[[nodiscard]] CGridValidation validate_c_grid_operators(const PlanetMesh& mesh,
                                                        std::size_t worker_count = 1U);

}  // namespace planetsim
