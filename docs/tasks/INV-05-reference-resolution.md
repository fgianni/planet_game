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
| L5 | 124.0 m/s | 76–117 m/s (months 2–16) | 36–46 m/s |
| L6 | 143.0 m/s | 107–143 m/s (months 2–23); **stopped in month 24 at 150.8 m/s** | 37–55 m/s, at 72–78° S |

- **Not the hot start alone:** after month 1's peak each level settles at
  its own level of strongest wind, about 1.4 times per level (≈ 50, 70,
  100 m/s at L3, L4, L5); at L6 that reaches the 150 m/s bound, as its V8
  run did in year 2.
- **The maxima are local:** the zonal-mean jets are 20–46 m/s.
- L5 stopped at month 16 and L6 at month 3 when the RTX 5070 Ti machine
  was shut down (2026-11-03; `traj_L5_partial16.log`); L6 restarted from
  the start, and L5's 16 months are kept as they are.
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

Months 2–12 (after the hot start), the share of hourly maxima against the
share of edges (`build/inv05/where_L3.log`, `where_L4.log`):

| | L3 | L4 |
|---|---|---|
| Terrain ≥ 1 km | 16.8 % vs 13.9 % (×1.2) | 11.5 % vs 9.6 % (×1.2) |
| Step ≥ 500 m | 22.2 % vs 15.2 % (×1.5) | 13.1 % vs 9.4 % (×1.4) |
| Rain ≥ 20 mm/day | 8.8 % vs 6.5 % (×1.3) | 20.5 % vs 8.7 % (×2.3) |
| Tropics (< 30°) | 12.6 % vs 49.6 % (×0.3) | 0.9 % vs 50.0 % (×0.02) |
| Poleward of 60° | 26.3 % vs 13.3 % (×2.0) | 51.3 % vs 13.2 % (×3.9) |
| Top layer | 100 % | 99.8 % |

- **Not orography:** mountains are only mildly favoured (×1.2–1.5), the
  same at both levels; L3's months 9–12 lean more on terrain (40–48 %)
  than the rest.
- **High latitudes and rain, more so with level:** at L4 half the maxima
  are poleward of 60° and their rain association doubles from L3; the
  tropics have almost none. The zonal-mean jets are also far poleward
  (62–77° at L5 and L6). Candidates: condensation heating in high-latitude
  storms concentrated in single cells (grid-point storms), or the polar
  jets themselves sharpening as the resolution rises.
- **L5 (2026-11-03, `where_L5.log`), months 2–12:** poleward of 60°
  83.7 % of the maxima against 13.3 % of edges (×6.3; 98 % in months 6–7),
  rain ≥ 20 mm/day ×1.9, terrain ≥ 1 km ×1.3, tropics none. **The polar
  concentration grows with level (×2.0, ×3.9, ×6.3); the rain association
  does not (×1.3, ×2.3, ×1.9).** Grid-point storms are the weaker
  candidate; the polar caps are where resolution matters.

## 3. The strongest winds' structure (2026-11-03)

Every hour, the cell with the fastest top-layer wind: its speed against
its neighbours' mean (a spike or a smooth jet), its eastward share u/|V|,
its rain against its neighbours', and its latitude in 10° bins from 50°
([`tools/v8/reference_wind_structure_probe.cpp`](../../tools/v8/reference_wind_structure_probe.cpp);
reference mode leaves the circulation's vertical mass flux empty, so the
ascent ratio is not available). L3, month 1: 56 m/s, ×1.30 its neighbours,
eastward share 0.96, rain ×1.27, all equatorward of 70°: a smooth zonal jet.

Months 2–12 (`build/inv05/shape_L3.log`, `shape_L4.log`, `shape_L5.log`;
the rain ratio as a median, its mean dominated by cells whose neighbours
hardly rain):

| | L3 | L4 | L5 |
|---|---:|---:|---:|
| Fastest cell wind, top layer | 46.0 m/s | 59.8 m/s | 72.0 m/s |
| Against its neighbours' mean | ×1.32 | ×1.19 | ×1.10 |
| Eastward share u/\|V\| | 0.96 | 0.95 | 0.95 |
| Rain against its neighbours' (median) | ×1.44 | ×1.61 | ×3.51 |
| \|φ\| < 50° / 50–60 / 60–70 / 70–80 / 80–90 | 45 / 39 / 16 / 1 / 0 % | 17 / 30 / 42 / 12 / 0 % | 6 / 12 / 42 / 39 / 1 % |

- **A smooth zonal jet, not grid-point spikes:** the fastest cell is
  hardly faster than its neighbours, and less so at finer levels (×1.32 →
  ×1.10), and blows eastward (0.95).
- **The jet strengthens and moves poleward with level:** about +13 m/s
  per level, from 50–60° at L3 to 60–80° at L5 (Earth's eddy-driven jets
  are at 40–50°). Not at the pole itself (80–90°: ≤ 1 %), so not the mesh
  or Coriolis near the pole.
- **Rain under the jet sharpens at L5** (median ×3.5): the storm track
  under the jet concentrates, though it no longer holds the maxima more
  often (§2).
- **The cell winds are 25–35 % below the dynamics' edge maxima** (72 m/s
  against ≈ 100 m/s at L5): the 150 m/s bound is on the edge-normal winds,
  which carry grid-scale structure that the cell reconstruction smooths.
  At L6 the edge maxima are at 134–141 m/s in months 2–5 of the restarted
  run.

Reading: reference mode's polar caps get colder with level (P2 rises),
and the jet follows by thermal wind, stronger and farther poleward. The
question becomes the cause of the colder caps. Next: the caps' energy
budget by level, the resolved eddy heat transport across 50–70°, the top
of atmosphere and the surface (ice, snow, cloud) poleward of 60°.

## 4. The polar caps by level (2026-11-04)

L6's restarted trajectory (`build/inv05/traj_L6.log`) stopped in month 24,
a wind of 150.8 m/s against the sub-step rule's 150 m/s bound, in year 2
as its V8 run did. At the same month, the tropics hardly differ between
levels but the caps (poleward of 60°) cool by 5–10 K per level:

| Month 16 | L3 | L4 | L5 | L6 |
|---|---:|---:|---:|---:|
| Polar caps | 285.5 K | 280.0 K | 270.5 K | 264.7 K |
| Tropics (30° S–30° N) | 306.1 K | 307.0 K | 307.4 K | 307.7 K |
| P2 | 24.7 K | 32.3 K | 42.3 K | 48.6 K |
| Largest wind | 45.7 m/s | 84.8 m/s | 95.3 m/s | 119.1 m/s |

At L6 the caps are still cooling at month 23 (257.5 K). The caps' heat
budget by level (the dynamics' transport into them across 60° and 70°,
clouds, ice and snow; [`tools/v8/reference_polar_budget_probe.cpp`](../../tools/v8/reference_polar_budget_probe.cpp))
is running for 24 months at L3, L4 and L5.

