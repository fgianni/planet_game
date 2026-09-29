# Task M3-02 — Surface energy columns and the first thermal planet

- **Milestone:** P0 / M3 (second task; M3-01 is complete)
- **Requested:** 2026-09-29
- **Scope:** ADR-0007 in full: land and ocean two-layer columns, surface
  longwave with the grey layer, the backward-Euler step in both modes, the
  four slow temperature fields, PSNAP schema v2 with the v1 → v2 migration,
  and experiments A (dead rock) and B (aqua planet). No horizontal transport,
  no snow or ice (M4), no hydrology.
- **Governing decisions:** ADR-0007 (all), ADR-0006 §4 (sub-steps and their
  forcing), ADR-0005 (tiles, hypsometry), ADR-0003 §3.4 and §3.6 (snapshots,
  migration), ADR-0002 §4.4 and §4.6 (precision, determinism), specification
  §13 M3, §13.1, §23, §24

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Materials** (SI, from standard textbook values of conductivity `k` and
   volumetric heat capacity `ρc`; e.g. Oke, *Boundary Layer Climates*, and
   Hartmann, *Global Physical Climatology*). Land layers are derived, not
   chosen: the surface layer is the diurnal skin depth `√(2κ/ω_day)` and the
   ground layer the annual one `√(2κ/ω_year)`, with `κ = k/ρc` and the
   planet's own synodic day and orbital year; the conductance between them is
   `k` over the distance between layer centres.

   | Material | k (W/m/K) | ρc (J/m³/K) | Albedo | Emissivity | Earth-derived layers |
   |---|---|---|---|---|---|
   | Rock | 2.5 | 2.2e6 | 0.25 | 0.95 | 0.18 m / 3.38 m, C 3.9e5 / 7.4e6, 1.41 W/m²/K |
   | Dry soil | 0.3 | 1.3e6 | 0.30 | 0.95 | 0.08 m / 1.52 m, C 1.0e5 / 2.0e6, 0.37 W/m²/K |
   | Wet soil | 1.5 | 2.9e6 | 0.15 | 0.97 | 0.12 m / 2.28 m, C 3.5e5 / 6.6e6, 1.25 W/m²/K |

   The ocean column is the prototype's calibrated one: mixed layer
   2.9e8 J/m²/K (~70 m), deep layer 2.5e9 J/m²/K, exchange 0.7 W/m²/K,
   albedo 0.06, emissivity 0.97.
2. **Presets.** `dead_rock`: rock, grey layer `g = 0`. `aqua_planet`: grey
   layer `g = 0` (only the ocean tile has area). `earth_like`: dry soil and
   the calibrated `g` of step 3. The A/B inertia comparison uses `g = 0` on
   both sides so that only the surface differs.
3. **Solver.** For each tile, eliminate the deep layer (linear in the
   backward-Euler system) and solve the remaining scalar equation
   `a·x + βεσ·x⁴ = b` (β = 1 − g/2) by Newton from
   `x₀ = min(b/a, (b/βεσ)^¼)`, which lies at or above the root, so the
   iteration decreases monotonically. A fixed 10 iterations; the residual is
   reported. Solve and budget in `double`; the stored surface and ground
   temperatures are `float`, the deep ocean `double` (ADR-0007 §4.1).
   Budget closure (V2) is evaluated on the `double` solution; the `float`
   storage rounding is reported separately.
4. **Forcing.** Climate mode uses `substep_mean_insolation_W_m2`; reference
   mode the instantaneous insolation at the step's midpoint tick. Both tiles
   of every cell are stepped, whatever their area.
5. **Snapshots.** PSNAP schema v2 stores the six slow fields. The core
   reader stays domain-agnostic: it accepts a migration hook for fields that
   a v1 file lacks and throws if a v1 file is read without one. The planet
   layer provides the hook, which initialises the four temperatures to the
   closed-form equilibrium of ADR-0007 §4.5. The v1 golden file stays and must
   load through the hook; a v2 golden file is added.
6. **Field IDs:** `land_surface_temperature_K` 0x0003'0001,
   `land_ground_temperature_K` 0x0003'0002,
   `ocean_mixed_layer_temperature_K` 0x0003'0003 (all `float`, cell),
   `ocean_deep_temperature_K` 0x0003'0004 (`double`, cell); all slow.

## 2. Steps

1. Materials, derived layers and the single-column backward-Euler solver,
   with unit tests for V1–V4.
2. Registry fields and baseline, `SlowState` members, closed-form
   initialisation, the mesh-wide step for both modes with diagnostics; V2
   globally and V8 (workers).
3. Scheduler processes, spin-up, experiments A and B (V5–V7); calibrate `g`
   for `earth_like` and record it (specification §24).
4. PSNAP v2, the migration hook, golden v2; V9.
5. `planet_cli thermal` (experiments, spin-up, per-sub-step timing, V10) and
   documentation (ADR-0007 implementation record, README, audit, status).

## 3. Acceptance

ADR-0007 §5 V1–V10, with the gates stated there, plus the unchanged M0–M3-01
suite, warnings as errors, the floating-point policy and the registry check.
