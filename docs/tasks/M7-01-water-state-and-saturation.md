# Task M7-01 — Water state and saturation

- **Milestone:** P0 / M7 (first task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.1–4.2 (accepted 2026-10-09), V1, V10; ADR-0003 §3.6

## Done

- **Fields** (registry group `0x0007`): `atmosphere_specific_humidity_kg_kg`
  (slow, N layers like the atmosphere, float64), `land_surface_water_kg_m2`
  (slow, the bucket, float64), and the derived `precipitation_kg_m2_s`,
  `evaporation_kg_m2_s` and `runoff_kg_m2_s`, which later tasks fill.
- **PSNAP schema 6** with the step 5 → 6, "water vapour and the land bucket"
  (`initialise_water`, `sim/planet/atmosphere/water.{hpp,cpp}`): each layer
  at 60% relative humidity of its temperature at σ_k p_s, the bucket half
  full (75 kg/m², W_max = 150 mm). The humidity must have the atmosphere's
  layer count, as every scenario-layered field.
- `initialise_atmosphere` ends with `initialise_water`, so every new run and
  the 4 → 5 migration have the water too (the 5 → 6 step then sets the same
  values).
- **Saturation** (`sim/planet/atmosphere/saturation.{hpp,cpp}`): e_s by
  Bolton (1980) at and above 0 °C and Murphy and Koop (2005) below, their
  slopes, q_sat and its slope at fixed p, L_v and L_s.
- **Golden save** `tests/data/golden/psnap-v6-l0.psnap`; every older file
  now also passes through 5 → 6.

## Results

- **V1** (`tests/unit/test_saturation.cpp`): e_s within 0.2% of the
  reference tables from −60 to 40 °C (Bolton is 0.14% high at 40 °C); the
  branches meet at 0 °C to 1e-4; the slopes agree with central differences
  to 1e-6; q_sat(20 °C, 1000 hPa) = 14.7 g/kg.
- **V10** (`test_golden_snapshot`, `test_snapshot_file`): v1–v5 load through
  5 → 6 with the declared values exactly; v6 loads as written and steps ten
  years; a schema 5 file may not carry the water; the round trips are
  byte-identical.
- Nothing reads the water yet, so the climate is unchanged; the slow-state
  hashes change because two fields were added.
