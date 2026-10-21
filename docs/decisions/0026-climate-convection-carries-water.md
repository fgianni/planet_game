# ADR-0026 — Climate mode's convection carries water up

- **Status:** Option 1 accepted; options 2 and 3 not taken for P0
  (2026-10-20, with the user)
- **Date:** 2026-10-20
- **Accepted:** 2026-10-20, option 1 (§8)
- **Milestone:** P0, between M8 and M9 (investigation INV-01)
- **Context documents:** `docs/tasks/INV-01-reference-humidity-and-parity.md`
  (findings 1–4); task M8-07 (the vapour budget); task M8-09 (the modes'
  convective shares)
- **Related:** ADR-0001 V2 (parity), ADR-0021 §4.5 (one physics in both
  modes), ADR-0023 (§4.2 Betts–Miller, §4.2.5 no layer moistened, §11.1
  condensation in the solve, §15 V8), ADR-0024 (the transport's Newton),
  ADR-0025 (§4.3 and §13 convective condensate and detrainment, §14 the
  investigation)
- **Amends on acceptance:** ADR-0023 §4.2.5 and §11.1; ADR-0025 §13's
  convective share in climate mode

## 1. Context

V8's +6.4 K gap (ADR-0023 §15) is, by INV-01's finding 4, mostly climate
mode's missing upward water transport:
- reference mode has +23 W/m² of greenhouse, about 18 from high cloud
  (0.20 against 0.013) and about 5 from clear-sky vapour, three times
  climate mode's in the middle layer;
- it also absorbs +13.5 W/m² more sunlight, at an equal cloud shortwave
  effect: likely the warmer planet's smaller snow and ice.

Climate mode keeps its water in the bottom layer:
- **§11.1:** the month's evaporation condenses in the bottom layer inside
  the solve, before convection acts (task M8-07: 92 % of the tropics'
  condensation, against 0.12 mm/day lifted into the middle layer and
  1.83 in reference mode);
- **§4.2.5:** Betts–Miller never moistens a layer, so convection lifts no
  water. In Betts and Miller's scheme the convecting column relaxes to a
  reference humidity profile in every layer, moistening those below it
  and drying those above it (Betts 1986).

Reference mode lifts water by resolved ascent, and its high cover (0.20)
is near Earth's (0.2–0.3). The missing high cloud, the dry middle
troposphere in climate mode and most of V8's gap are one cause.

## 2. Decision drivers

- One physics in both modes (ADR-0021 §4.5): whatever climate mode's
  convection does, reference mode's must do, at its own step.
- Water and moist enthalpy exact.
- Continuous in the state for the solve and the coupled transport's
  Newton (ADR-0024).
- Each change measured on the planet against reference mode before it
  is decided: high cover, the middle layer's vapour, V8.

## 3. Options (each a prototype behind a parameter, off by default and bit-identical)

1. **Betts–Miller moistening (amends ADR-0023 §4.2.5).** The convecting
   column relaxes every layer's humidity towards RH_bm q_sat(T_ref), up as
   well as down, as Betts and Miller's scheme does.
   - **Deep case** (the column loses water): the net loss is the
     convective precipitation. The moistened layers' gain is water the
     dried layers lost, so each layer's convective condensate is its
     drying, reduced in proportion until the condensates sum to the net
     loss.
   - **Shallow case** (relaxing every layer would add water): the
     humidity profile is scaled so that it adds none (a constant shift
     could take a cold layer's small q_ref below zero), and
     the temperatures keep Σ ΔT = 0 (Betts 1986's shallow scheme). No
     precipitation.
   - The moist enthalpy is kept exactly in both cases.
2. **Convection before condensation for convecting columns (amends §11.1).**
   In a column that convects, the solve condenses only what Betts–Miller
   cannot take: the bottom layer's excess goes to the convection, which
   lifts it. Specified after option 1 is measured.
3. **The month's convective share (amends ADR-0025 §13 in climate mode).**
   A month counts as convective the share of its time a column would
   convect (task M8-09: 0.99 against 0.44). Specified after options 1
   and 2 are measured.

## 4. Validation

| ID | Check |
|---|---|
| V1 | Water and moist enthalpy close to round-off with each option, in a column and on the planet |
| V2 | Option 1: a convecting column's layers relax to RH_bm q_sat(T_ref) in both directions; the deep case's precipitation is the net loss; the shallow case adds no water |
| V3 | Climate mode's middle-layer vapour and high cover move towards reference mode's (L3, year 11; reference: 12.6 kg/m², 0.20) |
| V4 | V8's gap (L3, year 11) and each term of its anatomy (INV-01 finding 4), measured for each option |
| V5 | The coupled Newton converges at L3–L6 as now |

## 5. Consequences

- The climate changes. A refit (τ_d, κ_v and the cloud constants) follows
  whatever is accepted, in both modes at once.
- ADR-0025's cloud work (§4.1, §11, §4.3) is to be measured again on top
  of it: with water carried up, the upper layers' humidity and the anvils
  change.

## 6. Tasks

1. **INV-01, continued:** the options' prototypes and V1–V5's
   measurements, recorded.
2. A decision on what to accept, with the user, then an implementation
   task and the refit.

## 7. References

- Betts, A. K. (1986). A new convective adjustment scheme. Part I:
  Observational and theoretical basis. *Q. J. R. Meteorol. Soc.* 112,
  677–691.
- Betts, A. K., and Miller, M. J. (1986). A new convective adjustment
  scheme. Part II: Single column tests using GATE wave, BOMEX, ATEX and
  arctic air-mass data sets. *Q. J. R. Meteorol. Soc.* 112, 693–709.

## 8. Decisions

**8.1 Option 1 accepted (2026-10-20, with the user; INV-01 finding 5).**
- **What it gives:** at L3, year 11, climate mode's middle-layer vapour
  rises from 3.8 to 11.3 kg/m² (reference 12.6), and reference mode is
  unchanged (295.46 K against 295.51).
- **V8's gap** falls from +6.4 to +3.1 K, the greenhouse difference from
  23 to 5 W/m².
- **It amends ADR-0023 §4.2.5:** Betts–Miller moistens as well as dries,
  as in Betts and Miller's scheme. Water and moist enthalpy are exact
  (V1, V2).
- **In the preset with the one refit** at the end of this ADR's work, so
  the constants are fitted once against the final physics. Until then
  `AtmosphereParameters::convective_moistening` stays off by default.
- **What remains of the gap:** high cloud (0.04 against 0.20, about
  12 W/m² of the longwave effect) and the surface albedo (clear-sky
  absorbed sunlight +11 W/m², the ice-albedo feedback).
- **Option 2 set aside** unless option 3 falls short. Moving the bottom
  layer's moisture past the solve's condensation would take the month's
  latent heat out of the implicit balance, the failure ADR-0023 §11.1
  fixed (task M8-02).

**8.2 Option 3 not taken for P0 (2026-10-20, with the user; INV-01
finding 6).**
- Reference mode's share of convecting steps rises cleanly with its mean
  column's parcel buoyancy (0 below −8 K, 0.46 at −1 to 0 K, 0.81 at 0 to
  1 K).
- Climate mode's month-end columns are stable (−6 to −4 K), since a
  month's step completes the relaxation, so the curve would give it a
  share near 0.1 against reference mode's 0.42.
- Only a fitted offset would match them. On three layers, with liquid
  anvils that overheat the planet (task M8-09), that is not worth a
  closure in P0. V8's remaining gap (high cloud and the ice-albedo
  feedback) is recorded after the refit.

**8.3 Follow-up (INV-02, measured 2026-10-20).** The Earth-like preset
now enables option 1. A shared refit gives τ_d = 1.11144453 and
κ_v = 0.00555722248 m²/kg. V9's global cloud targets pass without
changing the cloud constants. In V8, precipitation passes at a 0.969
climate/reference ratio, but the temperature gap is 4.148 K against the
0.3 K limit; reference high cloud cover remains 0.192 against climate's
0.031. At L5, the 250-year budget and performance gates pass. At L6,
ADR-0024 §9's accepted diffusive fallback completes the 250-year cold
start on INV-02's pair within V11. INV-03 then refitted τ_d and κ_v to
1.111 and 0.005555 m²/kg; L5/250 and L6/250 pass performance, energy,
water and column-convergence gates on this pair. M9 has not started. Full measurements are in
[`INV-02`](../tasks/INV-02-moistening-refit-and-parity.md) and
[`INV-03`](../tasks/INV-03-l6-startup-transport.md).

