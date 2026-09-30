# Task M4-01 — Cryosphere state and land snow

- **Milestone:** P0 / M4 (first task)
- **Requested:** 2026-09-30
- **Scope:** ADR-0008 §4.1 (both reservoirs and the precipitation forcing),
  §4.2 (constants), §4.3 (land tile), §4.5 (budgets, land terms), §4.6 (PSNAP
  schema 4). Sea-ice thermodynamics (§4.4) are M4-02; the sea-ice field is
  added here so that M4 needs one schema change, and stays zero until M4-02.
  Seasonal experiment, climatology, albedo diagnostics and the `g` refit are
  M4-03; ADR-0003's M4 history work is M4-04.
- **Governing decisions:** ADR-0008 (§4.1–4.3, §4.5, §4.6), ADR-0007 §4.3–4.4
  (column step, budget), ADR-0003 §3.6 (migration), ADR-0002 §4.4 (precision),
  specification §13 M4

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Fields.** `land_snow_water_equivalent_kg_m2` 0x0004'0001 and
   `sea_ice_mass_kg_m2` 0x0004'0002 (`float64`, cell, slow);
   `prescribed_precipitation_kg_m2_s` 0x0004'0003 (`float32`, cell, derived,
   in `ForcingState`, zero unless a test or scenario sets it). All
   baselined in `tools/ci/field_registry_baseline.tsv`.
2. **Column system.** `column_step` exposes the eliminated system
   (`a`, `b`, the radiative factor, the lower-layer update) and a surface
   solve that accepts an extra constant sink in W/m². `step_column` is
   rewritten on top of it and must stay bit-identical (ADR-0008 V6).
3. **Land step** (ADR-0008 §4.3), per tile, with snow `W` and precipitation
   `P`:
   1. phase from the surface layer at the start of the step: `T_s ≤ T_m`
      gives snowfall `P Δt` (added to `W` before the solve), otherwise rain
      (runoff);
   2. albedo from `W` at the start of the step, before snowfall:
      `α = α_ground + (α_snow − α_ground) · W / (W + W_m)`;
   3. the ADR-0007 solve with that albedo; if snow is present and the new
      surface temperature exceeds `T_m`, the surface is held at `T_m`, the
      ground layer updated with it, and the surplus
      `b − a T_m − r T_m⁴` (W/m², positive by construction) melts
      `surplus · Δt / L_f`;
   4. if that exceeds `W`, all snow melts and the solve is repeated without
      the clamp and with the constant sink `L_f W / Δt`; the root is above
      `T_m` by construction;
   5. meltwater and rain leave as runoff (a diagnostic until M9).
4. **Budgets.** Energy closure becomes
   `storage + L_f · melt = Δt (absorbed − emitted)` per tile and globally,
   with the ADR-0007 V2 gate. Water: `ΔW = snowfall − melt` per tile, and
   globally `Δ(Σ A W) = Σ A (snowfall − melt)`, gate 1e-12 relative plus
   `4ε · Σ A W`. Diagnostics report snowfall, rain, melt, runoff, stored snow
   and the latent energy.
5. **Snapshots.** PSNAP schema 4 stores the two reservoirs. The core reader
   stays domain-agnostic: `SnapshotMigration::initialise_schema_4_fields`
   is required for files older than 4 and throws when missing; the planet
   layer's `surface_energy_migration` supplies it, declaring both
   reservoirs zero (ADR-0008 §4.6). The v1–v3 golden files load through the
   chain; a v4 golden file is added with synthetic non-negative reservoirs.
6. **CLI.** `planet_cli thermal --precipitation KG_M2_S` sets a uniform
   prescribed precipitation (default 0) and prints the snow budget.

## 2. Steps

1. Constants and the column-system refactor; `step_column` bit-identical.
2. The land snow step with unit tests: clamped melt against the closed form,
   complete melt, snowfall and rain phases, per-tile energy and water
   closure, the invariant, and the no-snow case bit-identical to
   `step_column`.
3. Fields, `ForcingState`, the mesh-wide step and diagnostics; physics tests
   with prescribed snowfall on an Earth-like planet: global energy and water
   closure every step (climate and reference), worker bit-identity, the
   albedo-feedback sign for snow (ADR-0008 V5), and zero precipitation
   leaving M3 results unchanged.
4. PSNAP schema 4, the migration hook, golden v4; ADR-0008 V9.
5. CLI option and documentation (ADR-0008 implementation record, README,
   audit, task index, status).

## 3. Acceptance

ADR-0008 V1, V2, V4, V5 (snow), V6, V8 and V9 for the land tile, plus the
unchanged M0–M3 suite, warnings as errors, the floating-point policy and the
registry check.
