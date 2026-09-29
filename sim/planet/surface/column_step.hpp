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
};

// Throws std::invalid_argument for a non-positive or non-finite step,
// non-positive temperatures, negative insolation or g outside [0, 1).
[[nodiscard]] ColumnStepResult step_column(const ColumnProperties& column, ColumnState state,
                                           double insolation_W_m2, double grey_emissivity,
                                           double dt_s);

// The closed-form equilibrium under constant insolation, both layers equal:
// T = [(1 − α) Q / (β ε σ)]^¼.
[[nodiscard]] double column_equilibrium_temperature_K(const ColumnProperties& column,
                                                      double insolation_W_m2,
                                                      double grey_emissivity);

}  // namespace planetsim
