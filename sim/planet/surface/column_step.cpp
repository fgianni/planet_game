#include "sim/planet/surface/column_step.hpp"

#include "sim/planet/atmosphere/saturation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace planetsim {
namespace {

void validate_grey_emissivity(double grey_emissivity) {
    if (!(grey_emissivity >= 0.0 && grey_emissivity < 1.0)) {
        throw std::invalid_argument("grey-layer emissivity must lie in [0, 1)");
    }
}

}  // namespace

ColumnSystem column_system(const ColumnProperties& column, ColumnState state,
                           double insolation_W_m2, double grey_emissivity, double dt_s) {
    validate_grey_emissivity(grey_emissivity);
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument("column step must be finite and positive");
    }
    if (!(state.surface_K > 0.0) || !(state.lower_K > 0.0) || !std::isfinite(state.surface_K) ||
        !std::isfinite(state.lower_K)) {
        throw std::invalid_argument("column temperatures must be finite and positive");
    }
    if (!std::isfinite(insolation_W_m2) || insolation_W_m2 < 0.0) {
        throw std::invalid_argument("insolation must be finite and non-negative");
    }

    ColumnSystem system;
    system.radiative = (1.0 - 0.5 * grey_emissivity) * column.emissivity *
                       stefan_boltzmann_W_m2_K4;
    system.absorbed_W_m2 = (1.0 - column.albedo) * insolation_W_m2;
    const double surface_rate = column.surface_heat_capacity_J_m2_K / dt_s;
    system.lower_rate = column.lower_heat_capacity_J_m2_K / dt_s;
    system.exchange = column.exchange_W_m2_K;
    // Exchange with the lower layer after eliminating it: k_eff (T_s' − T_l).
    const double effective_exchange =
        system.exchange * system.lower_rate / (system.lower_rate + system.exchange);
    system.a = surface_rate + effective_exchange;
    system.b = surface_rate * state.surface_K + system.absorbed_W_m2 +
               effective_exchange * state.lower_K;
    return system;
}

namespace {

// A flux and its derivative in κ.
struct Limited {
    double value = 0.0;
    double slope = 0.0;
};

// Dew, κ < 0: κ V / (V − κ Δt), never more than the vapour V in a step.
[[nodiscard]] Limited dew_limited(double kappa, double vapour, double dt) noexcept {
    const double d = vapour - kappa * dt;
    if (!(d > 0.0)) {
        return {0.0, 0.0};
    }
    return {kappa * vapour / d, vapour * vapour / (d * d)};
}

// Free water or ice: κ for evaporation, the dew limit below.
[[nodiscard]] Limited free_limited(double kappa, double vapour, double dt) noexcept {
    return kappa < 0.0 ? dew_limited(kappa, vapour, dt) : Limited{kappa, 1.0};
}

// κ S / (S + κ Δt) and its derivative in κ for κ ≥ 0; dew below.
[[nodiscard]] Limited snow_limited(double kappa, double store, double vapour,
                                   double dt) noexcept {
    if (kappa < 0.0) {
        return dew_limited(kappa, vapour, dt);
    }
    const double d = store + kappa * dt;
    if (!(d > 0.0)) {
        return {0.0, 0.0};
    }
    return {kappa * store / d, store * store / (d * d)};
}

// κ min(1, W / (W_c + κ Δt)) and its derivative for κ ≥ 0; dew below.
[[nodiscard]] Limited bucket_limited(double kappa, double store, double threshold,
                                     double vapour, double dt) noexcept {
    if (kappa < 0.0) {
        return dew_limited(kappa, vapour, dt);
    }
    const double d = threshold + kappa * dt;
    if (store >= d) {
        return {kappa, 1.0};
    }
    return {kappa * store / d, store * threshold / (d * d)};
}

}  // namespace

namespace {

// The demand δq per unit of τ_u and its derivatives in x, q_a and q_cap: the
// unsaturated q_sat(x) − q_a, or the raining layer's R (q_sat(x) − q_cap).
struct Deficit {
    double value = 0.0;
    double surface = 0.0;
    double air = 0.0;
    double cap = 0.0;
};

[[nodiscard]] Deficit deficit(const ColumnSystem& system, double surface_K) noexcept {
    const double saturated = saturation_specific_humidity(surface_K, system.pressure_Pa);
    const double slope = saturation_specific_humidity_slope(surface_K, system.pressure_Pa);
    Deficit d{saturated - system.air_humidity, slope, -1.0, 0.0};
    if (system.cap_humidity >= 0.0) {
        const double raining = system.cap_ratio * (saturated - system.cap_humidity);
        if (raining > d.value) {
            d = {raining, system.cap_ratio * slope, 0.0, -system.cap_ratio};
        }
    }
    return d;
}

}  // namespace

