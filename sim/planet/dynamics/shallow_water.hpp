#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/operators/c_grid.hpp"

#include <cstddef>

namespace planetsim {

// The TRiSK shallow-water equations on the C-grid (ADR-0011 §4.3, task
// M6-02): the horizontal core that the N-layer primitive equations stack.
//
//   ∂h/∂t   = −div(F),  F_e = ĥ_e u_e,  ĥ_e = (h_1 + h_2)/2
//   ∂u_e/∂t = (1/d_e) Σ W_ee' l_e' F_e' (q_e + q_e')/2 − ∂_n(K + g(h + b)) − ν₄ L(L(u))
//
// with q_v = (ζ_v + f_v)/h_v at the corners (h_v kite-weighted), q_e the
// mean of an edge's corners, K the TRiSK kinetic energy and L the C-grid
// vector Laplacian ∂_n δ − ∂_t ζ. The Coriolis and PV-flux term does no work,
// so energy is conserved in space (Ringler et al., 2010).

struct ShallowWaterParameters {
    double gravity_m_s2 = 9.80616;
    double rotation_rate_rad_s = 7.292e-5;
    Vec3d rotation_axis{0.0, 0.0, 1.0};   // f = 2Ω (ω̂ · x)
    double hyperviscosity_m4_s = 0.0;     // ν₄ ≥ 0
};

struct ShallowWaterState {
    Field2D<double> thickness_m;              // h, at cells
    EdgeField<double> normal_velocity_m_s;    // u, at edges, along n_e
};

struct ShallowWaterDiagnostics {
    double mass_m3 = 0.0;                 // Σ A h
    double kinetic_energy_J = 0.0;        // Σ A h K (per unit density)
    double potential_energy_J = 0.0;      // Σ A g h (h/2 + b) (per unit density)
    double potential_enstrophy = 0.0;     // Σ A_v h_v q_v² / 2
    double max_wind_m_s = 0.0;            // max |u_e|

    [[nodiscard]] double energy_J() const noexcept {
        return kinetic_energy_J + potential_energy_J;
    }
};

// The sub-step rule (ADR-0011 §4.3): a span is cut into ⌈span / Δt_max⌉
// equal steps, Δt_max = courant · d_min / (wave_speed + max_wind), d_min the
// mesh's smallest centre distance. A function of the mesh and the stated
// bounds only.
struct SubstepRule {
    double wave_speed_m_s = 350.0;
    double max_wind_m_s = 100.0;
    double courant = 0.5;
};

[[nodiscard]] std::size_t substep_count(const PlanetMesh& mesh, double span_s,
                                        const SubstepRule& rule);

// ν₄ = d̄⁴ / (30² τ), d̄ the mean centre distance: the fastest mode of the
// C-grid vector Laplacian L, a rotational mode on the dual triangles with
// eigenvalue −27.2/d̄² at L4 and −28.9/d̄² at L5 (measured, task M6-02),
// decays in no less than τ: in 1.07 τ at L5 and 1.22 τ at L4.
[[nodiscard]] double hyperviscosity_for_damping_time(const PlanetMesh& mesh,
                                                     double damping_time_s);

class ShallowWaterModel {
  public:
    // `bottom_height_m` is b at the cells. The mesh and grid must outlive
    // the model.
    ShallowWaterModel(const PlanetMesh& mesh, const CGridGeometry& grid,
                      ShallowWaterParameters parameters, Field2D<double> bottom_height_m);

    [[nodiscard]] const ShallowWaterParameters& parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] const Field2D<double>& corner_coriolis() const noexcept { return f_corner_; }

    // The tendencies (∂h/∂t, ∂u/∂t) of `state`.
    void tendency(const ShallowWaterState& state, ShallowWaterState& rate,
                  std::size_t worker_count = 1U) const;

    // One RK3 step of length dt.
    void step(ShallowWaterState& state, double dt_s, std::size_t worker_count = 1U) const;

    // Advances `span_s` in substep_count(span, rule) equal RK3 steps. Throws
    // std::runtime_error if the wind exceeds rule.max_wind_m_s after a step.
    // Returns the number of steps.
    std::size_t advance(ShallowWaterState& state, double span_s, const SubstepRule& rule,
                        std::size_t worker_count = 1U) const;

    [[nodiscard]] ShallowWaterDiagnostics diagnose(const ShallowWaterState& state,
                                                   std::size_t worker_count = 1U) const;

    // L(u)_e = ∂_n δ − ∂_t ζ: the C-grid vector Laplacian.
    void vector_laplacian(const EdgeField<double>& velocity, EdgeField<double>& result,
                          std::size_t worker_count = 1U) const;

  private:
    const PlanetMesh* mesh_;
    const CGridGeometry* grid_;
    ShallowWaterParameters parameters_;
    Field2D<double> bottom_height_m_;
    Field2D<double> f_corner_;
};

}  // namespace planetsim
