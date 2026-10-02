#pragma once

#include "sim/planet/surface/column_step.hpp"
#include "sim/planet/surface/surface_materials.hpp"

namespace planetsim {

// The ocean tile with zero-layer sea ice (ADR-0008 §4.4 and §10, tasks M4-03
// and M5-03).
//
// Open water is the ADR-0007 column; a mixed layer that would cool below the
// seawater freezing point T_f is held there and the deficit freezes ice.
// Under ice the mixed layer stays at T_f and the deep layer's exchange with
// it is the ocean heat flux F_o to the ice. Ice thinner than h_r is floes of
// thickness h_r over the fraction c = m / (ρ_i h_r) of the tile (the cover at
// the start of the step holds for the step); the rest is leads of open
// water at T_f, whose net heat Q_L freezes or melts ice. The floes (no heat
// capacity) conduct k_i (T_f − T_i) / h_f from their base to their surface,
// whose temperature T_i balances sunlight, emission, conduction and the
// external source, held at the melting point T_m if it would exceed it (the
// surplus melts the top). The new mass solves the backward-Euler growth
// equation, whose residual increases with the mass: one root, stable for
// any step. Where it is not positive all the ice melts and the heat left
// over warms the mixed layer, the surfaces keeping the step's balance, so
// the tile's response is continuous.
struct OceanTileSystem {
    ColumnProperties column;   // the ocean, with the albedo of the ice at the start
    ColumnSystem open;         // the open-water system of that column
    ColumnState before;        // mixed and deep layers
    double ice_kg_m2 = 0.0;    // before the step
    double dt_s = 0.0;
    double cover = 0.0;                  // c₀ = min(1, m / (ρ_i h_r))
    double floe_absorbed_W_m2 = 0.0;     // (1 − α_ice) Q per floe area
    double lead_absorbed_W_m2 = 0.0;     // (1 − α_ocean) Q per lead area
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
    // The tile's surface temperature: c₀ T_i + (1 − c₀) T_f under ice, or the
    // mixed layer; column.surface_slope_K_m2_W is its derivative.
    double radiating_K = 0.0;
    double emitted_slope = 0.0;     // d(emitted)/d(source)
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
