#pragma once

#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace planetsim {

// One cell's atmospheric column for one step (ADR-0010 §4.4): N equal-mass
// layers exchanging grey longwave with each other, space and the surface,
// and sensible heat with the surface through the bottom layer. The surface
// is seen only through its linearised response, so the same solver serves
// the planet's tiles and the analytic surfaces of the tests.

inline constexpr double column_stefan_boltzmann_W_m2_K4 = 5.670374419e-8;
inline constexpr std::size_t max_column_layers = max_atmosphere_layer_count;
using LayerArray = std::array<double, max_column_layers>;

// The column's constants for one step.
struct ColumnRadiation {
    std::size_t layers = 0;
    double layer_heat_capacity_J_m2_K = 0.0;   // c_p p_s / (g N)
    LayerArray emissivity{};                   // ε_k = 1 − exp(−Δτ_k)
    LayerArray exner{};                        // π_k = (σ_k p_s / p₀)^κ_c: T_k = θ_k π_k
    double air_factor = 1.0;                   // T_air = air_factor · T_0 = T_0 / σ_0^κ_c
};

// τ(p) = τ₀ [f_l p/p₀ + (1 − f_l) (p/p₀)⁴] (ADR-0010 §3.2 B).
[[nodiscard]] inline double longwave_optical_depth(const AtmosphereParameters& parameters,
                                                   double pressure_Pa) noexcept {
    const double x = pressure_Pa / parameters.reference_pressure_Pa;
    const double x2 = x * x;
    return parameters.longwave_optical_depth *
           (parameters.linear_optical_depth_fraction * x +
            (1.0 - parameters.linear_optical_depth_fraction) * x2 * x2);
}

[[nodiscard]] inline ColumnRadiation column_radiation(const AtmosphereParameters& parameters,
                                                      double surface_pressure_Pa,
                                                      double gravity_m_s2) {
    ColumnRadiation column;
    column.layers = parameters.layer_count;
    if (column.layers == 0U) {
        return column;
    }
    if (!(surface_pressure_Pa > 0.0) || !std::isfinite(surface_pressure_Pa)) {
        throw std::invalid_argument("an atmospheric column needs a positive surface pressure");
    }
    const double n = static_cast<double>(column.layers);
    const double kappa = critical_lapse_exponent(parameters, gravity_m_s2);
    column.layer_heat_capacity_J_m2_K =
        dry_air_heat_capacity_J_kg_K * surface_pressure_Pa / (gravity_m_s2 * n);
    for (std::size_t layer = 0; layer < column.layers; ++layer) {
        const double bottom_Pa = surface_pressure_Pa * (1.0 - static_cast<double>(layer) / n);
        const double top_Pa = surface_pressure_Pa * (1.0 - static_cast<double>(layer + 1U) / n);
        const double depth = longwave_optical_depth(parameters, bottom_Pa) -
                             longwave_optical_depth(parameters, top_Pa);
        column.emissivity[layer] = -std::expm1(-depth);
        column.exner[layer] = std::pow(
            layer_sigma(layer, column.layers) * surface_pressure_Pa /
                parameters.reference_pressure_Pa,
            kappa);
    }
    column.air_factor = std::pow(1.0 / layer_sigma(0U, column.layers), kappa);
    return column;
}

// With the water cycle (ADR-0021 §4.6): each layer's depth is the dry part
// τ_d Δp / p₀ and the vapour's κ_v q_k Δp / g, from its humidity at the
// start of the step (the column solve holds it fixed).
[[nodiscard]] inline ColumnRadiation column_radiation(const AtmosphereParameters& parameters,
                                                      double surface_pressure_Pa,
                                                      double gravity_m_s2,
                                                      std::span<const double> humidity_kg_kg) {
    ColumnRadiation column = column_radiation(parameters, surface_pressure_Pa, gravity_m_s2);
    if (column.layers == 0U) {
        return column;
    }
    if (humidity_kg_kg.size() != column.layers) {
        throw std::invalid_argument("a column's humidity needs one value per layer");
    }
    const double layer_Pa = surface_pressure_Pa / static_cast<double>(column.layers);
    const double dry = parameters.dry_optical_depth * layer_Pa / parameters.reference_pressure_Pa;
    for (std::size_t layer = 0; layer < column.layers; ++layer) {
        const double vapour_kg_m2 = std::max(0.0, humidity_kg_kg[layer]) * layer_Pa / gravity_m_s2;
        column.emissivity[layer] =
            -std::expm1(-(dry + parameters.vapour_absorption_m2_kg * vapour_kg_m2));
    }
    return column;
}

