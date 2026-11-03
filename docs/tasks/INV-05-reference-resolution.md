# Investigation INV-05 — Reference mode's resolution dependence

- **Milestone:** P0, after INV-04 and before M9
- **Status:** started 2026-11-03 (with the user)
- **Predecessor:** [INV-04](INV-04-v8-transport-and-radiation.md), whose V8 has no
  converged reference above L5
- **Governing decisions:** ADR-0011 (winds: the reference dynamical core),
  ADR-0023 §16 (V8 at L4), §16.5–§16.6 (V8 at L5 and L6)
- **Scope:** why reference mode's climate changes with the mesh level, and
  whether a level exists where it settles. No solver, preset or gate
  changes without a decision; M9 is outside this work.

## What is known (from INV-04)

| | Reference L4 | Reference L5 | Reference L6 |
|---|---:|---:|---:|
| Mean surface (K) | 290.26 | 287.50 | — |
| P2 equator-to-pole (K) | 48.8 | 60.1 | — |
| Tropical bands 25° S–25° N | 297–304 K | the same within 1 K | — |
| Poleward of 40° | | 5–12 K colder, more ice and snow | — |
| Run | 13 years | 13 years | stopped in year 2: winds past 150 m/s |

(ADR-0028 at c = 50 m/s for L4 and L5, at c = 25 m/s for L6; the waves are
climate mode's only, but the radiation pair differs between the two.)

- The tropics agree; the change is at high latitudes, with the
  cryosphere, a weaker greenhouse and less cloud there.
- The dynamics' grid-scale damping is already per grid (the
  hyperviscosity from a fixed damping time at the grid scale).

## Plan

1. **Spin-up trajectories by level:** from the same initial state, month
   by month, the maximum wind, the jet's speed and latitude, the P2
   difference, the polar caps' temperature, sea ice and the global mean,
   at L3 and L4 (this machine) and L5 and L6 (the RTX 5070 Ti machine; L6
   until it stops), two years each. They separate a spin-up transient
   (the winds peak early, while the hot start cools), a level-dependent
   equilibrium (each level settles elsewhere) and a polar feedback (the
   polar cooling leads and the winds follow).
2. Then, by what 1 shows: the dynamics' damping and friction against
   resolution, the resolved eddy heat transport, or the cryosphere's
   growth.

## What depends on resolution in reference mode (code read, 2026-11-03)

| Term | Scaled with resolution? | Expected effect at L5–L6 |
|---|---|---|
| ∇⁴ hyperviscosity (`hyperviscosity_for_damping_time`) | yes: the grid-scale mode decays in 8 h at every level, ν ∝ d̄⁴ | a fixed physical wavelength is damped 16 times less per level: stronger resolved eddies (usual for the scheme) |
| Surface drag | no dependence: bulk coefficients (1.5e-3 ocean, 4e-3 land) | — |
| Sub-step count (`substep_count`) | yes: CFL on the mesh's smallest spacing, assuming winds ≤ 150 m/s | the guard was raised from 100 to 150 m/s once already, after a Held–Suarez spin-up reached 100.85 m/s (ADR-0011 §13) |
| **Orography** (`dynamics_orography_max_step_m`) | **no: at most 800 m between neighbours**, a height, not a slope | the steepest slope allowed doubles per level: steeper mountains, stronger forced flow |
| **Resolved condensation** (τ_c = 12 h, with Betts–Miller's τ_bm = 2 h) | **no:** fixed times | finer cells resolve stronger ascent; latent heat released in one cell drives more ascent there (grid-point storms). The surface step's own note: with τ_c = 3 h, winds passed 150 m/s within 3 hours at L3. Reference mode's convective share falls from 0.21 at L4 to 0.08 at L5 |

Predictions for the trajectories:
- **orography:** the strongest winds over steep terrain, early, whatever the climate;
- **grid-point storms:** the strongest winds where condensation is intense (tropics, storm tracks), growing with level.

The trajectory probe records the strongest wind's speed, not its place: if
the winds are the issue, the next probe records where (latitude, terrain,
condensation).
