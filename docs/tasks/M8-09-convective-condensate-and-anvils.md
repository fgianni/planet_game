# Task M8-09 — Convective condensate and anvils

- **Milestone:** P0 / M8, reopened (ADR-0025)
- **Status:** in progress (2026-10-19): implemented with e_d = 0 in the
  preset (its value is the refit's, task M8-10); V4 passes; the modes'
  convective shares being measured
- **Governing decisions:** ADR-0025 §4.3 as amended by §13, V1, V4

## Done

- **The convective condensate** (`solve_atmosphere_column`, with moist
  convection): a layer's Betts–Miller drying V_k and ŵ_k = w_k / w_0 of
  its condensation in the solve and after convection (§13).
  `ColumnSolveResult::convective_condensate_kg_kg` keeps its meaning,
  V_k (the vapour budget's convective drying).
- **Detrainment:** a share e_d (`AtmosphereParameters::
  convective_detrainment_share`, `ColumnMoisture::detrainment_share`,
  default 0) of the column's convective condensate moves, as liquid, to
  the layers weighted ŵ_k (1 − ŵ_{k+1}); it joins their cloud water and
  rains through ADR-0023 §12's exact autoconversion.
  `ColumnSolveResult::cloud_source_kg_kg`,
  `convective_cloud_source_kg_kg` and `detrained_kg_m2_s` report it.
- **`cloud_microphysics`** takes each layer's whole cloud-water source and
  its convective part (it took the large-scale and convective parts).
- **Diagnostics:** `SurfaceEnergyDiagnostics::detrained_kg`;
  `AnnualSurfaceSummary` gains the year's precipitation, its convective
  part and the detrained water; `planet_cli thermal
  --convective-detrainment e_d`, and the report's `clouds` line gains
  `e_d`, `convective_share` and `detrained_share` (of the precipitation).

## Results

- **V4** (`test_cloud_water` §8, §9):
  - a deep convecting column at 10 minutes (convection to the top
    layer): with e_d = 0.5 half its convective condensate (1.85 kg/m² in
    the step) reaches the detrainment layers, the top layer's cloud water
    rising from 164 to 494 g/m²; the column's water and its moist
    enthalpy Σ (c_p T + L q) equal e_d = 0's to rounding; with e_d = 1 no
    convective water stays in the bottom layer;
  - the convective share over a month, 10-minute steps against one
    step: 0.983 and 1.0 (with §4.3's w_k: 0.47 and 1.0, the reason for
    §13).
- **V1:** the existing column-solve closures (`test_cloud_water` §7,
  `test_moist_convection`) pass unchanged; all 98 tests pass.
- **e_d = 0 changes no physics:** L4 climate mode over three years gives
  the same output to every printed digit as before. The two-year L3
  `planet_cli run` state hash changes (`0e0e6e4ce106b8a6` to
  `c06498aaf16a2a0c`) because the saved convective precipitation field
  is reclassified.
- **Climate mode, L4, ten years** (`planet_cli thermal --subdivision 4
  --years 10 --layers 3 --coupled --water --moist-convection 1
  --convective-detrainment e_d`, seed 1; not refitted):

  | e_d | Convective share | Cover | High cover | 20–40° | Tropics | Poles | Cloud LW | Surface |
  |---|---|---|---|---|---|---|---|---|
  | 0 | 0.99 (M8-05: 0.06) | 0.60 | 0.01 | 0.68–0.71 | 0.81–0.82 | 0.07–0.11 | +21 W/m² | 289.4 K |
  | 0.5 | 1.00 | 0.51 | 0.34 | 0.47–0.49 | 0.62–0.65 | 0.13–0.25 | +74 | 314.0 |
  | 1 | 1.00 | 0.50 | 0.50 | 0.46–0.52 | 0.68–0.70 | 0.08–0.19 | +72 | 314.9 |

  - Detrainment moves the clouds towards Earth's pattern: the subtropics
    to CERES's 0.4–0.5, the tropics to 0.6–0.65, high cover into §4.4's
    0.15–0.35 at e_d = 0.5.
  - The anvils are liquid cloud at the top layer's 220 K, nearly black in
    the longwave (κ_l W_c = 6.5 per cloudy layer): the cloud longwave
    effect triples and the surface warms 25 K. ADR-0023 has no ice
    clouds (§8); real anvil cirrus is ice, with larger particles and less
    water per area.
