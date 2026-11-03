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