// The longwave of a column with layer emissions e_k = ε_k σ T_k⁴ over a
// surface sending `upward_surface` into its base: what each layer absorbs
// from the two streams, the downward flux at the surface and the outgoing
// longwave. Linear in (e, upward_surface).
struct LongwaveFluxes {
    LayerArray absorbed_in{};       // ε_k (U_k + D_{k+1})
    double downward_surface_W_m2 = 0.0;
    double outgoing_W_m2 = 0.0;
};

[[nodiscard]] inline double downward_surface_longwave(const ColumnRadiation& column,
                                                      const LayerArray& emission) noexcept {
    double down = 0.0;
    for (std::size_t layer = column.layers; layer-- > 0U;) {
        down = (1.0 - column.emissivity[layer]) * down + emission[layer];
    }
    return down;
}

[[nodiscard]] inline LongwaveFluxes longwave_fluxes(const ColumnRadiation& column,
                                                    const LayerArray& emission,
                                                    double upward_surface_W_m2) noexcept {
    LongwaveFluxes fluxes;
    LayerArray from_above{};
    double down = 0.0;
    for (std::size_t layer = column.layers; layer-- > 0U;) {
        from_above[layer] = down;
        down = (1.0 - column.emissivity[layer]) * down + emission[layer];
    }
    fluxes.downward_surface_W_m2 = down;
    double up = upward_surface_W_m2;
    for (std::size_t layer = 0; layer < column.layers; ++layer) {
        fluxes.absorbed_in[layer] = column.emissivity[layer] * (up + from_above[layer]);
        up = (1.0 - column.emissivity[layer]) * up + emission[layer];
    }
    fluxes.outgoing_W_m2 = up;
    return fluxes;
}

// The surface below a column, linearised: given the downward longwave D at
// the surface and the surface air temperature A, the longwave it sends up
// (emission plus reflection) and the sensible heat it gives the bottom
// layer, with their derivatives.
struct SurfaceExchange {
    double upward_W_m2 = 0.0;
    double sensible_W_m2 = 0.0;
    double d_upward_d_downward = 0.0;
    double d_upward_d_air = 0.0;
    double d_sensible_d_downward = 0.0;
    double d_sensible_d_air = 0.0;
    // The evaporation into the bottom layer (kg/m²/s, ADR-0021 §4.4), zero
    // for a dry surface.
    double vapour_kg_m2_s = 0.0;
    double d_vapour_d_downward = 0.0;
    double d_vapour_d_air = 0.0;
};

// The column's water for one step (ADR-0021 §4.4): each layer's humidity q*_k
// after the step's transport, the bottom layer before the step's
// evaporation (the surface's, added inside the solve), the layers'
// pressures σ_k p_s, their mass and the latent heat each kilogram of
// condensate releases (L_v, plus L_f for the share that falls as snow).
struct ColumnMoisture {
    LayerArray humidity_kg_kg{};
    LayerArray pressure_Pa{};
    double layer_mass_kg_m2 = 0.0;
    double latent_J_kg = 0.0;
};

struct ColumnSolveSettings {
    int max_iterations = 40;
    int max_halvings = 12;
    int max_non_monotone_steps = 4;
    // max |F_k| at which the step is converged; the rounding floor of the
    // storage term C T / Δt is added (ADR-0007 V2 needs about 1e-7 W/m²).
    double tolerance_W_m2 = 1e-10;
};

