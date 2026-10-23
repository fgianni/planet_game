# ADR-0027 — The air mixed within the climate transport's groups

- **Status:** Proposed
- **Date:** 2026-10-23
- **Milestone:** P0, between M8 and M9 (investigation INV-04)
- **Context documents:** `docs/tasks/INV-04-v8-transport-and-radiation.md`
  (from "Climate mode's land is dry" to "Heat mixed within the transport
  groups")
- **Related:** ADR-0001 V2 (parity), ADR-0009 §11 (the cells' air) and
  §12 (the transport one mesh level coarser), ADR-0011 §17 (the coupled
  transport; §17.6 the azonal flow carries no heat), ADR-0021 §4.5 (the
  humidity on the same graph), ADR-0023 §16 (V8 at L4 over three years),
  ADR-0024 §9 (the diffusive fallback)
- **Amends on acceptance:** ADR-0009 §12's "every cell of a group
  receives the group's source"

## 1. Context

V8's baseline at L4 (ADR-0023 §16.4) fails at 0.936 K. The gap is land
against ocean: reference mode's land is 9.2 K warmer and its ocean 2.4 K
colder. INV-04 traced it to the climate transport:

- **Climate mode's land air is cold and dry.** Its bottom air is
  8.5–23 K colder than the ocean's at the same latitude (reference mode:
  0–5.5 K). Its transport brings land 1.55 W/m² of latent heat (reference
  mode: 23.3), and the cloud longwave over land is half reference mode's.
- **Most of the contrast is inside the transport's groups.** The
  transport is solved on groups of about four cells (§12). Each cell of a
  group receives the group's source per m² and solves its own column, so
  its air stays in balance with its own surface. Within a group with
  land and ocean, the land cells' bottom air is 30 K colder than the
  ocean cells', a few hundred kilometres apart. Between all-land and
  all-ocean groups at the same latitude the contrast is 10.7 K. Real air
  crosses a group in about a day; reference mode's resolved winds carry
  it so.
- **More water for land does not fix it.** Sensitivity tests carrying
  water by the azonal flow, or with ten times the water's eddy exchange,
  leave land 15–17 K colder than the ocean.

## 2. Decision drivers

- Physical: the air of neighbouring cells exchanges heat within a day; a
  monthly step should not keep a 30 K contrast across 300 km.
- Energy exact, and the coupled Newton (ADR-0011 §17.1) converging as
  now, with the fallback (ADR-0024 §9) as rare.
- No new tunable constant.
- The performance budgets (V11): L5 and L6 at 250 years.
- Deterministic for any worker count.

## 3. Options (each prototyped behind a parameter, off by default)

1. **Heat mixed within each group.** A cell's source is its group's H
   plus a shift δ with Σ A δ = 0, chosen so that the group's cells share
   one column temperature θ_c (or T_m with moist eddies) to first order:
   δ = (θ* − θ_c) / s_c, with s_c = dθ_c/dh and
   θ* = Σ A θ_c / s_c / Σ A / s_c. δ is held fixed through each coupled
   solve and updated between three solves, from the cells at H + δ; an
   update is scaled down so that no cell's source falls more than half
   way to its floor, and a solve that ends with a larger residual than
   the one before is undone. Prototype:
   `SurfaceEnergyParameters::heat_group_mixing` = 1.
2. **Option 1 and the water mixed within each group.** After the
   humidity transport, each cell's humidity is its group's (now each
   cell's is scaled by its group's change). Prototype:
   `humidity_group_mixing` = 1.
3. **Options 1 and 2 and the zonal-mean zonal wind's advection** between
   groups: divergence-free layer fluxes from a streamfunction of ū taken
   at the coarse cells' corners, for heat and water. Prototype:
   `zonal_advection_share` = 1.

## 4. Measurements (L4, climate mode, V8's climate half)

Ten-year spin-up, then three years (INV-04; reference mode does not use
this transport, so its 291.295 K and 1.451 m/yr stand):

| | Mean (K) | Land − ocean (K) | Air within mixed groups (K) | V8 gap (K) | Precipitation ratio | Wall (s) |
|---|---:|---:|---:|---:|---:|---:|
| Now | 290.361 | −16.6 | −30.2 | 0.934 | 0.901 | 7.7 |
| Option 1 | 290.819 | −8.9 | −0.1 | 0.476 | 0.902 | 12.9 |
| Option 2 | 290.906 | −9.0 | −0.5 | 0.389 | 0.905 | 12.0 |
| Option 3 | 293.959 | −6.4 | −0.7 | −2.664 | 0.888 | 39.9 |
| Reference mode | 291.295 | −4.9 | | | | |

All months converge below 1e-4 W/m², with no fallback month, and the
energy and water gates pass in every month.

L5 and L6 (250 years, 16 workers) and worker determinism: §8, to be
measured.

## 5. Recommendation

**Option 1** (or 2, which adds the water for consistency at no cost).
It removes the within-group contrast, halves land's deficit, and moves
V8's gap from 0.93 to about 0.4 K without a refit. m = 1 is the physical
value, not a fit: the air of a group is one air.

Option 3 is not recommended now: it warms the planet 3 K (a refit) and
triples the Newton iterations. Between groups, the zonal wind's
advection is the next candidate if V8 still fails after options 1–2.

## 6. Consequences

- A group is one air: the mixing scale is the group's size (about 480 km
  at L5, 240 km at L6), so the remaining land–ocean contrast is between
  groups and depends on the level.
- The step does two more coupled solves and a column pass each month
  (1.7 times L4's wall time).
- V3's L4 fit (287.984 K at 150 years) moves; a refit of τ_d and κ_v may
  follow, measured, not assumed.
- PSNAP and field meanings are unchanged: the applied source per cell is
  a step-internal quantity.

## 7. Validation

| ID | Check |
|---|---|
| V1 | Energy and water close to round-off on the planet (L4, L5, L6) |
| V2 | A unit test: a two-cell group of land and ocean ends with one θ_c to first order, Σ A δ = 0 exactly, and an off switch bit-identical to now |
| V3 | V8 at L4 over three years (ADR-0023 §16) |
| V4 | V11: L5 and L6 at 250 years within their budgets (607.5 s, 2 025 s), with no more fallback months than now |
| V5 | The same state hash on one and four workers |
| V6 | V3's L4 fit and V9's cloud targets, after any refit |

## 8. Open measurements

- L5 and L6 at 250 years with option 1 (running, INV-04).
- V5 on one and four workers.

## 9. Tasks

1. INV-04: §8's measurements, recorded.
2. A decision with the user, then an implementation task (the
   prototype's knob becomes the preset's, a unit test, the refit if
   needed, V8).
