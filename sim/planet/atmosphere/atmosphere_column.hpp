#pragma once

#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/atmosphere/subgrid_cloud.hpp"

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
    LayerArray exner_dry{};                    // (σ_k p_s / p₀)^(R_d/c_p): the dry adiabat (ADR-0023 §4.2)
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
        column.exner_dry[layer] = std::pow(
            layer_sigma(layer, column.layers) * surface_pressure_Pa /
                parameters.reference_pressure_Pa,
            dry_air_gas_constant_J_kg_K / dry_air_heat_capacity_J_kg_K);
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
    // The share of the excess a step condenses, 1 − exp(−Δt/τ_c) (1: all).
    double condensation_fraction = 1.0;
    // Moist convection (ADR-0023 §4.2): with it the column's convection is a
    // dry adjustment to the dry adiabat and a Betts–Miller relaxation by
    // 1 − exp(−Δt/τ_bm) towards the moist adiabat at RH_bm.
    bool moist_convection = false;
    double moist_convection_fraction = 1.0;
    double moist_convection_relative_humidity = 0.8;
    // ADR-0026 option 1 (prototype, off: ADR-0023 §4.2.5): the convecting
    // column's humidity relaxes towards RH_bm q_sat(T_ref) in every layer,
    // moistening as well as drying.
    bool convective_moistening = false;
    // Cloud water (ADR-0023 §4.3, §12), with moist convection: each layer's
    // at the start of the step, the autoconversion threshold q_l0, the
    // step's shares 1 − exp(−Δt/τ_a) and 1 − exp(−Δt/τ_e), τ_a / Δt, and the
    // rain's evaporation efficiency ε.
    LayerArray cloud_water_kg_kg{};
    double cloud_water_threshold_kg_kg = 0.0;
    double autoconversion_fraction = 1.0;
    double autoconversion_time_ratio = 0.0;
    double evaporation_fraction = 1.0;
    double rain_evaporation_efficiency = 0.5;
    // Sub-grid condensation (ADR-0025 §4.1, §4.2), with moist convection: a
    // layer's deficit varies with half-width b = β q_sat, and it condenses
    // and evaporates towards the distribution's equilibrium cloud water.
    // β = 0 is saturation adjustment, ADR-0023 §11.1's.
    double subgrid_width_fraction = 0.0;
    // Non-precipitating cloud (ADR-0025 §11.2.1): autoconversion acts above
    // q_th = A W_np / m, A the layer's sub-grid cloudy area. 0: ADR-0023
    // §12's threshold alone.
    double non_precipitating_path_kg_m2 = 0.0;
    // Convective detrainment (ADR-0025 §4.3): the share e_d of the column's
    // convective condensate moved to its detrainment layers; 0: none.
    double detrainment_share = 0.0;
};

// A layer's equilibrium cloud water under the sub-grid distribution
// (ADR-0025 §4.1) at temperature T with total water q_t = q + q_l, and its
// derivative in T at fixed q_t (through q_sat in the deficit and the width).
struct LayerEquilibrium {
    SubgridCloud cloud;
    double d_water_d_temperature = 0.0;
};

[[nodiscard]] inline LayerEquilibrium layer_equilibrium(const ColumnMoisture& moisture,
                                                        std::size_t layer, double total_kg_kg,
                                                        double temperature_K) noexcept {
    const double pressure = moisture.pressure_Pa[layer];
    const double saturated = saturation_specific_humidity(temperature_K, pressure);
    const double slope = saturation_specific_humidity_slope(temperature_K, pressure);
    LayerEquilibrium result;
    result.cloud = subgrid_cloud(total_kg_kg - saturated,
                                 moisture.subgrid_width_fraction * saturated);
    result.d_water_d_temperature = -result.cloud.area * slope +
                                   result.cloud.d_water_d_width *
                                       moisture.subgrid_width_fraction * slope;
    return result;
}

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
    // With moist convection, each layer's drying by Betts–Miller (kg/kg),
    // condensate that joins the cloud water as the large-scale kind does
    // (ADR-0023 §14).
    LayerArray convective_condensate_kg_kg{};
    // ADR-0025 §4.3: each layer's cloud-water source this step after
    // detrainment (kg/kg), its convective part (Betts–Miller's drying and
    // w_k of the condensation, moved or kept), and the column's detrained
    // water (kg/m²/s).
    LayerArray cloud_source_kg_kg{};
    LayerArray convective_cloud_source_kg_kg{};
    double detrained_kg_m2_s = 0.0;
    double precipitation_kg_m2_s = 0.0;
    double condensation_W_m2 = 0.0;
    double evaporation_kg_m2_s = 0.0;
    // With moist convection (ADR-0023 §4.2): its share of the precipitation,
    // the highest convecting layer (−1: none), and the change it made to
    // the column's moist enthalpy Σ m (c_p ΔT + L Δq) (zero to rounding).
    double convective_precipitation_kg_m2_s = 0.0;
    int convection_top = -1;
    double convection_enthalpy_change_J_m2 = 0.0;
    // With moist convection, cloud water (ADR-0023 §4.3): each layer's after
    // the step, and what evaporated of cloud water and of the falling
    // precipitation. The precipitation is then what reaches the surface,
    // and the condensation's heat the net, L (P + Δ cloud water) / Δt.
    LayerArray cloud_water_kg_kg{};
    double cloud_evaporation_kg_m2_s = 0.0;
    double rain_evaporation_kg_m2_s = 0.0;
    LayerArray cloud_evaporation_by_layer_kg_kg{};   // per layer, kg/kg
    LayerArray rain_evaporation_by_layer_kg_kg{};
    // With moisture (ADR-0024 §4.1, §4.5): the moist enthalpy temperature
    // T_m = (1/N) Σ (T_k + L q_k / c_p) after the step (L the condensate's,
    // as the convection's), and dT_m/dh from the implicit solve's response
    // r, (1/N) Σ r_k (1 + f L q_sat′(T_k) / c_p where layer k condensed in
    // the solve, f its in-solve share), and the change of the surface's
    // evaporation where the bottom layer did not condense.
    // Convection, condensation and the microphysics conserve T_m, so it
    // rises with h where θ_c need not.
    double moist_enthalpy_K = 0.0;
    double moist_enthalpy_slope_K_m2_W = 0.0;
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

