# Task M8-01 — Cloud water state

- **Milestone:** P0 / M8 (first task)
- **Status:** complete (2026-10-16)
- **Governing decisions:** ADR-0023 §4.1, §4.7 (accepted 2026-10-16), V1, V10; ADR-0003 §3.6

## Done

- **Fields** (registry group `0x0008`): `atmosphere_cloud_water_kg_kg`
  (slow, N layers like the atmosphere, float64), and the derived
  `cloud_fraction` (N layers), `convective_precipitation_kg_m2_s`,
  `cloud_shortwave_effect_W_m2` and `cloud_longwave_effect_W_m2`, which
  later tasks fill.
- **PSNAP schema 7** with the step 6 → 7, "no cloud water"
  (`initialise_cloud_water`, `sim/planet/atmosphere/water.{hpp,cpp}`): every
  layer's cloud water zero, as the M7 world holds no condensate. The cloud
  water must have the atmosphere's layer count, as every scenario-layered
  field. `initialise_water` ends with it, so every new run, and every older
  file through 5 → 6, starts without clouds.
- **Golden save** `tests/data/golden/psnap-v7-l0.psnap` (three layers of
  cloud water, 0–1 g/kg from `planet_cli snapshot write`); every older file
  now also passes through 6 → 7.
- The registry baseline (`tools/ci/field_registry_baseline.tsv`) gains the
  five fields.

## Results

- **V10** (`test_golden_snapshot`, `test_snapshot_file`): v1–v6 load through
  6 → 7 with no cloud water; v7 loads as written and steps ten years; a
  schema 6 file may not carry the cloud water; the round trips, cloud water
  included, are byte-identical.
- **V1:** nothing makes or reads cloud water yet, so the water budgets and
  the climate are unchanged; the slow-state hashes change because a field
  was added. The budgets count cloud water from M8-03, when it first holds
  any.
- All 92 tests pass; the field-registry check passes (39 fields, 1 retired).
