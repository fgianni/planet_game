# Task M8-07 — Sub-grid condensation

- **Milestone:** P0 / M8, reopened (ADR-0025)
- **Status:** in progress (2026-10-19): ADR-0025 §11 (non-precipitating
  cloud) implemented; stopped again for a decision: the model's air is
  nearly saturated in both modes (Results, finding 3)
- **Governing decisions:** ADR-0025 §4.1, §4.2 (b_sub), V1, V2

## Done

- **`sim/planet/atmosphere/subgrid_cloud.hpp`:** Smith's triangular
  closed forms: the equilibrium cloud water q_l*(s, b), the cloudy area
  (dq_l*/ds) and dq_l*/db; b ≤ 0 is saturation adjustment.
- **The column** (`atmosphere_column.hpp`), with moist convection and
  β = `AtmosphereParameters::cloud_subgrid_width` > 0:
  - the solve condenses each layer towards q_l* of its total water
    q + q_l at the new temperatures; its Jacobian, the transport's
    moist-enthalpy slope and the surface evaporation's condensing share
    follow (dC/dq_t = the cloudy area, dC/dT from s and b = β q_sat);
  - the pass after convection condenses towards q_l* of the total water
    held so far (the step's start, the solve's condensate and
    Betts–Miller's drying);
  - cloud water evaporates towards q_l* (`evaporation_to_equilibrium`),
    f_e of the gap per step.
  - β = 0 takes the old arithmetic: a two-year L3 run ends with the same
    state hash (`0e0e6e4ce106b8a6`) before and after.
- `planet_cli thermal --cloud-subgrid-width β` and a report line with the
  year's cover, cover above the bottom layer, mean optical depth, cloud
  radiative effects, cloud water and the zonal cover by 10° band
  (`AnnualSurfaceSummary`, `SurfaceEnergyDiagnostics`).

## Results

- **V2** (`tests/unit/test_subgrid_cloud.cpp`):
  - the closed forms equal the triangle's integrals (water to 8e-12 b,
    area to 1e-15);
  - the derivatives match finite differences to 1e-6;
  - q_l* is continuous at Q = −1, 0, 1 and rises with s;
  - b → 0 is saturation adjustment;
  - at β → 0 the evaporation towards equilibrium is the evaporation to
    saturation.
- **V1:** the column solve closes its water to 1e-11 at 10 minutes and a
  month, over an evaporating surface, in stable and convecting columns.
  A stable column at 90 % holds cloud water only with β > 0 (196 g/m² at
  β = 0.2, against none).
- All 98 tests pass.
- **Finding 1 — β changes nothing in climate mode.** L4, three years: the
  zonal cover is the same to two decimals at β = 0, 0.2 and 0.4 (global
  0.664, above the bottom layer 0.017, optical depth 4.3).
  - The cause is structural. Under ADR-0023 §12 and §14 a layer's cloud
    water settles at its condensation rate times τ_a.
  - A month's condensation is fixed by the column's water balance, since
    what evaporates and converges rains out. The PDF moves the humidity
    at which a layer starts condensing, not how much a month condenses.
  - So the cloud water, and through §13 the cover, follows the rain, and
    ADR-0025 §4.1 as written cannot move it.
- **Finding 2 — L6 breaks with β = 0.2.** One year from the initial
  state: the coupled transport does not converge, and its source
  (−1 004 W/m²) takes a column's bottom layer to 5 K. This is ADR-0024's
  failure class. L3 and L4 run (no failed months, closure 3e-6 and 5e-6).
  At β = 0 L6 runs (20 s for the year).

- **ADR-0025 §11.2.1, non-precipitating cloud** (`ColumnMoisture::
  non_precipitating_path_kg_m2`, `AtmosphereParameters::
  cloud_non_precipitating_path_kg_m2`, `planet_cli thermal
  --cloud-non-precipitating-path`): autoconversion acts above
  q_th = A W_np / m, A the layer's sub-grid cloudy area at the
  microphysics' state. W_np = 0 is §12's threshold, bit for bit.
