#include "sim/planet/surface/column_step.hpp"

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

double ColumnSystem::surplus_W_m2(double surface_K) const noexcept {
    const double x3 = surface_K * surface_K * surface_K;
    return b - a * surface_K - radiative * x3 * surface_K;
}

double solve_column_surface(const ColumnSystem& system, double sink_W_m2) {
    const double b = system.b - sink_W_m2;
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
    return 1.0 / (a + 4.0 * radiative * surface_K * surface_K * surface_K);
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
    result.newton_residual_W_m2 =
        system.a * surface_K + result.emitted_W_m2 - (system.b - sink_W_m2);
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
