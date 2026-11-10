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
| **Resolved condensation** (τ_c = 12 h, with Betts–Miller's τ_bm = 2 h; **corrected 2026-11-09, §15: with moist convection on, as in the preset, τ_c is not used and the column condenses the whole excess, saturation adjustment**) | **no:** fixed times | finer cells resolve stronger ascent; latent heat released in one cell drives more ascent there (grid-point storms). The surface step's own note: with τ_c = 3 h, winds passed 150 m/s within 3 hours at L3. Reference mode's convective share falls from 0.21 at L4 to 0.08 at L5 |

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
was run for 24 months at L3, L4 and L5 (`build/inv05/caps_L3.log` …
`caps_L5.log`; L6 is running on the RTX 5070 Ti machine).

Year 2 (months 13–24), poleward of 60°, W/m² of the cap:

| | L3 S | L4 S | L5 S | L3 N | L4 N | L5 N |
|---|---:|---:|---:|---:|---:|---:|
| Surface temperature (K) | 279.8 | 273.6 | 265.0 | 282.7 | 278.2 | 270.2 |
| Carried in across 60°, dry | 17.8 | 19.4 | 31.1 | 21.0 | 26.1 | 33.0 |
| Carried in across 60°, latent | 48.6 | 44.6 | 44.2 | 54.9 | 45.7 | 42.7 |
| **Carried in, total** | **66.4** | **64.0** | **75.3** | **75.9** | **71.8** | **75.7** |
| Cloud longwave effect | 38.9 | 32.0 | 22.3 | 41.4 | 34.6 | 26.4 |
| Cloud shortwave effect | −17.4 | −14.7 | −11.3 | −21.5 | −19.4 | −16.1 |
| **Cloud net** | **21.5** | **17.3** | **11.0** | **19.9** | **15.2** | **10.3** |
| Cloud cover | 0.36 | 0.32 | 0.28 | 0.39 | 0.34 | 0.30 |
| Sea-ice cover of the cap | 0.02 | 0.26 | 0.55 | 0.00 | 0.00 | 0.15 |
| Snow cover of the cap | 0.00 | 0.05 | 0.10 | 0.16 | 0.19 | 0.26 |

- **Not the transport:** the heat carried into the caps is the same
  within ±6 W/m² at all three levels, and largest at L5. The resolved
  eddies deliver as much heat to colder caps.
- **The clouds:** the caps' net cloud warming halves from L3 to L5 (21 →
  11 W/m²), mostly the longwave (39–41 → 22–26 W/m²): less cover (0.36–0.39
  → 0.28–0.30) and thinner clouds.
- **Ice and snow follow:** southern sea ice from 0.02 to 0.55 of the cap,
  snow up in both, more sunlight reflected.
- **Feedback or cause:** colder caps hold less vapour and make fewer
  clouds, so the cloud loss can follow the cooling as well as cause it.
  What sets the first difference is open: the cloud fraction's dependence
  on the cell size (the same water spread over smaller cells), or the
  ice's albedo once it grows.
- Caveat on the split: the transports are the dynamics' column changes by
  10° band (INV-04), the latent part including the RK3 clipping's added
  vapour; latent above dry at 60° is unlike Earth's, where dry dominates
  there. The totals' comparison between levels does not depend on the
  split.

