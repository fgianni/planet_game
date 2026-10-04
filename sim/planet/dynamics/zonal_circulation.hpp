#pragma once

#include "sim/core/math/banded_lu.hpp"
#include "sim/planet/dynamics/primitive_equations.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace planetsim {

// The climate mode's zonal-mean circulation (ADR-0011 §14, task M6-04
// step B): the steady axisymmetric primitive equations on latitude bands of
// equal width and N equal-mass σ layers, with the vertical discretisation
// of the reference-mode core (layer-mean Exner π̄, geopotential Φ̄, the
// logarithmic edge θ̂). Layer 0 is the bottom one; bands run from the south.
//
// Unknowns, band by band: ū_k, θ̄_k and the column eddy kinetic energy E at
// band j, then v̄_k and the barotropic pressure gradient P at the boundary
// j + ½ north of it. A rigid lid, Σ_k v̄_k = 0 at every boundary, removes
// the external gravity wave; P is its Lagrange multiplier. The equations:
//  - absolute angular momentum M = (Ω a cos φ + ū) a cos φ in flux form,
//    upwind with limited second-order face values (first-order upwind's
//    diffusion of the planetary M is a spurious torque), with stress-form viscosity ν cos²φ ∂_φ(ū / cos φ) and the eddy
//    flux [u'v'] a cos φ;
//  - the meridional gradient-wind balance with friction;
//  - θ̄ in upwind flux form with the eddy heat flux −D_k ∂θ̄_k/∂y and the
//    linearised heating Q_k = Q⁰_k + Λ_k (T_k − T⁰_k);
//  - (1 − L² ∇²) E = c_E χ |∂θ̄/∂y|², χ = L² f² / (L² f² + N_B² H²);
//  - D_k = ℓ_h √(2E) s_k, s_k = σ_k² / mean(σ²); [u'v'] = ℓ_m ∂E/∂y in the
//    layers with σ < σ_free.
// ∂θ̄/∂y, in the closure and the numerical diffusion of θ̄, is taken on
// pressure surfaces, so that sloping σ surfaces over terrain create no
// eddies and no flux.
// The steady state is found by Newton's method with a backtracking line
// search, falling back to pseudo-transient continuation with Newton
// polishing, on an exact banded Jacobian
// (forward-mode differentiation, columns coloured five blocks apart) with
// a banded LU. Sequential and deterministic.
struct ZonalCirculationParameters {
    std::size_t bands = 36;
    std::size_t layer_count = 3;
    double radius_m = 6'371'000.0;
    double gravity_m_s2 = 9.80616;
    double rotation_rate_rad_s = 7.292e-5;
    double gas_constant_J_kg_K = 287.04;
    double heat_capacity_J_kg_K = 1004.64;
    double reference_pressure_Pa = 100'000.0;
    // Numerical viscosity of ū, v̄ and θ̄: the smallest that converges.
    double viscosity_m2_s = 5.0e4;
    // Vertical momentum diffusion K_v between adjacent layers. Without it,
    // any solid-body wind aloft is steady where the circulation is weak, a
    // null space of the steady problem; Δz²/K_v is about 140 days for three
    // layers.
    double vertical_viscosity_m2_s = 1.0;

    // The eddy closure (§14). c_E is the fitted constant; the lengths are
    // measured from reference mode.
    bool eddies = true;
    // c_E fitted in the model's form to the Held–Suarez N = 3 reference
    // (ADR-0011 §15); the planets set their own.
    double eddy_generation_m4_s2_K2 = 2.91e12;  // c_E
    double eddy_scale_m = 2.0e6;                // L (§15)
    double heat_mixing_length_m = 1.2e5;        // ℓ_h
    double momentum_mixing_length_m = 2.0e5;    // ℓ_m
    double free_troposphere_sigma = 0.7;        // σ_free
    // What keeps the residual differentiable: the eddy and gust speed
    // √(E + √(E² + w⁴)) (√(2E) once E ≫ w²), the floor of N_B² in χ, and
    // the upwind fluxes' |F| smoothed to F² / √(F² + (μ v_ε)²).
    double minimum_eddy_velocity_m_s = 0.01;
    double upwind_smoothing_m_s = 0.01;
    // The meridional fluxes of M and θ̄ take limited (van Albada) MUSCL
    // face values; these set the limiter's smoothness scale.
    double limiter_angular_momentum_m2_s = 6.4e5;   // a × 0.1 m/s
    double limiter_temperature_K = 0.01;
    double minimum_buoyancy_frequency_sq_s2 = 1.0e-6;

    // Convective relaxation (§14): where the lapse rate exceeds Γ_c between
    // adjacent layers, enthalpy moves up towards neutral within τ_c. Γ_c = 0
    // turns it off (Held–Suarez); the Earth-like planet takes ADR-0010's.
    double critical_lapse_rate_K_m = 0.0;
    double convective_time_s = 3.0 * 3'600.0;
    double convective_smoothing_K = 0.01;

    // The initial guess's thermal wind uses f / (f² + f_*²) for 1 / f,
    // f_* = 2Ω sin(regularisation latitude).
    double initial_guess_latitude_deg = 10.0;

    // Convergence: the scaled residual (wind and temperature tendencies in
    // m/s and K per day, E in m²/s², the lid in m/s), as a maximum norm.
    double tolerance = 1.0e-8;
    // Newton: iterations per attempt; its line search halves the step at
    // most this often (a step must lower the scaled RMS residual, Armijo
    // with c = 1e-4).
    std::size_t max_newton_iterations = 12;
    std::size_t max_step_halvings = 4;
    // Continuation: its iteration cap, and a Newton attempt from a copy of
    // the iterate after every polish_interval accepted steps.
    std::size_t max_continuation_iterations = 400;
    std::size_t polish_interval = 10;
    // Continuation: the first pseudo-step, its growth cap per iteration,
    // and the RMS growth beyond which a step is rejected (and the
    // pseudo-step cut fourfold).
    double initial_pseudo_step_s = 3'600.0;
    double pseudo_step_growth = 2.0;
    double continuation_rejection = 1.5;
};