// The Betts–Miller step (ADR-0023 §4.2; Betts 1986, Betts and Miller 1986)
// on a column already dry-adjusted. A parcel lifted from the bottom layer
// conserves its moist static energy h = c_p T + Φ + L q, saturating where
// its humidity exceeds q_sat; Φ_k is hydrostatic in the environment,
// Φ_k − Φ_{k−1} = R_d ½ (T_{k−1} + T_k) ln(p_{k−1} / p_k). Layers 1..top where
// the parcel is warmer than its environment, contiguous from layer 1, with
// the bottom layer form the convecting column. Its reference profile is the
// parcel's temperatures and RH_bm q_sat of them, shifted by one constant s
// over the column so that Σ (c_p ΔT + L Δq) = 0; where that would moisten
// the column (the shallow case), only temperature relaxes, Σ ΔT = 0. The
// column relaxes by r towards it; the water it loses is the convective
// precipitation.
// The parcel buoyancy over which a layer joins the convecting column.
inline constexpr double betts_miller_onset_K = 1.0;

struct BettsMillerResult {
    int top = -1;                    // highest convecting layer, −1: none
    double precipitation_kg_m2 = 0.0;
    bool shallow = false;
    LayerArray parcel_K{};           // the parcel's temperatures, layers 0..top
    LayerArray weight{};             // each layer's share w_k of the relaxation
    double shift_K = 0.0;            // s: T_ref = parcel + s
    // The parcel's buoyancy in layer 1, T_parcel,1 − T_1, whether or not the
    // column convects (ADR-0026 option 3's measurements).
    double buoyancy_K = 0.0;
};