**L6** (`caps_L6.log`, months 13–23; the run stopped in month 24 at
150.831332 m/s, the same wind as the trajectory run's: it reproduces)
continues the trend: surface 258.0 K S and 263.0 K N, carried in 81.0 and
85.6 W/m² (more again), cloud net 4.7 and 4.1 W/m², cover 0.23 and 0.25,
sea ice 0.70 and 0.26 of the caps, snow 0.15 and 0.36.

Next: how the cloud fraction depends on the cell's state and size (code
read), and a test that separates cause from feedback (the same run with
the caps' clouds or ice albedo held between levels).

## 5. The caps' clouds by level (2026-11-04)

Hourly over the caps, year 2 (months 13–24; L5 months 13–20)
([`tools/v8/reference_cap_clouds_probe.cpp`](../../tools/v8/reference_cap_clouds_probe.cpp);
`build/inv05/clouds_L3.log` … `clouds_L5.log`):

| | L3 S | L4 S | L5 S | L3 N | L4 N | L5 N |
|---|---:|---:|---:|---:|---:|---:|
| Cloud water path (kg/m²) | 0.0196 | 0.0191 | 0.0195 | 0.0227 | 0.0208 | 0.0225 |
| Column cover | 0.346 | 0.315 | 0.276 | 0.381 | 0.334 | 0.332 |
| Cell layers at overcast | 0.4 % | 2.3 % | 4.0 % | 0.9 % | 2.4 % | 4.3 % |
| Cloud water beyond overcast | 0.6 % | 7.3 % | 22.6 % | 1.1 % | 5.9 % | 15.4 % |
| Vapour path (kg/m²) | 15.0 | 11.1 | 6.4 | 21.5 | 17.6 | 16.0 |

- **The same cloud water, less cover:** the caps hold the same cloud water
  at every level (within 10 %), but finer cells concentrate it: overcast
  layers rise tenfold and up to a quarter of the water is beyond overcast,
  where f_k = min(1, q_l m / W_c) adds no cover and the in-cloud
  optics (reflection τ/(2 + τ), emissivity) saturate. Concave optics over
  a more variable water field radiate less for the same mean water.
- **Vapour falls, cloud water does not:** the vapour loss is the colder
  caps' feedback; the cloud water is not, so the cover loss is not
  explained by the cooling.
- **A candidate first cause:** resolved cloud water concentrates with
  resolution, the cloud scheme's overcast limit and saturating optics
  make the same water warm the caps less, the caps cool, ice and snow
  grow, P2 rises, and the polar jet strengthens to the dynamics' bound at
  L6. ADR-0023 §13.3 made the optics linear in cloud water below
  overcast so that a month's mean water radiates as its steps do; that
  holds in time but not across cell sizes once cells reach overcast.

Next: a causal test at L5, the caps' cloud optics made insensitive to the
concentration (an experiment, not a preset change), to see how much of
the 10–15 K colder caps it removes.

## 6. The overcast test (2026-11-05)

The in-cloud water path W_c raised from 0.05 to 0.2 kg/m² (the trajectory
probe's fourth argument), so few cells reach overcast at any level, for
two years at L3, L4 and L5; L5's baseline rerun for 24 months beside it
(`traj_L5.log`). If the overcast concentration is the first cause, the
caps' L3–L5 difference (≈ 15 K at month 16) shrinks with it. W_c changes
every level's cover too, so the test compares the levels' difference,
not the climates.

Year 2 (months 13–24) by level, the preset's W_c and four times it
(`traj_L3.log` … `traj_L5.log`, `traj_L3_wc02.log` … `traj_L5_wc02.log`):

| | W_c = 0.05: L3 / L4 / L5 | L5 − L3 | W_c = 0.2: L3 / L4 / L5 | L5 − L3 |
|---|---|---:|---|---:|
| Polar caps | 281.7 / 276.0 / 267.6 K | −14.1 K | 284.2 / 278.7 / 271.1 K | −13.1 K |
| Tropics | 305.9 / 306.7 / 307.2 K | +1.3 K | 308.5 / 308.9 / 309.0 K | +0.5 K |
| P2 | 28.2 / 35.8 / 44.9 K | +16.7 K | 28.3 / 35.3 / 43.5 K | +15.2 K |
| Largest wind | 55 / 85 / 110 m/s | +55 m/s | 53 / 84 / 107 m/s | +53 m/s |

**Not confirmed:** with few cells at overcast the levels differ by
almost as much (−13.1 against −14.1 K). The overcast concentration
accounts for about 1 K; the caps' cloud loss is mostly the colder caps'
feedback.

## 7. When the difference starts: the first month (2026-11-05)

Month 1 of the cap budgets (`caps_L3.log` … `caps_L6.log`), before the
caps have cooled (their surface temperatures within 3 K):

| Month 1, W/m² of the cap | L3 | L4 | L5 | L6 |
|---|---:|---:|---:|---:|
| Latent carried in across 60°, S | 166.6 | 126.4 | 93.4 | 89.4 |
| Latent carried in across 60°, N | 187.9 | 144.8 | 119.8 | 106.3 |
| Dry carried in across 60°, S | 79.4 | 91.2 | 83.6 | 65.4 |
| Cloud longwave effect, S | 36.8 | 27.5 | 20.5 | 16.1 |
| Cloud cover, S | 0.45 | 0.39 | 0.33 | 0.30 |

- **The first difference is the water reaching the caps:** in the first
  month, finer levels carry 40–45 % less vapour into the caps, while the
  dry heat differs little; their clouds are thinner from the start. In
  year 2 the totals agree because colder caps draw more dry heat (§4):
  the year-2 agreement is the feedback, the month-1 deficit the cause.
- The dynamics' clipping adds no vapour (0.0000 kg/m²/day at L3), so the
  latent transports are the advection's.
- Open: whether the water condenses on the way (more resolved
  condensation in the mid-latitudes at finer levels) or is carried less
  (the advection's numerical diffusion, larger on coarse cells, moving
  vapour down its gradient). The vapour budget by 30° band
  ([`tools/v8/reference_vapour_budget_probe.cpp`](../../tools/v8/reference_vapour_budget_probe.cpp))
  runs for three months at L3–L6.

## 8. The vapour budget by band, and the advection scheme (2026-11-05)

Month 1, kg/m²/day of each band (`build/inv05/vapour_L3.log` …
`vapour_L5.log`; L6 running):

| | L3 | L4 | L5 |
|---|---:|---:|---:|
| Advection, 30° S–0 | −3.55 | −2.90 | −1.76 |
| Advection, 0–30° N | −3.17 | −2.29 | −1.97 |
| Advection, 60–30° S | +2.28 | +2.10 | +1.21 |
| Advection, 30–60° N | +2.43 | +1.50 | +1.20 |
| Advection, 90–60° S | +5.75 | +4.37 | +3.23 |
| Advection, 60–90° N | +6.49 | +5.00 | +4.14 |
| Condensation (resolved + convective), 90–60° S | 4.65 | 4.26 | 4.20 |
| Evaporation, 60–30° S | 5.64 | 7.16 | 7.85 |
| Vapour path, 30° S–0 (kg/m²) | 218 | 234 | 245 |

- **The meridional vapour flux itself shrinks with level,** from the
  tropics' export (about halved L3 to L5) to the caps' import (−44 %);
  the water is not lost on the way (the caps' condensation hardly
  changes, the mid-latitudes evaporate more to make up), and the tropics
  keep more of their vapour.
- **The cause in the code:** `primitive_equations.cpp` advects the vapour
  first-order upwind (ADR-0021 §4.5: upwind q on the edges and the
  interfaces, the limited positive form of ADR-0002 §4.2), and Θ with a
  centred, energy-conserving edge value. Upwind's numerical diffusivity
  is about |u| Δx / 2: with the cell spacing 891, 446, 223 and 112 km at
  L3–L6 and |u| = 10–30 m/s, 4.5–13, 2.2–6.7, 1.1–3.3 and 0.6–1.7 × 10⁶
  m²/s, against Earth's effective eddy diffusivity for moisture of about
  1–2 × 10⁶ m²/s. At L3 and L4 the scheme's own diffusion carries much of
  the poleward vapour flux; it halves with each level, the heat's does
  not exist, which is why the latent transport falls with level and the
  dry does not (§7).
- **The chain:** coarse cells over-transport vapour poleward → the caps
  are moist, cloudy and warm (vapour and cloud greenhouse); finer cells
  transport less → the caps dry, lose cloud, cool; ice and snow grow
  (§4); P2 rises; the polar jet strengthens (§3) until, at L6, the edge
  winds pass the dynamics' 150 m/s bound (§1).
- **For V8:** reference mode at L4 carries a numerical moisture
  diffusion of the order of Earth's eddy diffusivity or more, and climate
  mode's transport (ADR-0027, ADR-0028) was fitted to agree with it.

Whether reference mode converges once its moisture transport is resolved
cannot be said from these runs: the numerical part shrinks towards zero,
the resolved eddies' part is what is left, and L6 stops before its second
year ends.

Next, a causal test: the same runs with a higher-order, limited and
positive vapour advection (default off, the default's hash unchanged),
at L3 and L5; if the levels' cap difference largely closes, the upwind
scheme is confirmed. Adopting one is an ADR-0021 §4.5 amendment.

## 9. The advection test (2026-11-05)

The limited second-order vapour advection (`vapour_advection_order = 2`,
commit 80cc106; default off, the default unchanged), two years at L3, L4
and L5 (`traj_L3_o2.log` … `traj_L5_o2.log`; L5 months 13–22). Year 2:

| | Upwind: L3 / L4 / L5 | L5 − L3 | Order 2: L3 / L4 / L5 | L5 − L3 |
|---|---|---:|---|---:|
| Polar caps | 281.7 / 276.0 / 267.6 K | −14.1 K | 275.1 / 268.4 / 261.6 K | −13.5 K |
| Tropics | 305.9 / 306.7 / 307.2 K | +1.3 K | 307.2 / 307.5 / 307.9 K | +0.8 K |
| P2 | 28.2 / 35.8 / 44.9 K | +16.7 K | 37.7 / 45.6 / 52.7 K | +14.9 K |
| Largest wind | 55 / 85 / 110 m/s | +55 m/s | 71 / 111 / 131 m/s | +60 m/s |

- **Not the levels' difference:** order 2 cools every level's caps by
  6–8 K (less numerical diffusion, less vapour poleward, as §8 predicted),
  but the difference between the levels stays (−13.5 against −14.1 K).
  The upwind diffusion is a large effect on reference mode's caps at every
  level (and so on V8), not what changes with level.
- Order 2 makes the winds stronger at every level (L5 131 m/s in year 2),
  so it brings L5 nearer the dynamics' bound.

## 10. Where the water goes instead: condensation by band (2026-11-05)

Month 1 of the vapour budgets, L3–L6 (kg/m²/day; `vapour_L6.log` added):

| | L3 | L4 | L5 | L6 |
|---|---:|---:|---:|---:|
| 30° S–0: resolved condensation | 4.52 | 5.68 | 6.42 | 7.23 |
| 30° S–0: convective | 3.76 | 3.67 | 3.38 | 2.92 |
| 30° S–0: advection (export) | −3.55 | −2.90 | −1.76 | −1.43 |
| 60–30° S: resolved condensation | 5.16 | 6.75 | 6.97 | 7.32 |
| 60–30° S: evaporation | 5.64 | 7.16 | 7.85 | 8.67 |
| 60–30° S: advection | +2.28 | +2.10 | +1.21 | +0.71 |
| 90–60° S: advection | +5.75 | +4.37 | +3.23 | +3.09 |

The water rains out nearer where it evaporates at finer levels: the
tropics' and the mid-latitudes' resolved condensation rises by 40–60 %
from L3 to L6 (the convective part falls), the tropics export less than
half as much, the mid-latitudes evaporate more and import less. The local
water cycle intensifies with resolution and the poleward transport of
water weakens, with the upwind scheme or without it (§9). A candidate:
finer cells resolve narrower, stronger ascent and saturate where a
coarse cell's mean stays below saturation, so condensation is more
efficient on finer cells (the rain under the jet concentrates threefold
at L5, §3). The column has no sub-grid saturation in the preset (M8-07's
width β = 0), so a coarse cell condenses only once its mean saturates.

## 11. A passive tracer: transport without rain-out (2026-11-05)

A tracer χ held at 1 in the bottom layer equatorward of 30° and decaying
with a 10-day lifetime, carried by the winds with the vapour's own scheme
but never condensing (`passive_tracer_lifetime_s`, commit 28360be; off by
default, the tracer does not act on the climate, so each run's climate is
its baseline's). What the winds carry into the caps poleward of 60°
(kg/m²/day of the cap, of μ χ; `build/inv05/tracer_L3.log` …
`tracer_L6.log`, `tracer_L3_o2.log` … `tracer_L5_o2.log`):

| | L3 | L4 | L5 | L6 |
|---|---:|---:|---:|---:|
| Upwind, months 1–3, S / N | 48.8 / 45.3 | 43.8 / 38.3 | 35.1 / 30.2 | 32.8 / 26.1 |
| Upwind, months 4–6, S / N | 44.4 / 33.1 | 43.4 / 22.9 | 37.8 / 20.0 | |
| Order 2, months 1–3, S / N | 26.1 / 26.1 | 26.9 / 17.6 | 29.0 / 18.4 | |
| Order 2, months 4–6, S / N | 33.5 / 16.7 | 28.4 / 12.7 | 30.8 / 11.8 | |

- **With upwind, the transport itself falls with level** (−28 % S and
  −33 % N from L3 to L5 in months 1–3; L6 lower again), most of the
  vapour's fall (§8: −44 % and −36 %).
- **With order 2 it hardly does:** the levels' tracer delivery agrees
  within 10–30 % (only L3's north higher), and is about 40 % below
  upwind L3's. The upwind diffusion carries much of the tracer poleward
  at coarse levels, as §8 estimated; the resolved transport is roughly
  level-independent.
- **The puzzle left:** under order 2 the caps' temperature gap between
  levels stays at 13.5 K (§9) although the winds deliver about the same
  tracer at every level. So the vapour must rain out more on the way at
  finer levels (§10) — or the gap is not set by the water reaching the
  caps at all. The vapour budget under order 2 (`vapour_L3_o2.log` …
  `vapour_L5_o2.log`) separates them.

## 12. The vapour budget under order 2 (2026-11-05)

Month 1, kg/m²/day (`vapour_L3_o2.log` … `vapour_L5_o2.log`):

| | L3 | L4 | L5 |
|---|---:|---:|---:|
| Into 90–60° S | 2.48 | 2.69 | 2.65 |
| Into 60–90° N | 3.62 | 3.44 | 3.62 |
| Condensation, 90–60° S | 2.48 | 3.38 | 4.37 |
| Evaporation, 90–60° S | 0.28 | 0.90 | 1.85 |
| Condensation, 60–90° N | 3.77 | 4.73 | 5.26 |
| Evaporation, 60–90° N | 0.66 | 1.70 | 2.03 |
| Resolved condensation, 30° S–0 | 4.95 | 6.33 | 7.42 |
| Advection, 30° S–0 | −2.00 | −1.68 | −1.16 |
| Advection, 60–30° S | +0.87 | +0.96 | +0.71 |

- **The water reaching the caps does not depend on level** under order 2
  (within 0.2 kg/m²/day), as the tracer's does not (§11). Neither the
  transport nor the rain-out on the way sets the caps' level difference.
- **What does change with level is the local water cycle:** resolved
  condensation rises 40–75 % from L3 to L5 in every band, and the caps'
  own evaporation rises six- and threefold; the tropics export less.
- So the gap of 13.5 K (§9) is not the water supply. It builds over the
  first year from a 2–3 K start (§7) and comes with the ice and snow
  (§4). Candidates not tested: the cryosphere's growth on the finer
  coastlines and cells (the caps' land and ocean fractions and their sea
  ice by level), the resolved condensation's heating aloft and its
  effect on the caps' stability, and the grid-scale damping (ν ∝ d̄⁴ at
  a fixed 8-hour time).

### Status (2026-11-05)

Established:
1. Reference mode's caps cool 5–10 K per level (L3–L6); the tropics do not
   (§4). The colder caps raise P2 and strengthen a smooth zonal polar jet
   at 60–80° (§3), whose edge winds reach the 150 m/s bound at L6 (§1).
2. Not orography (§2), not the clouds' overcast limit (§6), not the
   upwind diffusion as the level difference (§9), not the water reaching
   the caps (§11, §12).
3. The upwind vapour advection is a large, level-dependent bias of its
   own: at L3 it carries about 40 % of the poleward water (§11), and
   removing it cools every level's caps by 6–8 K (§9). That bears on V8's
   reference at L4 whatever else is found.

## 13. The caps' surface by level (2026-11-09)

The same planet (seed 1) at each level, before any step
([`tools/v8/reference_cap_geometry_probe.cpp`](../../tools/v8/reference_cap_geometry_probe.cpp);
heights are each cell's median sub-cell elevation):

| Poleward of 60° | L3 | L4 | L5 | L6 |
|---|---:|---:|---:|---:|
| South: land share | 0.039 | 0.124 | 0.112 | 0.124 |
| South: land's mean height | 610 m | 846 m | 925 m | 760 m |
| South: land ≥ 1.5 km (share of the cap) | 0.000 | 0.019 | 0.027 | 0.020 |
| South: area in mixed coast cells | 0.135 | 0.204 | 0.116 | 0.093 |
| North: land share | 0.564 | 0.576 | 0.560 | 0.546 |
| North: land's mean height | 801 m | 710 m | 483 m | 394 m |
| North: land ≥ 1.5 km | 0.069 | 0.037 | 0.026 | 0.022 |
| North: area in mixed coast cells | 0.488 | 0.250 | 0.156 | 0.141 |

- **Not the caps' geometry from L4 on:** the land shares agree within
  0.03 at L4–L6 and the planet's within 0.001 (0.290); the northern land
  gets lower with level (710 → 394 m), which would warm it, while it
  cools.
- **L3's southern cap is the exception:** almost all ocean (land 0.039
  against 0.11–0.12), so part of L3's warmer southern cap is its
  geometry; the northern cap, with the same land at every level, cools
  as much (§4: 282.7, 278.2, 270.2, 263.0 K in year 2).
- The coast cells shrink with level, as they must; with the land shares
  unchanged, the cryosphere's growth (§4) follows the cooling, not the
  coastlines.

## 14. The damping test (2026-11-09)

The grid-scale damping time at 2 h and 32 h against the preset's 8 h
(`hyperviscosity_damping_s`, commit fcbf9c9), two years each
(`traj_L3_d2.log`, `traj_L3_d32.log`, `traj_L5_d2.log`, `traj_L5_d32.log`).
Year 2:

| | 2 h | 8 h | 32 h |
|---|---:|---:|---:|
| L3 polar caps | 280.6 K | 281.7 K | 282.2 K |
| L5 polar caps | 268.5 K | 267.6 K | 266.5 K |
| L3 P2 | 29.7 K | 28.2 K | 27.4 K |
| L5 P2 | 44.0 K | 44.9 K | 46.4 K |
| L5 largest wind | 108 m/s | 110 m/s | 113 m/s |

**Not the damping:** a factor of 16 in the damping time moves the caps by
about ±1 K (in opposite directions at L3 and L5), against the levels'
14 K. The damping that changes 16-fold per level at a fixed wavelength
does not set the caps.

## 15. The condensation-time test: void, and a correction (2026-11-09)

τ_c at 6 h and 48 h against 12 h, two years at L3 and L5
(`traj_L3_c6.log`, `traj_L3_c48.log`, `traj_L5_c6.log`, `traj_L5_c48.log`):
**every run is identical to its baseline** to the printed digits. The
column uses τ_c only without moist convection
(`atmosphere_column.hpp`: `in_solve_fraction` is 1 with moist convection,
and the pass after it takes the whole excess too); with it, as in the
preset, the resolved condensation is saturation adjustment, the whole
excess over the cell's mean saturation each step (ADR-0023 §4.3). The
code-read table above said a fixed 12 h; that was wrong.

What it means for the condensation lead: the preset's resolved
condensation has no time scale and no sub-grid width (M8-07's β = 0), so
a cell condenses exactly when its mean saturates, and finer cells, whose
means vary more, saturate more of the time. That is a resolution
dependence built into the scheme, consistent with §10 and §12 (resolved
condensation up 40–75 % from L3 to L5 in every band). It is not yet shown
to set the caps' temperature.

## 16. The sub-grid condensation test (2026-11-10)

M8-07's sub-grid condensation (`cloud_subgrid_width` β; the preset's 0 is
saturation adjustment) at β = 0.2 and 0.4, two years at L3 and L5
(`traj_L3_b02.log`, `traj_L3_b04.log`, `traj_L5_b02.log`,
`traj_L5_b04.log`; checked first that β reaches reference mode: L3's
first month differs). Year 2:

| | β = 0 | β = 0.2 | β = 0.4 |
|---|---:|---:|---:|
| L3 polar caps | 281.7 K | 282.1 K | 281.3 K |
| L5 polar caps | 267.6 K | 270.5 K | 270.0 K |
| **L5 − L3, caps** | **−14.1 K** | **−11.5 K** | **−11.4 K** |
| L5 − L3, P2 | +16.7 K | +15.2 K | +14.4 K |
| L5 largest wind | 110 m/s | 110 m/s | 105 m/s |

- **A part, not the cause:** sub-grid condensation warms L5's caps by
  2.3–2.9 K and leaves L3's within 0.4 K, so it removes about 2.6 K (18 %)
  of the levels' 14 K; doubling β adds nothing more. Saturation
  adjustment on the cell's mean contributes to the level difference, as
  §15 reasoned, but most of the gap is elsewhere.

### Status (2026-11-10)

Of the caps' 14 K between L3 and L5 in year 2: about 2.6 K is the
saturation adjustment (§16), about 1 K the clouds' overcast limit (§6),
and about 1 K L3's ocean-only southern cap (§13). Ruled out as the rest:
orography (§2), the upwind diffusion as the level difference (§9), the
water reaching the caps (§11, §12), the coastlines and land (§13), the
grid-scale damping (§14). The next step is to measure the caps' energy
budget in full by level (top of atmosphere, surface, the atmosphere's
storage), so that the term carrying the rest is found rather than
guessed.

