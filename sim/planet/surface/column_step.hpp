#pragma once

#include "sim/planet/surface/surface_materials.hpp"

namespace planetsim {

// One backward-Euler step of a two-layer surface column (ADR-0007 §4.3):
//
//   C_s (T_s' − T_s) = Δt [ (1 − α) Q − β ε σ T_s'⁴ − k (T_s' − T_l') ]
//   C_l (T_l' − T_l) = Δt [ k (T_s' − T_l') ]             β = 1 − g/2
//
// The lower layer is linear, so eliminating it leaves one scalar equation
// a·x + βεσ·x⁴ = b for the new surface temperature, solved by Newton from a
// start at or above the root (the iteration then decreases monotonically).
// Everything is in double; the result carries the budget terms.

inline constexpr double stefan_boltzmann_W_m2_K4 = 5.670374419e-8;
inline constexpr int column_newton_iterations = 10;

struct ColumnState {
    double surface_K = 0.0;
    double lower_K = 0.0;
};

struct ColumnStepResult {
    ColumnState state;
    double absorbed_W_m2 = 0.0;         // (1 − α) Q
    double emitted_W_m2 = 0.0;          // β ε σ T_s'⁴, to space
    double storage_change_J_m2 = 0.0;   // C_s ΔT_s + C_l ΔT_l
    double newton_residual_W_m2 = 0.0;  // of the scalar equation, after the last iteration
    // External source actually received (horizontal transport and exchange
    // with the cell's air, ADR-0009), W/m²; part of the budget.
    double source_W_m2 = 0.0;
    // dT_s'/d(source): 1 / (a + 4 r T_s'³), or 0 where a phase change holds
    // the surface at its melting point (ADR-0008 §4.3).
    double surface_slope_K_m2_W = 0.0;
};

// The step with the lower layer eliminated: a·x + r·x⁴ = b for the new
// surface temperature x, and the lower layer's update once x is known.
struct ColumnSystem {
    double a = 0.0;            // C_s/Δt + k_eff
    double b = 0.0;            // C_s/Δt · T_s + (1 − α) Q + k_eff · T_l
    double radiative = 0.0;    // r = β ε σ
    double absorbed_W_m2 = 0.0;
    double lower_rate = 0.0;   // C_l/Δt
    double exchange = 0.0;     // k

    // The lower layer after the step, given the new surface temperature.
    [[nodiscard]] double lower_K(double lower_K_before, double surface_K) const noexcept {
        return (lower_rate * lower_K_before + exchange * surface_K) / (lower_rate + exchange);
    }
    // b − a·x − r·x⁴: the power the surface cannot absorb at temperature x.
    [[nodiscard]] double surplus_W_m2(double surface_K) const noexcept;
    // 1 / (a + 4 r x³): how the root moves with b.
    [[nodiscard]] double slope_K_m2_W(double surface_K) const noexcept;
};

// Validates as step_column does and builds the system.
[[nodiscard]] ColumnSystem column_system(const ColumnProperties& column, ColumnState state,
                                         double insolation_W_m2, double grey_emissivity,
                                         double dt_s);

// Newton root of a·x + r·x⁴ = b − sink_W_m2, with b − sink > 0. A constant
// sink (for example latent heat) keeps the start at or above the root.
[[nodiscard]] double solve_column_surface(const ColumnSystem& system, double sink_W_m2 = 0.0);

// The step's budget terms once the new surface temperature is known. `sink`
// is the constant sink the surface solve used (latent heat); the source is
// already part of system.b and is recorded for the budget.
[[nodiscard]] ColumnStepResult complete_column_step(const ColumnProperties& column,
                                                    const ColumnSystem& system,
                                                    ColumnState before, double surface_K,
                                                    double sink_W_m2, double source_W_m2);

// Solves a prepared system with an external source s − γ·T_s' in the
// surface equation: `source` is added to b and `exchange` (γ, W/m²/K) to a.
// The result records the source actually received, s − γ·T_s'.
[[nodiscard]] ColumnStepResult solve_column_step(const ColumnProperties& column,
                                                 ColumnSystem system, ColumnState before,
                                                 double source_W_m2, double exchange_W_m2_K = 0.0);

// Throws std::invalid_argument for a non-positive or non-finite step,
// non-positive temperatures, negative insolation or g outside [0, 1).
[[nodiscard]] ColumnStepResult step_column(const ColumnProperties& column, ColumnState state,
                                           double insolation_W_m2, double grey_emissivity,
                                           double dt_s, double source_W_m2 = 0.0);

// The closed-form equilibrium under constant insolation, both layers equal:
// T = [(1 − α) Q / (β ε σ)]^¼.
[[nodiscard]] double column_equilibrium_temperature_K(const ColumnProperties& column,
                                                      double insolation_W_m2,
                                                      double grey_emissivity);

}  // namespace planetsim