- **Finding 3 — non-precipitating cloud overcasts the planet.** L4, three
  years, β = 0.2: W_np = 0.03 kg/m² gives cover 0.96 in every band (high
  0.44, longwave effect +116 W/m²); W_np = 0.1 gives 1.0 everywhere. The
  layers' sub-grid area is near 1 almost everywhere, because the air is
  near saturation. Relative humidity by layer, L3 (share of the time
  saturated in brackets):

  | Band | Climate, year 11: bottom / middle / top | Reference, days 730–790: bottom / middle / top |
  |---|---|---|
  | 60–90° N | 0.91 (0.43) / 0.83 (0.44) / 0.92 (0.87) | 0.95 (0.57) / 0.94 (0.65) / 0.98 (0.75) |
  | 30–60° N | 0.84 (0.11) / 0.59 (0.10) / 0.96 (0.89) | 0.96 (0.54) / 0.84 (0.25) / 0.93 (0.44) |
  | 0–30° N | 0.82 (0.01) / 0.43 (0.02) / 0.98 (0.85) | 0.92 (0.27) / 0.89 (0.09) / 0.95 (0.43) |
  | 0–30° S | 0.82 (0.02) / 0.46 (0.04) / 0.97 (0.85) | 0.92 (0.30) / 0.89 (0.08) / 0.94 (0.46) |
  | 30–60° S | 0.82 (0.09) / 0.61 (0.08) / 0.92 (0.79) | 0.95 (0.50) / 0.88 (0.25) / 0.91 (0.44) |
  | 60–90° S | 0.92 (0.54) / 0.78 (0.51) / 0.85 (0.76) | 0.98 (0.68) / 0.96 (0.65) / 0.97 (0.66) |

  Earth's layer means are about 0.7–0.8 at the bottom and 0.3–0.6
  above. Both modes are too moist, reference mode more so, and climate
  mode's top layer is saturated most of the time. A cloud scheme driven
  by humidity cannot place the clouds until the humidity is right.

- **The vapour budget** (`SurfaceEnergyDiagnostics::vapour_budget`, by
  30° band and layer; `AtmosphereDynamicsDiagnostics::advection_by_band_kg`;
  per-layer evaporation in `ColumnSolveResult`), diagnostics only: the
  state hash is unchanged. L3, tropics 0–30° N, mm/day:

  | | Climate (year 11) | Reference (days 730–790) |
  |---|---|---|
  | Lifted into the middle layer | 0.12 | 1.83 |
  | Lifted into the top layer | 0.01 | 0.37 |
  | Condensed in the bottom layer / its convective drying | 5.39 / 0.38 | 1.07 / 2.91 |
  | Rain re-evaporation (all layers) | 0.01 | 0.03 |
  | Cloud-water evaporation (all layers) | 0.00 | 0.68 |
  | Vapour column (kg/m²) | 38 | 80 (Earth about 45) |

  - Re-evaporation does not moisten the layers in either mode.
  - Climate mode's water hardly leaves the bottom layer. The month's
    evaporation condenses there in the solve (ADR-0023 §11.1), and
    Betts–Miller never moistens a layer (§4.2.5), so convection carries
    no water up. Reference mode lifts it by resolved ascent.
  - Both modes' upper layers saturate because condensation is their only
    sink.
- **Five layers** (climate mode, L3, year 11): relative humidity by layer,
  bottom to top, tropics 0.82, 0.39, 0.47, 0.86, 0.64 (the fourth layer
  saturated 36–49 % of the time); mid-latitudes 0.85, 0.44, 0.64, 0.93,
  0.78; poles 0.93, 0.62–0.65, 0.79–0.86, 0.83–0.91, 0.79–0.85. The lower
  and middle troposphere is dry, as Earth's is. Most of three layers'
  near-saturation is their vertical resolution.

## Open (for the decision)

- ADR-0025 needs revising before more work. With the cloud water at
  C τ_a, the sub-grid distribution can change the clouds only if part of
  the cloud is the distribution's own, non-precipitating equilibrium. One
  way: the cloud water relaxes towards q_l* plus the precipitating part
  C τ_a. That makes the monthly-mean nonlinearity ADR-0023 §13 warned of
  central, and with it §4.2's b_eddy, which is what is meant to make a
  monthly mean's equilibrium match reference mode's.
- L6's convergence with β > 0.