inline BettsMillerResult betts_miller(const ColumnRadiation& column, const ColumnMoisture& moisture,
                                      std::span<double> temperature_K,
                                      std::span<double> humidity_kg_kg) {
    BettsMillerResult result;
    const std::size_t n = column.layers;
    if (n < 2U) {
        return result;
    }
    constexpr double cp = dry_air_heat_capacity_J_kg_K;
    constexpr double rd = dry_air_gas_constant_J_kg_K;
    const double latent = moisture.latent_J_kg;
    // Environment geopotential, the bottom layer at Φ = 0.
    LayerArray geopotential{};
    for (std::size_t k = 1; k < n; ++k) {
        geopotential[k] = geopotential[k - 1U] +
                          rd * 0.5 * (temperature_K[k - 1U] + temperature_K[k]) *
                              std::log(moisture.pressure_Pa[k - 1U] / moisture.pressure_Pa[k]);
    }
    // The parcel's temperature in layer k from a start (T, q).
    const auto parcel = [&](double start_K, double start_q, std::size_t k) {
        const double h = cp * start_K + latent * start_q;
        const double dry = (h - latent * start_q - geopotential[k]) / cp;
        if (start_q <= saturation_specific_humidity(dry, moisture.pressure_Pa[k])) {
            return dry;
        }
        // f(T) = c_p T + L q_sat(T) − (h − Φ) increases; f(dry) < 0 and
        // f(dry + L q/c_p) > 0 bracket the root. Newton, safeguarded by
        // bisection (q_sat grows too fast for Newton alone from below).
        double low = dry;
        double high = dry + latent * start_q / cp;
        double t = dry;
        for (int iteration = 0; iteration < 100; ++iteration) {
            const double f = cp * t + latent * saturation_specific_humidity(t, moisture.pressure_Pa[k]) -
                             (h - geopotential[k]);
            (f < 0.0 ? low : high) = t;
            double next =
                t - f / (cp + latent * saturation_specific_humidity_slope(t, moisture.pressure_Pa[k]));
            if (!(next > low && next < high)) {
                next = 0.5 * (low + high);
            }
            if (std::abs(next - t) <= 1e-12 * t) {
                return next;
            }
            t = next;
        }
        return t;
    };
    // Each layer joins by its parcel buoyancy b_k = T_p,k − T_k, as
    // w_k = smoothstep(b_k / b_onset), no more than the layer below; the
    // bottom layer joins as layer 1. A step-function trigger would switch a
    // moist column's drying on at full strength, and the monthly implicit
    // transport solve then has no root (task M8-02).
    LayerArray reference{};
    LayerArray weight{};
    reference[0] = temperature_K[0];
    int top = 0;
    double below = 1.0;
    for (std::size_t k = 1; k < n; ++k) {
        reference[k] = parcel(temperature_K[0], humidity_kg_kg[0], k);
        const double x =
            std::clamp((reference[k] - temperature_K[k]) / betts_miller_onset_K, 0.0, 1.0);
        weight[k] = std::min(below, x * x * (3.0 - 2.0 * x));
        below = weight[k];
        if (!(weight[k] > 0.0)) {
            break;
        }
        top = static_cast<int>(k);
    }
    result.buoyancy_K = reference[1] - temperature_K[1];
    if (top < 1) {
        return result;
    }
    weight[0] = weight[1];
    result.top = top;
    result.parcel_K = reference;
    result.weight = weight;
    const auto layers = static_cast<std::size_t>(top) + 1U;
    const double rh = moisture.moist_convection_relative_humidity;
    // The shift s with G(s) = Σ w (c_p (T_ref + s − T) + L min(0, q_ref(s) − q))
    // = 0: the column's moist enthalpy kept, no layer moistened. G increases
    // with s; Newton, safeguarded by bisection.
    const auto enthalpy = [&](double shift, double& slope) {
        double sum = 0.0;
        slope = 0.0;
        for (std::size_t k = 0; k < layers; ++k) {
            const double t = reference[k] + shift;
            const double q_ref = rh * saturation_specific_humidity(t, moisture.pressure_Pa[k]);
            sum += weight[k] * cp * (t - temperature_K[k]);
            slope += weight[k] * cp;
            if (moisture.convective_moistening || q_ref < humidity_kg_kg[k]) {
                sum += weight[k] * latent * (q_ref - humidity_kg_kg[k]);
                slope += weight[k] * latent * rh *
                         saturation_specific_humidity_slope(t, moisture.pressure_Pa[k]);
            }
        }
        return sum;
    };
    double low = -100.0;
    double high = 100.0;
    double shift = 0.0;
    for (int iteration = 0; iteration < 80; ++iteration) {
        double slope = 0.0;
        const double g = enthalpy(shift, slope);
        if (g == 0.0) break;
        (g < 0.0 ? low : high) = shift;
        double next = shift - g / slope;
        if (!(next > low && next < high)) next = 0.5 * (low + high);
        if (std::abs(next - shift) <= 1e-13 * std::max(1.0, std::abs(shift))) {
            shift = next;
            break;
        }
        shift = next;
    }
    result.shift_K = shift;
    const double r = moisture.moist_convection_fraction;
    if (moisture.convective_moistening) {
        // ADR-0026 option 1. Deep: every layer towards q_ref, the column's
        // net loss raining. Shallow (relaxing would add water): the
        // temperatures keep Σ w ΔT = 0 and the humidity profile is scaled
        // to add none (Betts 1986's shallow case). Both keep the column's
        // moist enthalpy.
        double net = 0.0;
        double weights = 0.0;
        for (std::size_t k = 0; k < layers; ++k) {
            net += weight[k] *
                   (rh * saturation_specific_humidity(reference[k] + shift, moisture.pressure_Pa[k]) -
                    humidity_kg_kg[k]);
            weights += weight[k];
        }
        if (net <= 0.0) {
            for (std::size_t k = 0; k < layers; ++k) {
                const double t = reference[k] + shift;
                const double dq =
                    rh * saturation_specific_humidity(t, moisture.pressure_Pa[k]) - humidity_kg_kg[k];
                temperature_K[k] += r * weight[k] * (t - temperature_K[k]);
                humidity_kg_kg[k] += r * weight[k] * dq;
            }
            result.shallow = false;
            result.precipitation_kg_m2 = -r * net * moisture.layer_mass_kg_m2;
            return result;
        }
        double sum_t = 0.0;
        for (std::size_t k = 0; k < layers; ++k) sum_t += weight[k] * (reference[k] - temperature_K[k]);
        const double shallow_shift = -sum_t / weights;
        // The reference profile scaled so the column's water is unchanged
        // (a constant shift could take a cold layer's tiny q_ref below zero).
        double reference_water = 0.0;
        double water = 0.0;
        for (std::size_t k = 0; k < layers; ++k) {
            reference_water += weight[k] * rh *
                               saturation_specific_humidity(reference[k] + shallow_shift,
                                                            moisture.pressure_Pa[k]);
            water += weight[k] * humidity_kg_kg[k];
        }
        const double scale = reference_water > 0.0 ? water / reference_water : 0.0;
        for (std::size_t k = 0; k < layers; ++k) {
            const double t = reference[k] + shallow_shift;
            const double dq = scale * rh * saturation_specific_humidity(t, moisture.pressure_Pa[k]) -
                              humidity_kg_kg[k];
            temperature_K[k] += r * weight[k] * (t - temperature_K[k]);
            humidity_kg_kg[k] += r * weight[k] * dq;
        }
        result.shallow = true;
        result.precipitation_kg_m2 = 0.0;
        return result;
    }
    double drying = 0.0;
    for (std::size_t k = 0; k < layers; ++k) {
        const double t = reference[k] + shift;
        const double dq =
            std::min(0.0, rh * saturation_specific_humidity(t, moisture.pressure_Pa[k]) - humidity_kg_kg[k]);
        temperature_K[k] += r * weight[k] * (t - temperature_K[k]);
        humidity_kg_kg[k] += r * weight[k] * dq;
        drying -= r * weight[k] * dq;
    }
    // No layer dried: the shallow case, temperature alone relaxed.
    result.shallow = !(drying > 0.0);
    result.precipitation_kg_m2 = drying * moisture.layer_mass_kg_m2;
    return result;
}

// Cloud radiation (ADR-0023 §4.4 as amended by §13) from a column's cloud
// water at the start of a step, held through the step as the vapour path is:
// - fraction f_k = min(1, q_l m / W_c), W_c the in-cloud water path of a
//   layer's cloud (§13.3);
// - in-cloud optical depth τ_k = 3 LWP_k / (2 ρ_w r_e), LWP_k = q_l m / f_k,
//   which is W_c below overcast;
// - shortwave reflection R_k = f_k (1 − g) τ_k / (2 + (1 − g) τ_k), the
//   layers by random overlap, transmission Π (1 − R_k);
// - longwave depth κ_l f_k LWP_k = κ_l q_l m, added to the layer's.
// Below overcast each layer's reflection and longwave depth are
// proportional to its cloud water, so a month of mean cloud water radiates
// as the reference steps averaging to it do (§13.3).
inline constexpr double liquid_water_density_kg_m3 = 1000.0;

struct CloudOptics {
    LayerArray fraction{};
    LayerArray optical_depth{};    // in-cloud τ_k
    LayerArray reflectance{};      // R_k
    LayerArray longwave_depth{};   // κ_l q_l m
    double transmission = 1.0;     // Π (1 − R_k)
    double column_fraction = 0.0;  // 1 − Π (1 − f_k), random overlap
    double column_optical_depth = 0.0;   // Σ f_k τ_k
};

