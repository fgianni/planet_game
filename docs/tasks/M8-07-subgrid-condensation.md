# Task M8-07 — Sub-grid condensation

- **Milestone:** P0 / M8, reopened (ADR-0025)
- **Status:** in progress (2026-10-19): implemented and tested; two
  findings stop it for a decision (Results)
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

## Open (for the decision)

- ADR-0025 needs revising before more work. With the cloud water at
  C τ_a, the sub-grid distribution can change the clouds only if part of
  the cloud is the distribution's own, non-precipitating equilibrium. One
  way: the cloud water relaxes towards q_l* plus the precipitating part
  C τ_a. That makes the monthly-mean nonlinearity ADR-0023 §13 warned of
  central, and with it §4.2's b_eddy, which is what is meant to make a
  monthly mean's equilibrium match reference mode's.
- L6's convergence with β > 0.
