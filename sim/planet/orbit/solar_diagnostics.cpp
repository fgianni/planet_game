#include "sim/planet/orbit/solar_diagnostics.hpp"

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace planetsim {

SolarDiagnostics analyze_solar_forcing(const PlanetState& state) {
    SolarDiagnostics result;
    result.cell_count = state.mesh().cell_count();
    result.min_insolation_W_m2 = std::numeric_limits<double>::infinity();
    result.max_insolation_W_m2 = -std::numeric_limits<double>::infinity();

    const auto& forcing = state.forcing();
    double total_area_m2 = 0.0;
    for (const auto& cell : state.mesh().cells()) {
        const double insolation = forcing.top_of_atmosphere_insolation_W_m2[cell.id];
        const double incidence = dot(cell.center_unit, forcing.orbit.sun_direction_body_unit);
        total_area_m2 += cell.area_m2;

        if (!std::isfinite(insolation)) {
            ++result.non_finite_count;
            continue;
        }
        result.min_insolation_W_m2 = std::min(result.min_insolation_W_m2, insolation);
        result.max_insolation_W_m2 = std::max(result.max_insolation_W_m2, insolation);
        result.total_incoming_power_W += insolation * cell.area_m2;

        if (incidence > 0.0) {
            ++result.illuminated_cell_count;
        } else {
            ++result.night_cell_count;
            if (insolation != 0.0) {
                ++result.night_side_nonzero_count;
            }
        }
        if (insolation < 0.0 || insolation > forcing.incident_solar_flux_W_m2) {
            ++result.out_of_range_count;
        }
    }

    if (result.cell_count == 0) {
        result.min_insolation_W_m2 = 0.0;
        result.max_insolation_W_m2 = 0.0;
        return result;
    }

    result.expected_incoming_power_W = std::numbers::pi_v<double> * state.mesh().radius_m() *
                                       state.mesh().radius_m() * forcing.incident_solar_flux_W_m2;
    result.global_mean_insolation_W_m2 = result.total_incoming_power_W / total_area_m2;
    result.expected_global_mean_insolation_W_m2 = 0.25 * forcing.incident_solar_flux_W_m2;
    if (result.expected_global_mean_insolation_W_m2 > 0.0) {
        result.relative_global_mean_error = std::abs(result.global_mean_insolation_W_m2 -
                                                     result.expected_global_mean_insolation_W_m2) /
                                            result.expected_global_mean_insolation_W_m2;
    }
    return result;
}

}  // namespace planetsim