struct ColumnSolveResult {
    double outgoing_W_m2 = 0.0;
    double downward_surface_W_m2 = 0.0;
    double upward_surface_W_m2 = 0.0;
    double air_K = 0.0;
    double sensible_W_m2 = 0.0;
    // Σ_k F_k and max |F_k| at the last evaluation (W/m²). An unconverged
    // column's layers are finished from that evaluation's fluxes,
    // T_k = T*_k − F_k Δt / C, so the energy budget closes whatever the
    // residual; `max_correction_K` is the largest such change (non-zero only
    // where the surface's response jumps, as when sea ice disappears).
    double residual_W_m2 = 0.0;
    double max_residual_W_m2 = 0.0;
    double max_correction_K = 0.0;
    int iterations = 0;
    bool converged = false;
    bool adjusted = false;          // convective adjustment changed the column
    double theta_K = 0.0;           // mean θ_c = (1/N) Σ T_k / π_k after the step
    double theta_slope_K_m2_W = 0.0;   // dθ_c/dh of the implicit step
    // dT_k/dh of the implicit step, through the convective pools: the
    // layers' response that the coupled transport needs (ADR-0011 §17.1).
    LayerArray temperature_slope_K_m2_W{};
    // With moisture (ADR-0021 §4.4), at the last evaluation: each layer's
    // humidity after evaporation and condensation, the condensate C_k
    // (kg/kg), the precipitation Σ C_k m / Δt and its latent heat
    // L Σ C_k m / Δt, and the surface's evaporation.
    LayerArray humidity_kg_kg{};
    LayerArray condensate_kg_kg{};
    double precipitation_kg_m2_s = 0.0;
    double condensation_W_m2 = 0.0;
    double evaporation_kg_m2_s = 0.0;
};

// Convective adjustment (ADR-0010 §3.3 B): adjacent layers whose θ_c
// decreases upward are mixed to a common θ_c, conserving Σ T_k (the
// enthalpy of equal-mass layers), until θ_c is non-decreasing upward
// (pool-adjacent-violators with weights π_k). Returns whether it changed
// anything. Layers that are already stable are untouched. `pools`, if
// given, receives the mixed pools: for a fixed set of pools the adjustment
// is linear, δT_k = π_k Σ_pool δT / Σ_pool π.
struct ConvectivePools {
    std::array<std::size_t, max_column_layers + 1U> start{};   // start[count] = N
    std::size_t count = 0;
};

inline bool convective_adjustment(const ColumnRadiation& column, std::span<double> temperature_K,
                                  ConvectivePools* pools_out = nullptr) {
    const std::size_t layers = column.layers;
    std::array<std::size_t, max_column_layers + 1U> start{};
    LayerArray pool_T{};    // Σ T of each pool
    LayerArray pool_pi{};   // Σ π of each pool
    std::size_t pools = 0;
    bool adjusted = false;
    for (std::size_t layer = 0; layer < layers; ++layer) {
        start[pools] = layer;
        pool_T[pools] = temperature_K[layer];
        pool_pi[pools] = column.exner[layer];
        ++pools;
        // θ of the lower pool above θ of the upper: unstable, merge. A
        // mixed pool is neutral only to rounding, so differences of a few
        // ulps count as stable (the adjustment is then idempotent).
        constexpr double neutral = 1.0 + 16.0 * std::numeric_limits<double>::epsilon();
        while (pools > 1U && pool_T[pools - 2U] * pool_pi[pools - 1U] >
                                 neutral * pool_T[pools - 1U] * pool_pi[pools - 2U]) {
            pool_T[pools - 2U] += pool_T[pools - 1U];
            pool_pi[pools - 2U] += pool_pi[pools - 1U];
            --pools;
            adjusted = true;
        }
    }
    start[pools] = layers;
    if (pools_out != nullptr) {
        pools_out->start = start;
        pools_out->count = pools;
    }
    if (!adjusted) {
        return false;
    }
    for (std::size_t pool = 0; pool < pools; ++pool) {
        if (start[pool + 1U] - start[pool] < 2U) {
            continue;
        }
        const double theta = pool_T[pool] / pool_pi[pool];
        for (std::size_t layer = start[pool]; layer < start[pool + 1U]; ++layer) {
            temperature_K[layer] = theta * column.exner[layer];
        }
    }
    return true;
}

namespace detail {

// Solves the dense system A x = b (n ≤ max_column_layers) by Gaussian
// elimination with partial pivoting; A and b are overwritten.
inline bool solve_dense(std::array<LayerArray, max_column_layers>& a, LayerArray& b,
                        std::size_t n) noexcept {
    for (std::size_t column = 0; column < n; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1U; row < n; ++row) {
            if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
                pivot = row;
            }
        }
        if (!(std::abs(a[pivot][column]) > 0.0)) {
            return false;
        }
        std::swap(a[pivot], a[column]);
        std::swap(b[pivot], b[column]);
        for (std::size_t row = column + 1U; row < n; ++row) {
            const double factor = a[row][column] / a[column][column];
            for (std::size_t k = column; k < n; ++k) {
                a[row][k] -= factor * a[column][k];
            }
            b[row] -= factor * b[column];
        }
    }
    for (std::size_t row = n; row-- > 0U;) {
        double value = b[row];
        for (std::size_t k = row + 1U; k < n; ++k) {
            value -= a[row][k] * b[k];
        }
        b[row] = value / a[row][row];
    }
    return true;
}

}  // namespace detail