ColumnSystem::Evaporation ColumnSystem::evaporation_kg_m2_s(double surface_K) const noexcept {
    Evaporation e;
    if (!evaporates()) {
        return e;
    }
    const double dq = deficit(*this, surface_K).value;
    e.water = free_limited(water_transfer * dq, vapour_kg_m2, step_s).value;
    e.ice = free_limited(ice_transfer * dq, vapour_kg_m2, step_s).value;
    e.snow = snow_limited(snow_transfer * dq, snow_kg_m2, vapour_kg_m2, step_s).value;
    e.bucket = bucket_limited(bucket_transfer * dq, bucket_kg_m2, bucket_threshold_kg_m2,
                              vapour_kg_m2, step_s)
                   .value;
    return e;
}

double ColumnSystem::latent_flux_W_m2(double surface_K) const noexcept {
    if (!evaporates()) {
        return 0.0;
    }
    const Evaporation e = evaporation_kg_m2_s(surface_K);
    return latent_heat_vaporisation_J_kg * (e.water + e.bucket) +
           latent_heat_sublimation_J_kg * (e.ice + e.snow);
}

double ColumnSystem::latent_slope_W_m2_K(double surface_K) const noexcept {
    if (!evaporates()) {
        return 0.0;
    }
    const Deficit d = deficit(*this, surface_K);
    const double dq = d.value;
    const double dq_dx = d.surface;
    const double water = free_limited(water_transfer * dq, vapour_kg_m2, step_s).slope *
                         water_transfer;
    const double ice = free_limited(ice_transfer * dq, vapour_kg_m2, step_s).slope * ice_transfer;
    const double snow = snow_limited(snow_transfer * dq, snow_kg_m2, vapour_kg_m2, step_s).slope *
                        snow_transfer;
    const double bucket = bucket_limited(bucket_transfer * dq, bucket_kg_m2,
                                         bucket_threshold_kg_m2, vapour_kg_m2, step_s)
                              .slope *
                          bucket_transfer;
    return dq_dx * (latent_heat_vaporisation_J_kg * (water + bucket) +
                    latent_heat_sublimation_J_kg * (ice + snow));
}

ColumnSystem::VapourSlopes ColumnSystem::vapour_slopes(double surface_K) const noexcept {
    VapourSlopes v;
    if (!evaporates()) {
        return v;
    }
    const Deficit d = deficit(*this, surface_K);
    const double dq = d.value;
    // dE_k/dδq of each source.
    const double water = free_limited(water_transfer * dq, vapour_kg_m2, step_s).slope *
                         water_transfer;
    const double ice = free_limited(ice_transfer * dq, vapour_kg_m2, step_s).slope * ice_transfer;
    const double snow = snow_limited(snow_transfer * dq, snow_kg_m2, vapour_kg_m2, step_s).slope *
                        snow_transfer;
    const double bucket = bucket_limited(bucket_transfer * dq, bucket_kg_m2,
                                         bucket_threshold_kg_m2, vapour_kg_m2, step_s)
                              .slope *
                          bucket_transfer;
    const double total = water + ice + snow + bucket;
    const double latent = latent_heat_vaporisation_J_kg * (water + bucket) +
                          latent_heat_sublimation_J_kg * (ice + snow);
    v.surface = d.surface * total;
    v.air = d.air * total;
    v.cap = d.cap * total;
    v.latent_air = d.air * latent;
    v.latent_cap = d.cap * latent;
    return v;
}

double ColumnSystem::surplus_W_m2(double surface_K) const noexcept {
    const double x3 = surface_K * surface_K * surface_K;
    return b - a * surface_K - radiative * x3 * surface_K - latent_flux_W_m2(surface_K);
}

