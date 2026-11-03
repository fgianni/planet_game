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

## 1. Spin-up trajectories (2026-11-03)

From the same initial state, reference mode month by month
([`tools/v8/reference_trajectory_probe.cpp`](../../tools/v8/reference_trajectory_probe.cpp);
`build/inv05/traj_L3.log`, `traj_L4.log` here; L5 and L6 on the RTX 5070 Ti
machine). The largest wind of any step:

| Level | Month 1 (the hot start) | Months 2–24 | Southern zonal-mean jet |
|---|---:|---|---|
| L3 | 95.5 m/s | 43–58 m/s | 21–34 m/s |
| L4 | 106.9 m/s | 59–85 m/s | 26–35 m/s |
| L5 | 124.0 m/s | 89–117 m/s (months 2–6) | 39–46 m/s |
| L6 | 143.0 m/s | (running) | 41 m/s (month 1) |

- **Not the hot start alone:** after month 1's peak each level settles at
  its own level of strongest wind, about 1.4 times per level (≈ 50, 70,
  100 m/s at L3, L4, L5); at L6 that reaches the 150 m/s bound, as its V8
  run did in year 2.
- **The maxima are local:** the zonal-mean jets are 20–46 m/s.
- The P2 difference also grows with level (after two years about 29 K at
  L3 and 37 K at L4; 48 K at L5 after six months), all levels still cooling
  from the start (the tropics 319 → 305 K in two years).

## 2. Where the strongest winds are (2026-11-03)

Every hour, the edge and layer with the largest normal wind, against the
same shares over all edges
([`tools/v8/reference_wind_location_probe.cpp`](../../tools/v8/reference_wind_location_probe.cpp)).
L3, first two months: always in the top layer; over terrain ≥ 1 km 10–13 %
of the maxima against 14 % of edges, over steps ≥ 500 m 12–20 % against
15 % (no preference for mountains); in rain ≥ 20 mm/day 23–25 % against
11–14 % (about twice as likely); poleward of 60° 29–39 % against 13 %.
L4 and L5 follow.
