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
    // dT_s'/d(source): 1 / (a + 4 r T_s'³ + λ q_sat'), or 0 where a phase
    // change holds the surface at its melting point (ADR-0008 §4.3).
    double surface_slope_K_m2_W = 0.0;
    // The latent heat of evaporation (ADR-0021 §4.3), λ (q_sat(T_s') − q₀):
    // W/m², negative for dew.
    double evaporation_W_m2 = 0.0;
};

// A tile's evaporation forcing for one step (ADR-0021 §4.3): the bulk
// transfer ρ C_E V_e (kg/m²/s per unit of humidity; 0: none), the air's
// humidity q₀ and the surface pressure, and for land the bucket's water.
struct EvaporationForcing {
    double transfer_kg_m2_s = 0.0;
    double air_humidity = 0.0;
    double pressure_Pa = 0.0;
    double bucket_kg_m2 = 0.0;
    double vapour_kg_m2 = 0.0;   // the bottom layer's vapour, which limits dew
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
    // Evaporation (ADR-0021 §4.3), with δq = q_sat(x, p) − q₀ and the
    // transfers τ = ρ C_E V_e (kg/m²/s per unit of humidity) of three
    // sources:
    //   free water  L_v τ_w δq                       (open water, leads)
    //   ice         L_s τ_i δq                       (floes)
    //   snow        L_s κ S / (S + κ Δt)             κ = τ_s δq
    //   bucket      L_v κ min(1, W / (W_c + κ Δt))   κ = τ_b δq
    // The snow and bucket terms are their stores' backward-Euler limits: a
    // step never takes more than S or W (W_c = 0.75 W_max, Manabe's
    // threshold). Dew (δq < 0) is limited by the air's vapour V the same
    // way, κ V / (V − κ Δt), so a step never condenses more than there is.
    // All zero: no evaporation, and the system is exactly ADR-0007's.
    double water_transfer = 0.0;       // τ_w (L_v)
    double ice_transfer = 0.0;         // τ_i (L_s)
    double snow_transfer = 0.0;        // τ_s (L_s)
    double bucket_transfer = 0.0;      // τ_b (L_v)
    double snow_kg_m2 = 0.0;           // S available
    double bucket_kg_m2 = 0.0;         // W available
    double bucket_threshold_kg_m2 = 0.0;   // W_c
    double step_s = 0.0;               // Δt of the limits
    double vapour_kg_m2 = 0.0;         // V: the air's vapour above the tile
    double air_humidity = 0.0;         // q₀, kg/kg
    double pressure_Pa = 0.0;          // p, for q_sat

    [[nodiscard]] bool evaporates() const noexcept {
        return water_transfer != 0.0 || ice_transfer != 0.0 || snow_transfer != 0.0 ||
               bucket_transfer != 0.0;
    }
    // The four sources' water fluxes at x (kg/m²/s; negative: dew).
    struct Evaporation {
        double water = 0.0;
        double ice = 0.0;
        double snow = 0.0;
        double bucket = 0.0;
    };
    [[nodiscard]] Evaporation evaporation_kg_m2_s(double surface_K) const noexcept;
    // Their latent heat at x, W/m², and its derivative in x.
    [[nodiscard]] double latent_flux_W_m2(double surface_K) const noexcept;
    [[nodiscard]] double latent_slope_W_m2_K(double surface_K) const noexcept;

    // The lower layer after the step, given the new surface temperature.
    [[nodiscard]] double lower_K(double lower_K_before, double surface_K) const noexcept {
        return (lower_rate * lower_K_before + exchange * surface_K) / (lower_rate + exchange);
    }
    // b − a·x − r·x⁴ − λ (q_sat(x) − q₀): the power the surface cannot
    // absorb at temperature x.
    [[nodiscard]] double surplus_W_m2(double surface_K) const noexcept;
    // 1 / (a + 4 r x³ + λ q_sat'(x)): how the root moves with b.
    [[nodiscard]] double slope_K_m2_W(double surface_K) const noexcept;
};

// Validates as step_column does and builds the system.
[[nodiscard]] ColumnSystem column_system(const ColumnProperties& column, ColumnState state,
                                         double insolation_W_m2, double grey_emissivity,
                                         double dt_s);

// Newton root of a·x + r·x⁴ + λ (q_sat(x) − q₀) = b − sink_W_m2, with
// b − sink > 0. Every term increases with x and is convex, so after at
// most one step the iterates fall monotonically onto the root. Without
// evaporation, a constant sink (latent heat of melting) keeps the start at
// or above the root, as before.
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