double solve_column_surface(const ColumnSystem& system, double sink_W_m2) {
    const double b = system.b - sink_W_m2;
    if (system.evaporates()) {
        // With evaporation: safeguarded Newton from the root without it. The
        // function is increasing, so a bracket [low, high] around the root
        // shrinks every iteration; a step leaving it bisects.
        ColumnSystem dry = system;
        dry.water_transfer = dry.ice_transfer = dry.snow_transfer = dry.bucket_transfer = 0.0;
        double x = solve_column_surface(dry, sink_W_m2);
        double low = 0.0;
        double high = std::numeric_limits<double>::infinity();
        constexpr int max_iterations = 60;
        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            const double x3 = x * x * x;
            const double emitted = system.radiative * x3 * x;
            const double latent = system.latent_flux_W_m2(x);
            const double value = system.a * x + emitted + latent - b;
            // At the root to rounding: the residual is at the level of its
            // own terms' last bits.
            const double terms = system.a * x + emitted + std::abs(latent) + std::abs(b);
            if (std::abs(value) <= 64.0 * std::numeric_limits<double>::epsilon() * terms) {
                return x;
            }
            (value > 0.0 ? high : low) = x;
            const double slope =
                system.a + 4.0 * system.radiative * x3 + system.latent_slope_W_m2_K(x);
            double next = x - value / slope;
            if (!(next > low && next < high)) {
                next = std::isfinite(high) ? 0.5 * (low + high) : 2.0 * x;
            }
            if (std::abs(next - x) <= 4.0 * std::numeric_limits<double>::epsilon() * x) {
                return next;
            }
            x = next;
        }
        return x;
    }
    // Both terms of f(x) = a x + r x⁴ − b are increasing, so each alone
    // overestimates the root: the smaller of the two is a start at or above it.
    double x = std::min(b / system.a, std::sqrt(std::sqrt(b / system.radiative)));
    // The iterates fall monotonically onto the root; once a step is at the
    // rounding level the root is reached and further iterations only
    // repeat it (task M5-04: this loop was 38 % of a climate step).
    for (int iteration = 0; iteration < column_newton_iterations; ++iteration) {
        const double x3 = x * x * x;
        const double value = system.a * x + system.radiative * x3 * x - b;
        const double slope = system.a + 4.0 * system.radiative * x3;
        const double step = value / slope;
        x -= step;
        if (std::abs(step) <= 4.0 * std::numeric_limits<double>::epsilon() * x) {
            break;
        }
    }
    return x;
}

double ColumnSystem::slope_K_m2_W(double surface_K) const noexcept {
    return 1.0 / (a + 4.0 * radiative * surface_K * surface_K * surface_K +
                  latent_slope_W_m2_K(surface_K));
}

ColumnStepResult complete_column_step(const ColumnProperties& column, const ColumnSystem& system,
                                      ColumnState before, double surface_K, double sink_W_m2,
                                      double source_W_m2) {
    ColumnStepResult result;
    result.state.surface_K = surface_K;
    result.state.lower_K = system.lower_K(before.lower_K, surface_K);
    result.absorbed_W_m2 = system.absorbed_W_m2;
    result.emitted_W_m2 = system.radiative * surface_K * surface_K * surface_K * surface_K;
    result.storage_change_J_m2 =
        column.surface_heat_capacity_J_m2_K * (result.state.surface_K - before.surface_K) +
        column.lower_heat_capacity_J_m2_K * (result.state.lower_K - before.lower_K);
    result.evaporation_W_m2 = system.latent_flux_W_m2(surface_K);
    result.newton_residual_W_m2 = system.a * surface_K + result.emitted_W_m2 +
                                  result.evaporation_W_m2 - (system.b - sink_W_m2);
    result.source_W_m2 = source_W_m2;
    return result;
}

ColumnStepResult solve_column_step(const ColumnProperties& column, ColumnSystem system,
                                   ColumnState before, double source_W_m2,
                                   double exchange_W_m2_K) {
    system.a += exchange_W_m2_K;
    system.b += source_W_m2;
    const double x = solve_column_surface(system);
    auto result = complete_column_step(column, system, before, x, 0.0,
                                       source_W_m2 - exchange_W_m2_K * x);
    result.surface_slope_K_m2_W = system.slope_K_m2_W(x);
    return result;
}

ColumnStepResult step_column(const ColumnProperties& column, ColumnState state,
                             double insolation_W_m2, double grey_emissivity, double dt_s,
                             double source_W_m2) {
    return solve_column_step(
        column, column_system(column, state, insolation_W_m2, grey_emissivity, dt_s), state,
        source_W_m2);
}

double column_equilibrium_temperature_K(const ColumnProperties& column, double insolation_W_m2,
                                        double grey_emissivity) {
    validate_grey_emissivity(grey_emissivity);
    const double radiative = (1.0 - 0.5 * grey_emissivity) * column.emissivity *
                             stefan_boltzmann_W_m2_K4;
    return std::sqrt(std::sqrt((1.0 - column.albedo) * insolation_W_m2 / radiative));
}

}  // namespace planetsim