inline CloudOptics cloud_optics(const AtmosphereParameters& parameters,
                                const ColumnRadiation& column, const ColumnMoisture& moisture) {
    CloudOptics optics;
    const double one_minus_g = 1.0 - parameters.cloud_asymmetry;
    double clear = 1.0;
    for (std::size_t k = 0; k < column.layers; ++k) {
        const double path_kg_m2 =
            std::max(0.0, moisture.cloud_water_kg_kg[k]) * moisture.layer_mass_kg_m2;
        if (!(path_kg_m2 > 0.0)) {
            continue;
        }
        const double f = std::min(1.0, path_kg_m2 / parameters.cloud_in_cloud_water_path_kg_m2);
        const double tau = 3.0 * (path_kg_m2 / f) /
                           (2.0 * liquid_water_density_kg_m3 * parameters.cloud_effective_radius_m);
        optics.fraction[k] = f;
        clear *= 1.0 - f;
        optics.optical_depth[k] = tau;
        optics.reflectance[k] = f * one_minus_g * tau / (2.0 + one_minus_g * tau);
        optics.transmission *= 1.0 - optics.reflectance[k];
        optics.longwave_depth[k] = parameters.cloud_longwave_absorption_m2_kg * path_kg_m2;
        optics.column_optical_depth += f * tau;
    }
    optics.column_fraction = 1.0 - clear;
    return optics;
}

// The column's longwave with its clouds' depth added to each layer's:
// ε' = 1 − (1 − ε) exp(−Δτ_cloud).
inline void add_cloud_longwave(ColumnRadiation& column, const CloudOptics& optics) noexcept {
    for (std::size_t k = 0; k < column.layers; ++k) {
        if (optics.longwave_depth[k] > 0.0) {
            column.emissivity[k] =
                -std::expm1(std::log1p(-column.emissivity[k]) - optics.longwave_depth[k]);
        }
    }
}

// The isobaric evaporation x (kg/kg) that brings a layer at (T, q) to
// saturation, q + x = q_sat(T − h x), h = L / c_p (K per kg/kg); zero where
// it is saturated. g(x) = q + x − q_sat(T − h x) increases, with g(0) < 0 and
// g(q_sat(T) − q) ≥ 0; Newton, safeguarded by bisection (q_sat's slope jumps
// at 0 °C, where the ice branch begins), and the lower end of the bracket
// returned, so that the layer never ends supersaturated.
[[nodiscard]] inline double evaporation_to_saturation(double temperature_K, double humidity_kg_kg,
                                                      double pressure_Pa, double heating_K) {
    const auto g = [&](double x) {
        return humidity_kg_kg + x - saturation_specific_humidity(temperature_K - heating_K * x,
                                                                 pressure_Pa);
    };
    double low = 0.0;
    double high = saturation_specific_humidity(temperature_K, pressure_Pa) - humidity_kg_kg;
    if (!(high > 0.0)) {
        return 0.0;
    }
    double x = 0.0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double value = g(x);
        if (value == 0.0) {
            return x;
        }
        (value < 0.0 ? low : high) = x;
        double next = x - value / (1.0 + heating_K * saturation_specific_humidity_slope(
                                                         temperature_K - heating_K * x, pressure_Pa));
        if (!(next > low && next < high)) {
            next = 0.5 * (low + high);
        }
        if (high - low <= 1e-14 * high || next == x) {
            break;
        }
        x = next;
    }
    return low;
}

// The isobaric evaporation x of a layer's cloud water towards its sub-grid
// equilibrium (ADR-0025 §4.1): with held cloud water q_l (of which
// `available` may evaporate) and total water q_t = q + q_l fixed,
// q_l − x = q_l*(q_t, T − h x). g(x) = q_l − x − q_l*(T − h x) falls with
// x (cooling raises q_l*); zero where g(0) ≤ 0. Bisection safeguards Newton,
// and the lower end of the bracket is returned, so the layer never ends
// below its equilibrium. At β = 0 this is evaporation_to_saturation's
// problem.
[[nodiscard]] inline double evaporation_to_equilibrium(const ColumnMoisture& moisture,
                                                       std::size_t layer, double temperature_K,
                                                       double humidity_kg_kg, double held_kg_kg,
                                                       double available_kg_kg, double heating_K) {
    const double total = humidity_kg_kg + held_kg_kg;
    const auto g = [&](double x, double* slope) {
        const auto equilibrium =
            layer_equilibrium(moisture, layer, total, temperature_K - heating_K * x);
        if (slope != nullptr) {
            *slope = -1.0 + heating_K * equilibrium.d_water_d_temperature;
        }
        return held_kg_kg - x - equilibrium.cloud.water_kg_kg;
    };
    if (!(g(0.0, nullptr) > 0.0) || !(available_kg_kg > 0.0)) {
        return 0.0;
    }
    double low = 0.0;
    double high = available_kg_kg;
    if (g(high, nullptr) >= 0.0) {
        return high;
    }
    double x = 0.5 * high;
    for (int iteration = 0; iteration < 100; ++iteration) {
        double slope = -1.0;
        const double value = g(x, &slope);
        if (value == 0.0) {
            return x;
        }
        (value > 0.0 ? low : high) = x;
        double next = x - value / slope;
        if (!(next > low && next < high)) {
            next = 0.5 * (low + high);
        }
        if (high - low <= 1e-14 * available_kg_kg || next == x) {
            break;
        }
        x = next;
    }
    return low;
}

