# Task M7-05 — Vapour radiation

- **Milestone:** P0 / M7 (fifth task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.6, §4.8, amended in §12
  (2026-10-09)

## Done

- **The vapour path:** `column_radiation(parameters, p_s, g, humidity)`
  in `sim/planet/atmosphere/atmosphere_column.hpp`. Each layer's depth is
  τ_d Δp / p₀ + κ_v q_k Δp / g, from its humidity at the start of the step
  (after the month's transport).
  - `AtmosphereParameters` gains `dry_optical_depth` (τ_d) and
    `vapour_absorption_m2_kg` (κ_v), both validated non-negative.
  - The surface step and the circulation's column heating
    (`compute_atmosphere_heating`) use the vapour path with
    `water_cycle`. Otherwise τ₀'s profile stays, bit for bit.
- **Provisional constants** (ADR-0021 §12): τ_d = τ₀/2, and
  κ_v = (τ₀/2) / 50 kg/m², which is 0.0144 m²/kg.

## Results

- **The vapour path's depths** (`tests/unit/test_rainout.cpp` §5):
  - with no vapour, the column's depth is τ_d p_s / p₀;
  - with vapour, it is τ_d p_s / p₀ + κ_v W, both to 1e-12;
  - the layers' emissivities follow their humidity.
- **Stability** (L3, coupled, 12 years from the M6 state; ADR-0021 §12):
  - with these constants, the climate settles near 287 K with 51 kg/m² of
    column vapour;
  - with vapour carrying 90 % of τ₀ it ran away (to 304 K, with
    357 kg/m² and rising), or dried out (262 K, 13 kg/m²).
- **Coupled test** (`test_water_transport`, second year):
  - mean 296.7 K, still warming down from its first-year peak;
  - 1.65 m/yr of precipitation, latent 130 W/m²;
  - every budget within its gate, and every solve converges.
- All 88 tests pass.

## Open for later tasks

- The refit (M7-07) fits τ_d and κ_v jointly to 288 K and 42 K. It must
  check the fitted climate's distance from the feedback's runaway and
  drying branches.
- The layers stay near RH 1, so the model holds about twice Earth's
  vapour. Clouds and moist convection (M8) are where that changes.
