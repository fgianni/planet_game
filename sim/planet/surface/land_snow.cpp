#include "sim/planet/surface/land_snow.hpp"

#include "sim/planet/surface/cryosphere_constants.hpp"

#include <cmath>
#include <stdexcept>

namespace planetsim {

double snow_covered_albedo(double ground_albedo, double snow_kg_m2) noexcept {
    const double cover = snow_kg_m2 / (snow_kg_m2 + snow_masking_kg_m2);
    return ground_albedo + (snow_albedo - ground_albedo) * cover;
}

LandSnowStepResult step_land_tile(const ColumnProperties& ground, ColumnState state,
                                  double snow_kg_m2, double insolation_W_m2,
                                  double precipitation_kg_m2_s, double grey_emissivity,
                                  double dt_s) {
    if (!std::isfinite(snow_kg_m2) || snow_kg_m2 < 0.0) {
        throw std::invalid_argument("snow must be finite and non-negative");
    }
    if (!std::isfinite(precipitation_kg_m2_s) || precipitation_kg_m2_s < 0.0) {
        throw std::invalid_argument("precipitation must be finite and non-negative");
    }

    LandSnowStepResult result;
    ColumnProperties column = ground;
    // Without snow the tile is exactly the ADR-0007 column (ADR-0008 V6).
    if (snow_kg_m2 > 0.0) {
        column.albedo = snow_covered_albedo(ground.albedo, snow_kg_m2);
    }
    result.albedo = column.albedo;
    const double precipitation_kg_m2 = precipitation_kg_m2_s * dt_s;
    if (state.surface_K <= melting_point_K) {
        result.snowfall_kg_m2 = precipitation_kg_m2;
    } else {
        result.rain_kg_m2 = precipitation_kg_m2;
    }
    const double snow_before_melt = snow_kg_m2 + result.snowfall_kg_m2;

    const ColumnSystem system =
        column_system(column, state, insolation_W_m2, grey_emissivity, dt_s);
    double surface_K = solve_column_surface(system);
    double sink_W_m2 = 0.0;
    if (snow_before_melt > 0.0 && surface_K > melting_point_K) {
        const double surplus_W_m2 = system.surplus_W_m2(melting_point_K);
        const double melt_kg_m2 = surplus_W_m2 * dt_s / latent_heat_of_fusion_J_kg;
        if (melt_kg_m2 < snow_before_melt) {
            surface_K = melting_point_K;
            result.melt_kg_m2 = melt_kg_m2;
            sink_W_m2 = surplus_W_m2;
        } else {
            // All the snow melts; what remains warms the column.
            result.melt_kg_m2 = snow_before_melt;
            sink_W_m2 = latent_heat_of_fusion_J_kg * snow_before_melt / dt_s;
            surface_K = solve_column_surface(system, sink_W_m2);
        }
    }
    result.snow_kg_m2 = snow_before_melt - result.melt_kg_m2;
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * result.melt_kg_m2;

    auto& step = result.column;
    step.state.surface_K = surface_K;
    step.state.lower_K = system.lower_K(state.lower_K, surface_K);
    step.absorbed_W_m2 = system.absorbed_W_m2;
    step.emitted_W_m2 = system.radiative * surface_K * surface_K * surface_K * surface_K;
    step.storage_change_J_m2 =
        column.surface_heat_capacity_J_m2_K * (step.state.surface_K - state.surface_K) +
        column.lower_heat_capacity_J_m2_K * (step.state.lower_K - state.lower_K);
    step.newton_residual_W_m2 = system.a * surface_K + step.emitted_W_m2 - (system.b - sink_W_m2);
    return result;
}

}  // namespace planetsim