// Cloud water to precipitation (ADR-0023 §4.3, §12, §14) on equal-mass
// layers, after the column solve and convection. Cloud water in a
// subsaturated layer evaporates by 1 − exp(−Δt/τ_e) of it, never beyond
// saturation. The excess x = q_l − q_l0 then follows dx/dt = c − x/τ_a
// while positive, with the step's condensate C_k, large-scale and
// convective (§14: Betts–Miller's drying V_k), entering at the constant rate
// c = C_k/Δt, integrated exactly: x(Δt) = c τ_a + (x₀ − c τ_a) exp(−Δt/τ_a)
// (from the moment it reaches q_l0 if it starts below); what leaves is
// precipitation, convective in the share V_k / C_k (at most V_k; ADR-0025
// §4.3: V_k the layer's convective condensate after detrainment, C_k its
// whole source, `source_kg_kg`). The cloud water so
// settles at q_l0 + c τ_a, and a month in one step gives what the same month
// gives in reference steps. Precipitation falls from the top: entering a
// layer it evaporates by ε (1 − RH) (1 − exp(−Δt/τ_e)) of itself, never
// beyond saturation, and then gains the layer's autoconversion. Every
// evaporation cools its layer by L/c_p per kg/kg, so the layer's moist
// enthalpy is exact. Amounts are kg/kg of a layer; the result's in kg/m².
struct MicrophysicsResult {
    double precipitation_kg_m2 = 0.0;   // reaching the surface
    double convective_kg_m2 = 0.0;      // its convective part
    double autoconversion_kg_m2 = 0.0;
    double cloud_evaporation_kg_m2 = 0.0;
    double rain_evaporation_kg_m2 = 0.0;
    // Per layer, kg/kg of the layer (the vapour budget, task M8-07).
    LayerArray cloud_evaporation_kg_kg{};
    LayerArray rain_evaporation_kg_kg{};
};

