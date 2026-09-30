# Task M4-02 — Diffusive horizontal heat transport

- **Milestone:** P0 / M4 (second task; M4-01 is complete)
- **Requested:** 2026-09-30
- **Scope:** ADR-0009 in full: the diffusion law, the coupled implicit
  step in both modes, presets, calibration of `D` together with `g`, the
  diagnostics and V1–V10. Sea ice is M4-03; the seasonal snow and ice
  experiment M4-04; ADR-0003's M4 history work M4-05.
- **Governing decisions:** ADR-0009 (all), ADR-0008 §4.3 (clamped land
  solve), ADR-0007 §4.3–4.4, ADR-0002 §4.2 and §4.6 (two-point Laplacian,
  deterministic reductions), specification §23 (calibration lessons), §24

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Parameter.** `SurfaceEnergyParameters::transport_coefficient_W_m2_K`
   (`D`, North's unit-sphere coefficient); `K = D R²` with the mesh radius.
   Dead rock and aqua planet: 0, and a zero coefficient skips the solve
   entirely (V6). Earth-like: `earth_like_transport_coefficient_W_m2_K`, a
   calibration constant with its fit record. Negative or non-finite `D`
   throws.
2. **Distribution.** The cell's source `H_c` reaches each tile as
   `H_c / (f_land + f_ocean)` per unit area, so the tile-weighted global sum
   equals `Σ A_c H_c` even though the stored fractions are `float`. The cell
   temperature is `T̄_c = (f_land T_land + f_ocean T_ocean) / (f_land + f_ocean)`.
3. **Local solves.** `column_step` gains a source term (default 0, added to
   `b`, bit-identical at 0) and a shared finishing function; `land_snow`
   splits into preparing the tile's system (albedo, precipitation phase)
   and solving it for a given source, returning the surface temperature's
   slope `dT/dH` (0 when clamped at the melting point). Tile systems are
   prepared once per step and reused by every Newton iteration.
4. **Solver** (`sim/planet/surface/heat_transport.{hpp,cpp}`, independent of
   the surface module through a per-cell response callback):
   - Newton on the sources as ADR-0009 §4.3, from the explicit estimate,
     with a fixed iteration count chosen from V7 and recorded;
   - each iteration a Jacobi-preconditioned conjugate-gradient solve,
     relative tolerance 1e-10, iteration cap 2,000, dot products through
     `reduce_deterministic_blocks`;
   - every iterate is projected above a per-cell floor at which each tile's
     surface root is at least 100 K, so a local solve always has a positive
     root (recorded as a refinement of ADR-0009 §4.3);
   - the applied transport is `K ∇² T̄` of the last iterate's cell
     temperatures, and the tiles take their final step with it.
5. **Budgets.** Per-tile closure becomes
   `storage + latent = Δt (absorbed − emitted + H)`; the global diagnostics
   add the transport sum `Σ A H` (≈ 0), `Σ A |H|`, the dissipation
   `Σ A H T̄` (≤ 0), the consistency residual, Newton and CG counts, and the
   northward transport across every 10° from 80° S to 80° N in watts.
6. **Calibration.** L5, seed 1, earth_like, 60 spin-up years, no
   precipitation (so no snow), no sea ice yet: `D` to a 5.5 PW peak
   annual-mean poleward transport (mean of both hemispheres' peaks) and
   `g` to 288 K, fitted alternately until both hold (0.05 PW, 0.05 K).
   Refitted in M4-04 once sea ice exists. `planet_cli thermal` gains
   `--transport D` and `--calibrate-transport PW` (with `--calibrate K`).

## 2. Steps

1. Local-solve refactors with source and slope; bit-identity tests.
2. The implicit transport solver with unit tests on the mesh: conservation,
   down-gradient sign, a pure-diffusion case against the linear solve.
3. Integration into `step_surface_energy`; physics tests V1–V8 (V4: the P2
   response of an ocean planet at L3–L6 under a single infinitely long step).
4. Calibration, CLI, performance (V10) and the seasonal land-snow
   measurement recorded for V9.
5. Documentation: ADR-0009 implementation record, README, audit, task
   index, status.

## 3. Acceptance

ADR-0009 §5 V1–V10 with the gates stated there, plus the unchanged M0–M4-01
suite, warnings as errors, the floating-point policy and the registry
check.
