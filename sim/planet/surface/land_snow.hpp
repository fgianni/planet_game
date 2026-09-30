#pragma once

#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/surface_materials.hpp"

namespace planetsim {

// One step of a land tile with snow (ADR-0008 §4.3). Precipitation falls as
// snow when the surface layer starts the step at or below the melting point,
// otherwise as rain; the albedo comes from the snow at the start of the step;
// a surface that would warm past the melting point while snow remains is
// held there and the surplus melts snow. Rain and meltwater run off.
struct LandSnowStepResult {
    ColumnStepResult column;       // storage_change excludes the latent term
    double snow_kg_m2 = 0.0;       // after the step
    double snowfall_kg_m2 = 0.0;
    double rain_kg_m2 = 0.0;
    double melt_kg_m2 = 0.0;
    double latent_J_m2 = 0.0;      // L_f · melt: energy taken by melting
    double albedo = 0.0;

    [[nodiscard]] double runoff_kg_m2() const noexcept { return rain_kg_m2 + melt_kg_m2; }
};

// The albedo of a land tile with snow W: the ground's, blended with snow by
// the cover W / (W + W_m).
[[nodiscard]] double snow_covered_albedo(double ground_albedo, double snow_kg_m2) noexcept;

// Throws std::invalid_argument for negative or non-finite snow or
// precipitation, and as step_column does.
[[nodiscard]] LandSnowStepResult step_land_tile(const ColumnProperties& ground, ColumnState state,
                                                double snow_kg_m2, double insolation_W_m2,
                                                double precipitation_kg_m2_s,
                                                double grey_emissivity, double dt_s);

}  // namespace planetsim
