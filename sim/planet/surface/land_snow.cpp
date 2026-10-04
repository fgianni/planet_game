#include "sim/planet/surface/land_snow.hpp"

#include "sim/planet/surface/cover_fractions.hpp"
#include "sim/planet/surface/cryosphere_constants.hpp"

#include <cmath>
#include <stdexcept>

namespace planetsim {

double snow_covered_albedo(double ground_albedo, double snow_kg_m2) noexcept {
    const double cover = snow_cover_fraction(snow_kg_m2);
    return ground_albedo + (snow_albedo - ground_albedo) * cover;
}

LandSnowSystem prepare_land_tile(const ColumnProperties& ground, ColumnState state,
                                 double snow_kg_m2, double insolation_W_m2,
                                 double precipitation_kg_m2_s, double grey_emissivity,
                                 double dt_s) {
    if (!std::isfinite(snow_kg_m2) || snow_kg_m2 < 0.0) {
        throw std::invalid_argument("snow must be finite and non-negative");
    }
    if (!std::isfinite(precipitation_kg_m2_s) || precipitation_kg_m2_s < 0.0) {
        throw std::invalid_argument("precipitation must be finite and non-negative");
    }

    LandSnowSystem tile;
    tile.column = ground;
    // Without snow the tile is exactly the ADR-0007 column (ADR-0008 V6).
    if (snow_kg_m2 > 0.0) {
        tile.column.albedo = snow_covered_albedo(ground.albedo, snow_kg_m2);
    }
    const double precipitation_kg_m2 = precipitation_kg_m2_s * dt_s;
    if (state.surface_K <= melting_point_K) {
        tile.snowfall_kg_m2 = precipitation_kg_m2;
    } else {
        tile.rain_kg_m2 = precipitation_kg_m2;
    }
    tile.system = column_system(tile.column, state, insolation_W_m2, grey_emissivity, dt_s);
    tile.before = state;
    tile.snow_kg_m2 = snow_kg_m2;
    tile.dt_s = dt_s;
    return tile;
}

LandSnowStepResult solve_land_tile(const LandSnowSystem& tile, double source_W_m2,
                                   double exchange_W_m2_K) {
    ColumnSystem system = tile.system;
    system.a += exchange_W_m2_K;
    system.b += source_W_m2;
    const double snow_before_melt = tile.snow_kg_m2 + tile.snowfall_kg_m2;

    LandSnowStepResult result;
    result.albedo = tile.column.albedo;
    result.snowfall_kg_m2 = tile.snowfall_kg_m2;
    result.rain_kg_m2 = tile.rain_kg_m2;
    double surface_K = solve_column_surface(system);
    double sink_W_m2 = 0.0;
    bool clamped = false;
    if (snow_before_melt > 0.0 && surface_K > melting_point_K) {
        const double surplus_W_m2 = system.surplus_W_m2(melting_point_K);
        const double melt_kg_m2 = surplus_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
        if (melt_kg_m2 < snow_before_melt) {
            surface_K = melting_point_K;
            result.melt_kg_m2 = melt_kg_m2;
            sink_W_m2 = surplus_W_m2;
            clamped = true;
        } else {
            // All the snow melts; what remains warms the column.
            result.melt_kg_m2 = snow_before_melt;
            sink_W_m2 = latent_heat_of_fusion_J_kg * snow_before_melt / tile.dt_s;
            surface_K = solve_column_surface(system, sink_W_m2);
        }
    }
    result.snow_kg_m2 = snow_before_melt - result.melt_kg_m2;
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * result.melt_kg_m2;
    result.column = complete_column_step(tile.column, system, tile.before, surface_K, sink_W_m2,
                                         source_W_m2 - exchange_W_m2_K * surface_K);
    result.column.surface_slope_K_m2_W = clamped ? 0.0 : system.slope_K_m2_W(surface_K);
    return result;
}

LandSnowStepResult step_land_tile(const ColumnProperties& ground, ColumnState state,
                                  double snow_kg_m2, double insolation_W_m2,
                                  double precipitation_kg_m2_s, double grey_emissivity,
                                  double dt_s) {
    return solve_land_tile(prepare_land_tile(ground, state, snow_kg_m2, insolation_W_m2,
                                             precipitation_kg_m2_s, grey_emissivity, dt_s));
}

}  // namespace planetsim