inline MicrophysicsResult cloud_microphysics(const ColumnRadiation& column,
                                             const ColumnMoisture& moisture,
                                             std::span<const double> source_kg_kg,
                                             std::span<const double> convective_kg_kg,
                                             std::span<double> temperature_K,
                                             std::span<double> humidity_kg_kg,
                                             std::span<double> cloud_water_kg_kg) {
    MicrophysicsResult result;
    const std::size_t n = column.layers;
    const double mass = moisture.layer_mass_kg_m2;
    const double specific_heat = column.layer_heat_capacity_J_m2_K / mass;
    const double heating = moisture.latent_J_kg / specific_heat;
    LayerArray autoconversion{};
    LayerArray convective_share{};
    double cloud_evaporation = 0.0;
    double converted = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        double& cloud = cloud_water_kg_kg[k];
        if (cloud > 0.0) {
            // ADR-0025 §4.1: towards the sub-grid equilibrium of the
            // layer's total water, the step's condensate held with it; at
            // β = 0 up to saturation, as before.
            const double x =
                moisture.subgrid_width_fraction > 0.0
                    ? moisture.evaporation_fraction *
                          evaporation_to_equilibrium(
                              moisture, k, temperature_K[k], humidity_kg_kg[k],
                              cloud + source_kg_kg[k], cloud, heating)
                    : std::min(moisture.evaporation_fraction * cloud,
                               evaporation_to_saturation(temperature_K[k], humidity_kg_kg[k],
                                                         moisture.pressure_Pa[k], heating));
            temperature_K[k] -= heating * x;
            humidity_kg_kg[k] += x;
            cloud -= x;
            cloud_evaporation += x;
            result.cloud_evaporation_kg_kg[k] = x;
        }
        {
            const double source = source_kg_kg[k];
            convective_share[k] = source > 0.0 ? convective_kg_kg[k] / source : 0.0;
            // ADR-0025 §11.2.1: the layer's cloud up to A W_np / m does not
            // rain; A from its sub-grid distribution, the step's
            // condensate in its total water.
            double threshold = moisture.cloud_water_threshold_kg_kg;
            if (moisture.non_precipitating_path_kg_m2 > 0.0 &&
                moisture.subgrid_width_fraction > 0.0) {
                const double area =
                    layer_equilibrium(moisture, k, humidity_kg_kg[k] + cloud + source,
                                      temperature_K[k])
                        .cloud.area;
                threshold = std::max(threshold, area * moisture.non_precipitating_path_kg_m2 / mass);
            }
            const double equilibrium = source * moisture.autoconversion_time_ratio;   // c τ_a
            const double decay = 1.0 - moisture.autoconversion_fraction;
            const double x0 = cloud - threshold;
            double x = 0.0;
            if (x0 >= 0.0) {
                x = equilibrium + (x0 - equilibrium) * decay;
            } else if (source > -x0) {
                const double reach = -x0 / source;   // share of the step to the threshold
                const double ratio = moisture.autoconversion_time_ratio;
                const double late = ratio > 0.0 ? std::exp(-(1.0 - reach) / ratio) : 0.0;
                x = equilibrium * (1.0 - late);
            } else {
                // Still below the threshold at the end: nothing converts.
                cloud += source;
                continue;
            }
            const double after = threshold + x;
            autoconversion[k] = std::max(0.0, cloud + source - after);
            cloud = cloud + source - autoconversion[k];
        }
        converted += autoconversion[k];
    }
    double large_scale = 0.0;   // falling, kg/kg of a layer
    double convective = 0.0;
    double rain_evaporation = 0.0;
    for (std::size_t k = n; k-- > 0U;) {
        const double falling = large_scale + convective;
        if (falling > 0.0) {
            const double pressure = moisture.pressure_Pa[k];
            const double relative = humidity_kg_kg[k] /
                                    saturation_specific_humidity(temperature_K[k], pressure);
            const double wanted = moisture.rain_evaporation_efficiency *
                                  std::max(0.0, 1.0 - relative) * moisture.evaporation_fraction *
                                  falling;
            const double x =
                wanted > 0.0
                    ? std::min(wanted, evaporation_to_saturation(temperature_K[k],
                                                                 humidity_kg_kg[k], pressure,
                                                                 heating))
                    : 0.0;
            temperature_K[k] -= heating * x;
            humidity_kg_kg[k] += x;
            const double kept = 1.0 - x / falling;
            large_scale *= kept;
            convective *= kept;
            rain_evaporation += x;
            result.rain_evaporation_kg_kg[k] = x;
        }
        // At most the layer's convective condensate is convective rain: the
        // autoconversion also drains cloud water from earlier steps.
        const double from_convection =
            std::min(autoconversion[k] * convective_share[k], convective_kg_kg[k]);
        large_scale += autoconversion[k] - from_convection;
        convective += from_convection;
    }
    result.precipitation_kg_m2 = (large_scale + convective) * mass;
    result.convective_kg_m2 = convective * mass;
    result.autoconversion_kg_m2 = converted * mass;
    result.cloud_evaporation_kg_m2 = cloud_evaporation * mass;
    result.rain_evaporation_kg_m2 = rain_evaporation * mass;
    return result;
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
        LayerArray condensing_share{};             // dC_k/dq_k
        LayerArray d_condensate_d_temperature{};   // dC_k/dT_k
    };
    // L m / Δt: the heating of a unit of condensate.
    const double condensation_rate =
        moisture != nullptr ? moisture->latent_J_kg * moisture->layer_mass_kg_m2 / dt_s : 0.0;
    // With moist convection there is no τ_c: the solve condenses the whole
    // excess at the new temperatures (ADR-0023 §4.3), so that the step's
    // latent heat is in the implicit balance the radiation and the transport
    // see; convection and the pass after it follow (task M8-02).
    const double in_solve_fraction =
        moisture == nullptr ? 0.0
                            : (moisture->moist_convection ? 1.0 : moisture->condensation_fraction);
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
                if (moisture->moist_convection && moisture->subgrid_width_fraction > 0.0) {
                    // ADR-0025 §4.1: towards the sub-grid equilibrium of the
                    // layer's total water (β = 0 takes the branch below,
                    // saturation adjustment, bit for bit).
                    const double cloud = std::max(0.0, moisture->cloud_water_kg_kg[k]);
                    const auto equilibrium =
                        layer_equilibrium(*moisture, k, e.humidity[k] + cloud, t[k]);
                    e.condensate[k] = std::max(0.0, equilibrium.cloud.water_kg_kg - cloud);
                    e.condensing_share[k] = e.condensate[k] > 0.0 ? equilibrium.cloud.area : 0.0;
                    e.d_condensate_d_temperature[k] =
                        e.condensate[k] > 0.0 ? equilibrium.d_water_d_temperature : 0.0;
                } else {
                    e.condensate[k] = in_solve_fraction *
                                      std::max(0.0, e.humidity[k] - saturation_specific_humidity(
                                                                        t[k], moisture->pressure_Pa[k]));
                    e.condensing_share[k] = e.condensate[k] > 0.0 ? in_solve_fraction : 0.0;
                    e.d_condensate_d_temperature[k] =
                        e.condensate[k] > 0.0
                            ? -in_solve_fraction * saturation_specific_humidity_slope(
                                                       t[k], moisture->pressure_Pa[k])
                            : 0.0;
                }
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
                    // dC_r/dT_c: the bottom layer's evaporation, which
                    // condenses in the share dC/dq, and the layer's own
                    // equilibrium.
                    double d_condensate = 0.0;
                    if (moisture->moist_convection && moisture->subgrid_width_fraction > 0.0) {
                        if (r == 0U) {
                            d_condensate += e.condensing_share[r] *
                                            (e.exchange.d_vapour_d_downward * d_down +
                                             e.exchange.d_vapour_d_air * d_air) *
                                            dt_s / moisture->layer_mass_kg_m2;
                        }
                        if (r == c) {
                            d_condensate += e.d_condensate_d_temperature[r];
                        }
                        value -= condensation_rate * d_condensate;
                    } else {
                        // Saturation adjustment, as before ADR-0025.
                        if (r == 0U) {
                            d_condensate += (e.exchange.d_vapour_d_downward * d_down +
                                             e.exchange.d_vapour_d_air * d_air) *
                                            dt_s / moisture->layer_mass_kg_m2;
                        }
                        if (r == c) {
                            d_condensate -= saturation_specific_humidity_slope(
                                e.temperature[r], moisture->pressure_Pa[r]);
                        }
                        value -= condensation_rate * in_solve_fraction * d_condensate;
                    }
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
    if (moisture != nullptr) {
        const double specific_heat = column.layer_heat_capacity_J_m2_K / moisture->layer_mass_kg_m2;
        double slope = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            // T_m's L q / c_p: a warmer layer condenses less (dC/dT < 0).
            double gain = 1.0;
            if (moisture->moist_convection && moisture->subgrid_width_fraction > 0.0) {
                gain -= moisture->latent_J_kg / specific_heat *
                        current.d_condensate_d_temperature[k];
            } else if (current.condensate[k] > 0.0) {
                gain += in_solve_fraction * moisture->latent_J_kg / specific_heat *
                        saturation_specific_humidity_slope(current.temperature[k],
                                                           moisture->pressure_Pa[k]);
            }
            slope += gain * response[k];
        }
        // The share of the surface's evaporation that stays in the bottom
        // layer's vapour: all but what condenses (ADR-0025 §4.1); M7's path
        // keeps none where the layer condenses at all.
        const double kept = moisture->moist_convection
                                ? 1.0 - current.condensing_share[0]
                                : (current.condensate[0] > 0.0 ? 0.0 : 1.0);
        if (kept > 0.0) {
            double d_vapour = 0.0;
            for (std::size_t c = 0; c < n; ++c) {
                LayerArray d_emission{};
                d_emission[c] = 4.0 * current.emission[c] / current.temperature[c];
                const double d_down = downward_surface_longwave(column, d_emission);
                const double d_air = c == 0U ? column.air_factor : 0.0;
                d_vapour += (current.exchange.d_vapour_d_downward * d_down +
                             current.exchange.d_vapour_d_air * d_air) *
                            response[c];
            }
            slope += kept * moisture->latent_J_kg / specific_heat * d_vapour * dt_s /
                     moisture->layer_mass_kg_m2;
        }
        result.moist_enthalpy_slope_K_m2_W = slope / static_cast<double>(n);
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
    const bool moist = convection && moisture != nullptr && moisture->moist_convection;
    LayerArray convective_kg_kg{};   // each layer's drying by Betts–Miller
    LayerArray convective_weight{};  // its weights relative to the bottom's, w_k / w_0 (0: none)
    if (convection) {
        // The adjustment is linear for its pools: the θ_c response sees it.
        // With moist convection it is to the dry adiabat (ADR-0023 §4.2).
        ColumnRadiation adiabat = column;
        if (moist) adiabat.exner = column.exner_dry;
        ConvectivePools pools;
        result.adjusted = convective_adjustment(adiabat, temperature_K, &pools);
        for (std::size_t pool = 0; pool < pools.count; ++pool) {
            double sum = 0.0;
            double weight = 0.0;
            for (std::size_t k = pools.start[pool]; k < pools.start[pool + 1U]; ++k) {
                sum += response[k];
                weight += adiabat.exner[k];
            }
            for (std::size_t k = pools.start[pool]; k < pools.start[pool + 1U]; ++k) {
                response[k] = sum / weight * adiabat.exner[k];
            }
        }
    }
    if (moist) {
        LayerArray before_t{};
        LayerArray before_q{};
        for (std::size_t k = 0; k < n; ++k) {
            before_t[k] = temperature_K[k];
            before_q[k] = result.humidity_kg_kg[k];
        }
        // The response through convection by its finite difference along the
        // pre-convection response, the layers that condensed in the solve
        // keeping saturation (δq = q_sat′ δT): the relaxation removes less
        // vapour from a warmer column, so it keeps only part of a warming
        // (about c_p / (c_p + L RH q_sat′), a quarter in the tropics).
        double largest = 0.0;
        for (std::size_t k = 0; k < n; ++k) largest = std::max(largest, std::abs(response[k]));
        const double epsilon = largest > 0.0 ? 1e-3 / largest : 0.0;
        LayerArray perturbed_t{};
        LayerArray perturbed_q{};
        for (std::size_t k = 0; k < n; ++k) {
            perturbed_t[k] = before_t[k] + epsilon * response[k];
            perturbed_q[k] = before_q[k];
            if (current.condensate[k] > 0.0) {
                perturbed_q[k] += saturation_specific_humidity_slope(before_t[k],
                                                                     moisture->pressure_Pa[k]) *
                                  epsilon * response[k];
            }
        }
        const BettsMillerResult bm = betts_miller(
            column, *moisture, temperature_K, std::span<double>(result.humidity_kg_kg.data(), n));
        for (std::size_t k = 0; k < n; ++k) {
            convective_kg_kg[k] = before_q[k] - result.humidity_kg_kg[k];
        }
        if (moisture->convective_moistening) {
            // ADR-0026 option 1: the moistened layers' gain is water the
            // dried layers lost. Each layer's convective condensate is its
            // drying, scaled to sum to the column's net loss.
            double dried = 0.0;
            double net = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                dried += std::max(0.0, convective_kg_kg[k]);
                net += convective_kg_kg[k];
            }
            const double scale = dried > 0.0 ? std::max(0.0, net) / dried : 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                convective_kg_kg[k] = std::max(0.0, convective_kg_kg[k]) * scale;
            }
        }
        result.convection_top = bm.top;
        if (bm.top >= 1 && bm.weight[0] > 0.0) {
            for (std::size_t k = 0; k < n; ++k) convective_weight[k] = bm.weight[k] / bm.weight[0];
        }
        if (bm.top >= 1) {
            result.adjusted = true;
            const double specific_heat = column.layer_heat_capacity_J_m2_K / moisture->layer_mass_kg_m2;
            double change = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                change += moisture->layer_mass_kg_m2 *
                          (specific_heat * (temperature_K[k] - before_t[k]) +
                           moisture->latent_J_kg * (result.humidity_kg_kg[k] - before_q[k]));
            }
            result.convection_enthalpy_change_J_m2 = change;
            result.convective_precipitation_kg_m2_s = bm.precipitation_kg_m2 / dt_s;
            result.precipitation_kg_m2_s += result.convective_precipitation_kg_m2_s;
            result.condensation_W_m2 += moisture->latent_J_kg * result.convective_precipitation_kg_m2_s;
            if (epsilon > 0.0) {
                static_cast<void>(betts_miller(column, *moisture,
                                               std::span<double>(perturbed_t.data(), n),
                                               std::span<double>(perturbed_q.data(), n)));
                for (std::size_t k = 0; k < n; ++k) {
                    response[k] = (perturbed_t[k] - temperature_K[k]) / epsilon;
                }
            }
        }
    }
    if (moisture != nullptr && convection) {
        // The convective adjustment moves the condensation's heat upward
        // and leaves the layers below supersaturated at their new
        // temperatures: each condenses its excess once, isobarically,
        // c_p (T' − T) = L (q − q_sat(T')) (ADR-0021 V4: no layer above
        // saturation after the step). Its heat may leave the column unstable
        // for the next step's adjustment: alternating the two to the end
        // made θ_c(h) non-smooth and tripled the coupled Newton's
        // iterations (task M7-07).
        const double specific_heat = column.layer_heat_capacity_J_m2_K / moisture->layer_mass_kg_m2;
        const double heating = moisture->latent_J_kg / specific_heat;   // K per kg/kg
        double extra = 0.0;
        {
            double condensed = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                const double pressure = moisture->pressure_Pa[k];
                const double q = result.humidity_kg_kg[k];
                if (moist && moisture->subgrid_width_fraction > 0.0) {
                    // ADR-0025 §4.1: towards the sub-grid equilibrium of the
                    // layer's total water, the cloud water so far included
                    // (the step's start, the solve's condensate and
                    // Betts–Miller's drying, which all become cloud water):
                    // c_p (T' − T) = L (q_l*(T') − q_l), Newton on
                    // g(T') = T' − T − h (q_l*(T') − q_l), increasing.
                    const double held = std::max(0.0, moisture->cloud_water_kg_kg[k]) +
                                        result.condensate_kg_kg[k] + convective_kg_kg[k];
                    const double total = q + held;
                    if (!(layer_equilibrium(*moisture, k, total, temperature_K[k]).cloud.water_kg_kg >
                          held)) {
                        continue;
                    }
                    double t = temperature_K[k];
                    for (int iteration = 0; iteration < 30; ++iteration) {
                        const auto equilibrium = layer_equilibrium(*moisture, k, total, t);
                        const double g =
                            t - temperature_K[k] - heating * (equilibrium.cloud.water_kg_kg - held);
                        const double next =
                            t - g / (1.0 - heating * equilibrium.d_water_d_temperature);
                        if (std::abs(next - t) <= 4.0 * std::numeric_limits<double>::epsilon() * t) {
                            t = next;
                            break;
                        }
                        t = next;
                    }
                    const double condensate = (t - temperature_K[k]) / heating;
                    temperature_K[k] = t;
                    result.humidity_kg_kg[k] = q - condensate;
                    result.condensate_kg_kg[k] += condensate;
                    condensed += condensate;
                    continue;
                }
                if (!(q > saturation_specific_humidity(temperature_K[k], pressure))) {
                    continue;
                }
                // Newton on g(T') = T' − T − f h (q − q_sat(T')), increasing
                // (f the condensation fraction).
                const double share = heating * (moist ? 1.0 : moisture->condensation_fraction);
                double t = temperature_K[k];
                for (int iteration = 0; iteration < 30; ++iteration) {
                    const double g =
                        t - temperature_K[k] - share * (q - saturation_specific_humidity(t, pressure));
                    const double next =
                        t - g / (1.0 + share * saturation_specific_humidity_slope(t, pressure));
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
        }
        result.precipitation_kg_m2_s += extra * moisture->layer_mass_kg_m2 / dt_s;
        result.condensation_W_m2 += condensation_rate * extra;
    }
    if (moist) {
        // The condensate becomes cloud water, which rains by autoconversion;
        // the rain and the cloud water re-evaporate (ADR-0023 §4.3).
        result.cloud_water_kg_kg = moisture->cloud_water_kg_kg;
        // ADR-0025 §4.3 (§13). A layer's convective condensate is its
        // Betts–Miller drying V_k and the share ŵ_k = w_k / w_0 of its
        // condensation in the solve and after convection: the layer's place
        // in the convecting column, not how strongly it convects, which
        // depends on the step (a month's step meets a month's instability).
        // A share e_d of the column's convective condensate is moved, as
        // liquid (no latent heat moves), to its detrainment layers, weighted
        // d_k = ŵ_k (1 − ŵ_{k+1}): the top of the convection.
        LayerArray source{};
        LayerArray convective{};
        double detrained = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            source[k] = result.condensate_kg_kg[k] + convective_kg_kg[k];
            convective[k] = convective_kg_kg[k] + convective_weight[k] * result.condensate_kg_kg[k];
        }
        if (moisture->detrainment_share > 0.0) {
            LayerArray detrainment{};
            double total_weight = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                const double above = k + 1U < n ? convective_weight[k + 1U] : 0.0;
                detrainment[k] = convective_weight[k] * (1.0 - above);
                total_weight += detrainment[k];
            }
            if (total_weight > 0.0) {
                for (std::size_t k = 0; k < n; ++k) {
                    const double moved = moisture->detrainment_share * convective[k];
                    source[k] -= moved;
                    convective[k] -= moved;
                    detrained += moved;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double arriving = detrained * (detrainment[k] / total_weight);
                    source[k] += arriving;
                    convective[k] += arriving;
                }
            }
        }
        result.detrained_kg_m2_s = detrained * moisture->layer_mass_kg_m2 / dt_s;
        const MicrophysicsResult rain = cloud_microphysics(
            column, *moisture, std::span<const double>(source.data(), n),
            std::span<const double>(convective.data(), n), temperature_K,
            std::span<double>(result.humidity_kg_kg.data(), n),
            std::span<double>(result.cloud_water_kg_kg.data(), n));
        result.convective_condensate_kg_kg = convective_kg_kg;
        result.cloud_source_kg_kg = source;
        result.convective_cloud_source_kg_kg = convective;
        double condensed = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            condensed += result.condensate_kg_kg[k] + convective_kg_kg[k];
        }
        const double evaporated = rain.cloud_evaporation_kg_m2 + rain.rain_evaporation_kg_m2;
        result.precipitation_kg_m2_s = rain.precipitation_kg_m2 / dt_s;
        result.convective_precipitation_kg_m2_s = rain.convective_kg_m2 / dt_s;
        result.cloud_evaporation_kg_m2_s = rain.cloud_evaporation_kg_m2 / dt_s;
        result.cloud_evaporation_by_layer_kg_kg = rain.cloud_evaporation_kg_kg;
        result.rain_evaporation_by_layer_kg_kg = rain.rain_evaporation_kg_kg;
        result.rain_evaporation_kg_m2_s = rain.rain_evaporation_kg_m2 / dt_s;
        result.condensation_W_m2 =
            moisture->latent_J_kg * (condensed * moisture->layer_mass_kg_m2 - evaporated) / dt_s;
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
    if (moisture != nullptr) {
        const double specific_heat = column.layer_heat_capacity_J_m2_K / moisture->layer_mass_kg_m2;
        double enthalpy = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            enthalpy += temperature_K[k] +
                        moisture->latent_J_kg / specific_heat * result.humidity_kg_kg[k];
        }
        result.moist_enthalpy_K = enthalpy / static_cast<double>(n);
    }
    return result;
}

}  // namespace planetsim
