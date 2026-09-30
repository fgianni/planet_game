#pragma once

#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/surface_materials.hpp"

namespace planetsim {

// The ocean tile with zero-layer sea ice (ADR-0008 §4.4, task M4-03).
//
// Open water is the ADR-0007 column; a mixed layer that would cool below the
// seawater freezing point T_f is held there and the deficit freezes ice.
// Under ice the mixed layer stays at T_f, the deep layer's exchange with it
// is the ocean heat flux F_o to the ice base, and the ice (no heat capacity)
// conducts k_i (T_f − T_i) / h from its base to its surface, whose
// temperature T_i balances sunlight, emission, conduction and the external
// source, held at the melting point T_m if it would exceed it (the surplus
// melts the top). The new thickness solves the backward-Euler growth
// equation, stable for any step.
struct OceanTileSystem {
    ColumnProperties column;   // the ocean, with the albedo of the ice at the start
    ColumnSystem open;         // the open-water system of that column
    ColumnState before;        // mixed and deep layers
    double ice_kg_m2 = 0.0;    // before the step
    double dt_s = 0.0;
    // The last thickness this tile solved for: the next solve of the same
    // step starts there (the transport solve re-solves every tile with
    // nearby sources). Only a starting point: the root does not depend on
    // it beyond the solve's tolerance, and each cell's calls come in a fixed
    // order.
    mutable double thickness_guess_m = 0.0;
};

struct OceanTileResult {
    // state: {mixed layer, deep layer}; emitted, source and slope refer to
    // the radiating surface (the ice surface over ice); storage_change
    // excludes the latent term.
    ColumnStepResult column;
    double radiating_K = 0.0;       // ice surface, or the mixed layer
    double ice_kg_m2 = 0.0;         // after the step
    double frozen_kg_m2 = 0.0;
    double melted_kg_m2 = 0.0;
    double latent_J_m2 = 0.0;       // L_f · (melted − frozen)
    double ocean_heat_flux_W_m2 = 0.0;   // F_o into the ice base (0 over open water)
    double albedo = 0.0;
};

// α_ocean + (α_ice − α_ocean) · min(1, h / h_r), h = m / ρ_i.
[[nodiscard]] double sea_ice_covered_albedo(double ocean_albedo, double ice_kg_m2) noexcept;

// Throws std::invalid_argument for negative or non-finite ice, and as
// step_column does.
[[nodiscard]] OceanTileSystem prepare_ocean_tile(const ColumnProperties& ocean, ColumnState state,
                                                 double ice_kg_m2, double insolation_W_m2,
                                                 double grey_emissivity, double dt_s);

// Solves a prepared tile with an external source s − γ·T in the equation of
// its radiating surface (see solve_column_step).
[[nodiscard]] OceanTileResult solve_ocean_tile(const OceanTileSystem& tile,
                                               double source_W_m2 = 0.0,
                                               double exchange_W_m2_K = 0.0);

// The source below which the tile's radiating surface would fall under
// `lowest_K` without the external source's exchange (a floor for the
// transport solve, ADR-0009 §9).
[[nodiscard]] double ocean_tile_source_floor_W_m2(const OceanTileSystem& tile, double lowest_K);

}  // namespace planetsim
