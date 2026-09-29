#include "sim/planet/surface/column_step.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace planetsim {
namespace {

void validate_grey_emissivity(double grey_emissivity) {
    if (!(grey_emissivity >= 0.0 && grey_emissivity < 1.0)) {
        throw std::invalid_argument("grey-layer emissivity must lie in [0, 1)");
    }
}

}  // namespace

ColumnStepResult step_column(const ColumnProperties& column, ColumnState state,
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

    const double radiative = (1.0 - 0.5 * grey_emissivity) * column.emissivity *
                             stefan_boltzmann_W_m2_K4;
    const double absorbed = (1.0 - column.albedo) * insolation_W_m2;
    const double surface_rate = column.surface_heat_capacity_J_m2_K / dt_s;
    const double lower_rate = column.lower_heat_capacity_J_m2_K / dt_s;
    // Exchange with the lower layer after eliminating it: k_eff (T_s' − T_l).
    const double effective_exchange =
        column.exchange_W_m2_K * lower_rate / (lower_rate + column.exchange_W_m2_K);

    const double a = surface_rate + effective_exchange;
    const double b = surface_rate * state.surface_K + absorbed + effective_exchange * state.lower_K;
    // Both terms of f(x) = a x + r x⁴ − b are increasing, so each alone
    // overestimates the root: the smaller of the two is a start at or above it.
    double x = std::min(b / a, std::sqrt(std::sqrt(b / radiative)));
    for (int iteration = 0; iteration < column_newton_iterations; ++iteration) {
        const double x3 = x * x * x;
        const double value = a * x + radiative * x3 * x - b;
        const double slope = a + 4.0 * radiative * x3;
        x -= value / slope;
    }

    ColumnStepResult result;
    result.state.surface_K = x;
    result.state.lower_K = (lower_rate * state.lower_K + column.exchange_W_m2_K * x) /
                           (lower_rate + column.exchange_W_m2_K);
    result.absorbed_W_m2 = absorbed;
    result.emitted_W_m2 = radiative * x * x * x * x;
    result.storage_change_J_m2 =
        column.surface_heat_capacity_J_m2_K * (result.state.surface_K - state.surface_K) +
        column.lower_heat_capacity_J_m2_K * (result.state.lower_K - state.lower_K);
    result.newton_residual_W_m2 = a * x + result.emitted_W_m2 - b;
    return result;
}

double column_equilibrium_temperature_K(const ColumnProperties& column, double insolation_W_m2,
                                        double grey_emissivity) {
    validate_grey_emissivity(grey_emissivity);
    const double radiative = (1.0 - 0.5 * grey_emissivity) * column.emissivity *
                             stefan_boltzmann_W_m2_K4;
    return std::sqrt(std::sqrt((1.0 - column.albedo) * insolation_W_m2 / radiative));
}

}  // namespace planetsim