// Per band, or layer × band, from the slow state.
struct ZonalForcing {
    std::vector<double> surface_pressure_Pa;        // band means of p_s
    std::vector<double> surface_height_m;           // band means of the dynamics' z_s
    std::vector<double> temperature_K;              // T⁰: the linearisation point and initial guess
    std::vector<double> heating_K_s;                // Q⁰ at T⁰
    std::vector<double> heating_derivative_s;       // Λ ≤ 0
    std::vector<double> rayleigh_friction_s;        // k_v on ū and v̄ (Held–Suarez), ≥ 0
    std::vector<double> drag_coefficient;           // C_D of the bottom layer, per band, ≥ 0
};

// newton: Newton from the initial state converged; continuation: it did
// not, and Newton converged from a continuation iterate.
enum class ZonalSolutionMethod { newton, continuation };

struct ZonalSolutionStatistics {
    std::size_t jacobians = 0;
    std::size_t newton_steps = 0;
    std::size_t continuation_steps = 0;
    std::size_t rejected_steps = 0;
};

struct ZonalCirculationSolution {
    std::size_t bands = 0;
    std::size_t layers = 0;
    std::vector<double> latitude_deg;            // band centres
    std::vector<double> boundary_latitude_deg;   // the bands − 1 interior boundaries
    // layer × band:
    std::vector<double> eastward_m_s;
    std::vector<double> temperature_K;
    // layer × boundary:
    std::vector<double> northward_m_s;
    std::vector<double> heat_diffusivity_m2_s;   // D_k
    // (layers + 1) × boundary: ψ_m = 2π a cos φ Σ_{k<m} μ̂ v̄_k (kg/s).
    std::vector<double> streamfunction_kg_s;
    std::vector<double> eddy_kinetic_m2_s2;          // E, per band
    std::vector<double> eddy_momentum_flux_m2_s2;    // column [u'v'], per boundary
    std::vector<double> barotropic_gradient_m_s2;    // P, per boundary
    // The zonal-mean p_s that balances P, ∂ ln p_s / ∂y = P / (R T̄_col),
    // with the bands' total mass held.
    std::vector<double> surface_pressure_Pa;
    // Surface torque per band (N m): friction and drag on the column; the
    // eddies and the flux-form transport exert none (V9).
    std::vector<double> surface_torque_N_m;
    double total_torque_N_m = 0.0;
    double gross_torque_N_m = 0.0;   // Σ |band torque|

    ZonalSolutionMethod method = ZonalSolutionMethod::newton;
    ZonalSolutionStatistics statistics;
    // The scaled residual at each Newton and continuation iterate, in order.
    std::vector<double> residual_history;
    std::vector<double> state;   // the unknowns, in ZonalCirculation's order
    double residual = 0.0;

    // A layer × band field's value.
    [[nodiscard]] double at_band(const std::vector<double>& field, std::size_t layer,
                                 std::size_t band) const {
        return field[layer * bands + band];
    }
    // A layer × boundary field's value.
    [[nodiscard]] double at_boundary(const std::vector<double>& field, std::size_t layer,
                                     std::size_t boundary) const {
        return field[layer * (bands - 1U) + boundary];
    }
};

class ZonalCirculation {
  public:
    explicit ZonalCirculation(ZonalCirculationParameters parameters);

    [[nodiscard]] const ZonalCirculationParameters& parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] std::size_t unknown_count() const noexcept;
    // Unknowns per band block: 3N + 2 (the last band has no boundary, 2N + 1).
    [[nodiscard]] std::size_t block_size() const noexcept {
        return 3U * parameters_.layer_count + 2U;
    }

    // Solves for the steady state; throws std::runtime_error if it is not
    // reached within the iteration caps (no silent failure, §14).
    [[nodiscard]] ZonalCirculationSolution solve(const ZonalForcing& forcing) const;

    // Pieces of the solve, for tests and diagnostics. States are unknown
    // vectors in the order above.
    [[nodiscard]] std::vector<double> initial_state(const ZonalForcing& forcing) const;
    [[nodiscard]] std::vector<double> residual(const ZonalForcing& forcing,
                                               std::span<const double> state) const;
    [[nodiscard]] BandedMatrix jacobian(const ZonalForcing& forcing,
                                        std::span<const double> state) const;
    // The residual in m/s and K per day, m²/s² and m/s: its maximum and RMS.
    [[nodiscard]] double scaled_norm(std::span<const double> residual) const;
    [[nodiscard]] double scaled_rms(std::span<const double> residual) const;

  private:
    void check(const ZonalForcing& forcing) const;
    [[nodiscard]] ZonalCirculationSolution solution(const ZonalForcing& forcing,
                                                    std::span<const double> state) const;

    ZonalCirculationParameters parameters_;
};

// Held and Suarez's (1994) forcing on the bands, as the reference core
// applies it (ADR-0011 V6): uniform p_s = p₀, no orography, Q = −k_T (T −
// T_eq) linearised about T⁰ = T_eq, and Rayleigh friction in σ > σ_b.
[[nodiscard]] ZonalForcing held_suarez_zonal_forcing(const ZonalCirculationParameters& parameters,
                                                     const HeldSuarezForcing& forcing);

}  // namespace planetsim