// The transport source h at which the bottom layer's equation balances with
// that layer at `bottom_K` and the others at their start-of-step values:
// h = C (T₀ − T₀⁰)/Δt − [ε₀ (U₀ + D₁) − 2 ε₀ σ T₀⁴] − H(D, A). Below the
// value for a low `bottom_K` the column's bottom layer would have to fall
// under it; the transport solve keeps its sources above (ADR-0009 §9,
// ADR-0010 §11). One evaluation of the surface.
template <typename Surface>
[[nodiscard]] double bottom_source_for(const ColumnRadiation& column,
                                       std::span<const double> before_K, double dt_s,
                                       double bottom_K, Surface&& surface) {
    const std::size_t n = column.layers;
    LayerArray emission{};
    for (std::size_t k = 0; k < n; ++k) {
        const double t = k == 0U ? bottom_K : before_K[k];
        const double t2 = t * t;
        emission[k] = column.emissivity[k] * column_stefan_boltzmann_W_m2_K4 * t2 * t2;
    }
    const double down = downward_surface_longwave(column, emission);
    const SurfaceExchange exchange = surface(down, column.air_factor * bottom_K);
    const LongwaveFluxes fluxes = longwave_fluxes(column, emission, exchange.upward_W_m2);
    return column.layer_heat_capacity_J_m2_K / dt_s * (bottom_K - before_K[0]) -
           (fluxes.absorbed_in[0] - 2.0 * emission[0]) - exchange.sensible_W_m2;
}

// What a column solve leaves for the next solve of the same column within
// one step (the transport solve re-solves every column for nearby sources):
// the implicit solution before the convective adjustment, its response to
// h, and the h it was solved for. The next solve starts from the linear
// prediction T* + (dT*/dh)(h − h_prev). Only a starting point, never carried
// across steps, so the step stays a function of the state.
struct ColumnWarmStart {
    LayerArray implicit_K{};
    LayerArray response{};
    double source_W_m2 = 0.0;
    bool valid = false;
};

// One backward-Euler step of the column with the transport source h
// (W/m²) received by the bottom layer, from which convection carries it
// upward (ADR-0010 §11):
//
//   F_k = C (T_k − T_k⁰)/Δt − [ε_k (U_k + D_{k+1}) − 2 ε_k σ T_k⁴]
//         − δ_k0 [H(D, A) + h] = 0
//
// solved by Newton over the layer temperatures with the exact Jacobian
// (the surface enters through its linearisation), then, if `convection`,
// adjusted convectively. `temperature_K` holds the first guess on entry
// (unless `warm` holds a valid start) and the result on exit.
// `surface(D, A)` returns the SurfaceExchange; its last call gives the
// fluxes the step applies. A surface whose response jumps
// (a phase boundary) may leave no exact root: the layers are then finished
// from the last evaluation's fluxes, which keeps the budget exact.
//
// With `moisture` (ADR-0021 §4.4) each layer also condenses its excess over
// saturation at its new temperature, C_k = max(0, q*_k − q_sat(T_k, p_k)),
// the bottom layer's q* including the surface's evaporation E Δt / m, and
// gains its latent heat:
//
//   F_k −= L m C_k / Δt,
//
// the switch at saturation being a kink of F as the surface's phase
// changes are. Without it the solve is the dry one bit for bit.
template <typename Surface>
ColumnSolveResult solve_atmosphere_column(const ColumnRadiation& column,
                                          std::span<const double> before_K, double dt_s,
                                          double source_W_m2, bool convection,
                                          Surface&& surface, std::span<double> temperature_K,
                                          const ColumnSolveSettings& settings = {},
                                          ColumnWarmStart* warm = nullptr,
                                          const ColumnMoisture* moisture = nullptr) {
    const std::size_t n = column.layers;
    ColumnSolveResult result;
    if (n == 0U) {
        return result;
    }
    constexpr double sigma = column_stefan_boltzmann_W_m2_K4;
    const double rate = column.layer_heat_capacity_J_m2_K / dt_s;

    struct Evaluation {
        LayerArray temperature{};
        LayerArray emission{};
        LayerArray residual{};
        LongwaveFluxes fluxes;
        SurfaceExchange exchange;
        double air_K = 0.0;
        double norm = 0.0;        // Σ F²
        double max_abs = 0.0;
        LayerArray humidity{};    // q*_k, the bottom layer with the evaporation
        LayerArray condensate{};  // C_k
    };
    // L m / Δt: the heating of a unit of condensate.
    const double condensation_rate =
        moisture != nullptr ? moisture->latent_J_kg * moisture->layer_mass_kg_m2 / dt_s : 0.0;
    const auto evaluate = [&](const LayerArray& t) {
        Evaluation e;
        e.temperature = t;
        for (std::size_t k = 0; k < n; ++k) {
            const double t2 = t[k] * t[k];
            e.emission[k] = column.emissivity[k] * sigma * t2 * t2;
        }
        const double down = downward_surface_longwave(column, e.emission);
        e.air_K = column.air_factor * t[0];
        e.exchange = surface(down, e.air_K);
        e.fluxes = longwave_fluxes(column, e.emission, e.exchange.upward_W_m2);
        if (moisture != nullptr) {
            for (std::size_t k = 0; k < n; ++k) {
                e.humidity[k] = moisture->humidity_kg_kg[k];
            }
            e.humidity[0] += e.exchange.vapour_kg_m2_s * dt_s / moisture->layer_mass_kg_m2;
            for (std::size_t k = 0; k < n; ++k) {
                e.condensate[k] = std::max(
                    0.0, e.humidity[k] -
                             saturation_specific_humidity(t[k], moisture->pressure_Pa[k]));
            }
        }
        for (std::size_t k = 0; k < n; ++k) {
            double f = rate * (t[k] - before_K[k]) -
                       (e.fluxes.absorbed_in[k] - 2.0 * e.emission[k]) -
                       (k == 0U ? source_W_m2 : 0.0);
            if (k == 0U) {
                f -= e.exchange.sensible_W_m2;
            }
            if (moisture != nullptr) {
                f -= condensation_rate * e.condensate[k];
            }
            e.residual[k] = f;
            e.norm += f * f;
            e.max_abs = std::max(e.max_abs, std::abs(f));
        }
        return e;
    };
    // dF/dT at an evaluation.
    const auto jacobian = [&](const Evaluation& e) {
        std::array<LayerArray, max_column_layers> j{};
        for (std::size_t c = 0; c < n; ++c) {
            LayerArray d_emission{};
            d_emission[c] = 4.0 * e.emission[c] / e.temperature[c];
            const double d_down = downward_surface_longwave(column, d_emission);
            const double d_air = c == 0U ? column.air_factor : 0.0;
            const double d_up = e.exchange.d_upward_d_downward * d_down +
                                e.exchange.d_upward_d_air * d_air;
            const double d_sensible = e.exchange.d_sensible_d_downward * d_down +
                                      e.exchange.d_sensible_d_air * d_air;
            const LongwaveFluxes d_fluxes = longwave_fluxes(column, d_emission, d_up);
            for (std::size_t r = 0; r < n; ++r) {
                double value = -(d_fluxes.absorbed_in[r] - 2.0 * d_emission[r]);
                if (r == c) {
                    value += rate;
                }
                if (r == 0U) {
                    value -= d_sensible;
                }
                if (moisture != nullptr && e.condensate[r] > 0.0) {
                    // dC_r/dT_c: the bottom layer's evaporation, and the
                    // layer's own saturation.
                    double d_condensate = 0.0;
                    if (r == 0U) {
                        d_condensate += (e.exchange.d_vapour_d_downward * d_down +
                                         e.exchange.d_vapour_d_air * d_air) *
                                        dt_s / moisture->layer_mass_kg_m2;
                    }
                    if (r == c) {
                        d_condensate -= saturation_specific_humidity_slope(
                            e.temperature[r], moisture->pressure_Pa[r]);
                    }
                    value -= condensation_rate * d_condensate;
                }
                j[r][c] = value;
            }
        }
        return j;
    };
    const auto tolerance = [&](const Evaluation& e) {
        double floor = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            floor = std::max(floor, 16.0 * std::numeric_limits<double>::epsilon() * rate *
                                        e.temperature[k]);
        }
        return std::max(settings.tolerance_W_m2, floor);
    };

    LayerArray start{};
    for (std::size_t k = 0; k < n; ++k) {
        if (!(temperature_K[k] > 0.0) || !std::isfinite(temperature_K[k])) {
            throw std::invalid_argument("atmosphere column temperatures must be positive");
        }
        start[k] = temperature_K[k];
    }
    if (warm != nullptr && warm->valid) {
        const double change = source_W_m2 - warm->source_W_m2;
        for (std::size_t k = 0; k < n; ++k) {
            const double predicted = warm->implicit_K[k] + warm->response[k] * change;
            start[k] = predicted > 0.5 * warm->implicit_K[k] ? predicted : warm->implicit_K[k];
        }
    }
    Evaluation current = evaluate(start);
    int non_monotone = 0;
    for (int iteration = 0; iteration < settings.max_iterations; ++iteration) {
        if (current.max_abs <= tolerance(current)) {
            result.converged = true;
            break;
        }
        auto j = jacobian(current);
        LayerArray step{};
        for (std::size_t k = 0; k < n; ++k) {
            step[k] = -current.residual[k];
        }
        if (!detail::solve_dense(j, step, n)) {
            break;
        }
        ++result.iterations;
        // Keep every layer above half its temperature.
        double scale = 1.0;
        for (std::size_t k = 0; k < n; ++k) {
            if (current.temperature[k] + scale * step[k] < 0.5 * current.temperature[k]) {
                scale = 0.5 * current.temperature[k] / -step[k];
            }
        }
        bool accepted = false;
        bool tiny = true;
        for (int halving = 0; halving <= settings.max_halvings; ++halving) {
            LayerArray trial{};
            for (std::size_t k = 0; k < n; ++k) {
                trial[k] = current.temperature[k] + scale * step[k];
                tiny = tiny && std::abs(scale * step[k]) <=
                                   4.0 * std::numeric_limits<double>::epsilon() *
                                       current.temperature[k];
            }
            Evaluation candidate = evaluate(trial);
            if (candidate.norm < current.norm) {
                current = candidate;
                accepted = true;
                break;
            }
            scale *= 0.5;
        }
        if (!accepted) {
            // Across a kink of the surface's response (a phase change) the
            // merit can rise; a bounded number of full steps cross it.
            if (tiny || non_monotone >= settings.max_non_monotone_steps) {
                // The surface's last call must be at the returned state.
                current = evaluate(current.temperature);
                break;
            }
            ++non_monotone;
            LayerArray trial{};
            for (std::size_t k = 0; k < n; ++k) {
                trial[k] = current.temperature[k] + step[k];
                trial[k] = std::max(trial[k], 0.5 * current.temperature[k]);
            }
            current = evaluate(trial);
        }
    }
    if (!result.converged && current.max_abs <= tolerance(current)) {
        result.converged = true;
    }

    // The response of the layers to h at the solution: J δT = e₀.
    LayerArray response{};
    {
        auto j = jacobian(current);
        response[0] = 1.0;
        if (!detail::solve_dense(j, response, n)) {
            response = LayerArray{};
        }
    }
    if (warm != nullptr) {
        warm->implicit_K = current.temperature;
        warm->response = response;
        warm->source_W_m2 = source_W_m2;
        warm->valid = true;
    }

    double residual_sum = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        // A converged column closes the budget within its tolerance; only an
        // unconverged one is finished from its fluxes (on a very long step
        // C/Δt is tiny and the correction would amplify rounding).
        const double correction = result.converged ? 0.0 : current.residual[k] / rate;
        temperature_K[k] = current.temperature[k] - correction;
        if (!(temperature_K[k] > 0.0)) {
            std::string state;
            for (std::size_t j = 0; j < n; ++j) {
                state += " T" + std::to_string(j) + "=" + std::to_string(current.temperature[j]) +
                         " F" + std::to_string(j) + "=" + std::to_string(current.residual[j]) +
                         " eps" + std::to_string(j) + "=" + std::to_string(column.emissivity[j]);
            }
            throw std::runtime_error("an atmospheric layer fell to a non-positive temperature "
                                     "finishing an unconverged column (" +
                                     std::to_string(result.iterations) + " iterations, h=" +
                                     std::to_string(source_W_m2) + " W/m²):" + state);
        }
        residual_sum += current.residual[k];
        result.max_correction_K = std::max(result.max_correction_K, std::abs(correction));
    }
    result.outgoing_W_m2 = current.fluxes.outgoing_W_m2;
    result.downward_surface_W_m2 = current.fluxes.downward_surface_W_m2;
    result.upward_surface_W_m2 = current.exchange.upward_W_m2;
    result.air_K = current.air_K;
    result.sensible_W_m2 = current.exchange.sensible_W_m2;
    result.residual_W_m2 = residual_sum;
    result.max_residual_W_m2 = current.max_abs;
    if (moisture != nullptr) {
        double condensate = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            result.humidity_kg_kg[k] = current.humidity[k] - current.condensate[k];
            result.condensate_kg_kg[k] = current.condensate[k];
            condensate += current.condensate[k];
        }
        result.precipitation_kg_m2_s = condensate * moisture->layer_mass_kg_m2 / dt_s;
        result.condensation_W_m2 = condensation_rate * condensate;
        result.evaporation_kg_m2_s = current.exchange.vapour_kg_m2_s;
    }
    if (convection) {
        // The adjustment is linear for its pools: the θ_c response sees it.
        ConvectivePools pools;
        result.adjusted = convective_adjustment(column, temperature_K, &pools);
        for (std::size_t pool = 0; pool < pools.count; ++pool) {
            double sum = 0.0;
            double weight = 0.0;
            for (std::size_t k = pools.start[pool]; k < pools.start[pool + 1U]; ++k) {
                sum += response[k];
                weight += column.exner[k];
            }
            for (std::size_t k = pools.start[pool]; k < pools.start[pool + 1U]; ++k) {
                response[k] = sum / weight * column.exner[k];
            }
        }
    }
    if (moisture != nullptr && convection) {
        // The convective adjustment moves the condensation's heat upward
        // and leaves the layers below supersaturated at their new
        // temperatures: each condenses its excess isobarically,
        // c_p (T' − T) = L (q − q_sat(T')), and the two adjustments
        // alternate until nothing more condenses (ADR-0021 V4: no layer
        // above saturation after the step).
        const double specific_heat = column.layer_heat_capacity_J_m2_K / moisture->layer_mass_kg_m2;
        const double heating = moisture->latent_J_kg / specific_heat;   // K per kg/kg
        double extra = 0.0;
        for (int round = 0; round < 64; ++round) {
            double condensed = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                const double pressure = moisture->pressure_Pa[k];
                const double q = result.humidity_kg_kg[k];
                if (!(q > saturation_specific_humidity(temperature_K[k], pressure))) {
                    continue;
                }
                // Newton on g(T') = T' − T − h (q − q_sat(T')), increasing.
                double t = temperature_K[k];
                for (int iteration = 0; iteration < 30; ++iteration) {
                    const double g = t - temperature_K[k] -
                                     heating * (q - saturation_specific_humidity(t, pressure));
                    const double next =
                        t - g / (1.0 + heating * saturation_specific_humidity_slope(t, pressure));
                    if (std::abs(next - t) <= 4.0 * std::numeric_limits<double>::epsilon() * t) {
                        t = next;
                        break;
                    }
                    t = next;
                }
                // The condensate from the temperature change, so that the
                // layer's energy is exact.
                const double condensate = (t - temperature_K[k]) / heating;
                temperature_K[k] = t;
                result.humidity_kg_kg[k] = q - condensate;
                result.condensate_kg_kg[k] += condensate;
                condensed += condensate;
            }
            extra += condensed;
            if (!(condensed > 0.0) || !convective_adjustment(column, temperature_K)) {
                break;
            }
            result.adjusted = true;
        }
        result.precipitation_kg_m2_s += extra * moisture->layer_mass_kg_m2 / dt_s;
        result.condensation_W_m2 += condensation_rate * extra;
    }
    double slope = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        slope += response[k] / column.exner[k];
    }
    result.theta_slope_K_m2_W = slope / static_cast<double>(n);
    result.temperature_slope_K_m2_W = response;
    double theta = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        theta += temperature_K[k] / column.exner[k];
    }
    result.theta_K = theta / static_cast<double>(n);
    return result;
}

}  // namespace planetsim
