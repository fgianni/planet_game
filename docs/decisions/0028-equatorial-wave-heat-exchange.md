# ADR-0028 — The equatorial waves' heat exchange in the climate transport

- **Status:** Accepted (2026-10-26, option 1, c = 50 m/s); amended
  2026-10-30 with the user (§11: c = 25 m/s)
- **Date:** 2026-10-26
- **Milestone:** P0, between M8 and M9 (investigation INV-04)
- **Context documents:** `docs/tasks/INV-04-v8-transport-and-radiation.md`
  ("Water for land with the heat mixed", "The cold air is the tropics'
  missing heat exchange", "The equatorial waves' exchange")
- **Related:** ADR-0001 V2 (parity), ADR-0009 §12 (the transport one mesh
  level coarser), ADR-0011 §17 (the coupled transport; §17.6 the azonal
  flow carries no heat), ADR-0023 §16 (V8 at L4 over three years),
  ADR-0024 (the eddies on moist enthalpy), ADR-0027 (the air mixed within
  the transport's groups)
- **Amends on acceptance:** ADR-0011 §17's eddy conductance (the zonal
  model's D̄ alone)

## 1. Context

ADR-0027 made V8 pass at L4 (−0.029 K, precipitation ratio 0.908), but
by cancellation: reference mode's land is 2.7 K warmer than climate
mode's and its ocean 1.1 K colder. INV-04 has since found why:

- **No transport of water fixes it.** Water mixed within groups, ten
  times the water's eddies, the zonal wind's or the azonal flow's water
  in its column mean add at most 2.6 W/m² to land's 2.7 W/m² of latent
  heat; the full azonal flow's water moistens the whole atmosphere (a
  3 % share warms the planet 2.3 K) rather than land.
- **The tropics' free troposphere is not tied together.** At
  30° S–30° N climate mode's middle layer is 10.7 K colder over land
  than over the ocean; reference mode's is 2.2 K colder. Betts–Miller
  relaxes each convecting column to the moist adiabat of its own bottom
  air, and between groups only the transport's eddies exchange heat.
  Their diffusivity, the zonal model's D = ℓ_h × eddy speed, is
  baroclinic and smallest at the equator (0.57–0.62e6 m²/s against
  1.1–1.3e6 at 45–65°).
- **The real tropics are tied by their waves.** Gravity waves remove
  free-tropospheric temperature differences within days (the weak
  temperature gradient balance), with the Walker and monsoon
  circulations. Reference mode resolves them; climate mode's monthly
  balance has none (§17.6 removed the azonal flow's heat because it was
  unbounded).
- **A stronger tropical exchange closes the split.** INV-04's
  sensitivity knob (a factor 10–30 on the eddy conductance within 30°,
  tapering to 1 at 40°) gives a land − ocean contrast of −5.3 to −4.4 K
  (reference: −4.8 K) and a precipitation ratio of 0.99–1.01. The land's
  air stays as dry: the contrast was heat, the dryness its symptom. A
  factor is a fit, though; this ADR is about its physical form.

## 2. Decision drivers

- Physical, with no fitted constant; the same at every level.
- Energy and water exact; the coupled Newton (ADR-0011 §17.1) and the
  fallback (ADR-0024 §9) as now.
- V8 by agreement, not cancellation: land and ocean each near reference
  mode's.
- The performance budgets (V11) and determinism (V5).

## 3. The form

The equatorial waves' exchange adds a diffusivity to the eddies':

  D = D̄ + D_w,  D_w(y) = c L_eq exp(−y² / (2 L_eq²)),
  L_eq = √(c / β),  β = 2Ω / a,

with c the first baroclinic gravity-wave speed, L_eq the equatorial
deformation radius, and y = a φ the distance from the equator. The
Gaussian is the Kelvin wave's meridional shape; c L_eq is the waves'
speed times their scale. It enters the transport's eddy conductance (so
heat and, through the same conductances, water) at every face, and
nothing else. On the Earth-like planet with c = 50 m/s: L_eq = 1 478 km
(13.3°), D_w = 7.4e7 m²/s at the equator, 2.4e7 at 20°, 6e6 at 30°,
8e5 at 40°.

## 4. Options (prototyped behind a parameter, off by default)

`SurfaceEnergyParameters::equatorial_wave_speed_m_s` = c (0: off).

1. **c a planet constant, 50 m/s for the Earth-like preset:** the value
   usually quoted for Earth's dry first baroclinic mode.
2. **c from the planet's tropical stratification each month,**
   c = N H / π from the zonal solution's tropical layer temperatures,
   with H the depth to the top layer's centre. On the Earth-like planet
   this gives 62–65 m/s in both modes (INV-04). It follows other planets
   and climates, but adds a month-to-month dependence (outside the
   Newton, like the eddies' D̄) and a choice of H.
3. **The prototype's factor** (×10–30 within 30°): a fit; not
   recommended.

## 5. Measurements (climate mode, ten-year spin-up, then three years)

L4, the ADR-0027 preset otherwise (INV-04; reference mode does not use
this transport, so its numbers stand):

| c (m/s) | Mean (K) | Land (K) | Land − ocean (K) | All-land vs all-ocean groups (K) | Precipitation (m/yr) | Ratio |
|---:|---:|---:|---:|---:|---:|---:|
| 0 (now) | 292.09 | 285.91 | −8.7 | −9.5 | 1.621 | 0.908 |
| 30 | 291.71 | 288.16 | −5.0 | −3.5 | 1.502 | 0.979 |
| 50 | 292.50 | 289.60 | −4.1 | −2.2 | 1.467 | 1.003 |
| 62 | 293.04 | 290.46 | −3.6 | −1.7 | 1.459 | 1.008 |
| 70 | 293.59 | 291.16 | −3.4 | −1.4 | 1.457 | 1.010 |
| Reference mode | 292.06 | 288.63 | −4.8 | | 1.471 | |

L5: c = 0 gives −8.0 K and 1.639 m/yr, c = 50 gives −3.5 K and
1.463 m/yr. Every run: no fallback month, the energy and water gates
pass, the Newton iterations unchanged (L4 115–119 against 116, L5 149
against 151).

## 6. Recommendation

**Option 1, c = 50 m/s.** It closes V8's split by agreement (land −4.1
against −4.8 K) and the precipitation ratio (1.003), at every level
measured, with no change to the solve's cost. c is a physical property
of the planet's atmosphere, not a fit to V8; option 2's derivation is a
later generalisation when other planets need it (its 62 m/s would warm
the Earth-like planet another 0.5 K before the refit).

## 7. Consequences

- The mean warms 0.4 K at L4 after three years (more in equilibrium), so
  V3's L4 fit (τ_d, κ_v) is refitted, then V8 and V9.
- The tropics' heat moves faster towards 30–40°, so the P2
  equator-to-pole difference may fall; V3's gradient is checked.
- Land's air stays drier than reference mode's (its latent delivery
  falls to 1.8 W/m²); the remaining water difference is recorded with
  INV-04's known limitations.
- PSNAP and field meanings are unchanged.

## 8. Validation

| ID | Check |
|---|---|
| V1 | Energy and water close to round-off (L4, L5, L6) |
| V2 | A unit test: D_w's value at the equator and its Gaussian width; c = 0 bit-identical to now |
| V3 | V8 at L4 over three years (ADR-0023 §16): the gap, the ratio, and land and ocean each within 1 K of reference mode's |
| V4 | V11: L5 and L6 at 250 years within their budgets, with no more fallback months than now |
| V5 | The same state hash on one and four workers |
| V6 | V3's L4 fit and V9's cloud targets, after the refit |

## 9. Tasks

1. INV-04: §5's measurements, recorded.
2. ~~A decision with the user~~ (accepted 2026-10-26: option 1,
   c = 50 m/s).
3. On acceptance: c in the Earth-like preset, V2's test, the refit (V6),
   V8 (V3), V11 (V4) and V5.

## 10. Implementation (complete, 2026-10-26)

- `earth_like` sets `equatorial_wave_speed_m_s = 50`;
  `equatorial_wave_diffusivity_m2_s` (`climate_circulation.hpp`) is the
  formula, and `test_equatorial_wave_exchange` is V2 (the peak c L_eq,
  e^(−1/2) at one L_eq north and south, zero for c = 0; at planet level,
  L3, two months with c = 50 and 0 close energy and water, differ, and
  agree on 1 and 4 workers).
- **The cold start's first month is harder.** L3's cold-start year
  (`test_coupled_transport`) stalls in months 0 and 3 at 51.9 and
  4.3 W/m² after 30 Newton iterations (c = 0: 3–4 iterations): a
  convecting column whose θ_c falls as its source rises (dθ_c/dh
  = −0.063 K m²/W), ADR-0024 §9.5's kink. L4's cold start falls back in
  month 0 (1 110 W/m²; c = 0 ends that month capped at 9.7 W/m²), then
  converges in 4–5 iterations every month (c = 0: 3–30). That test's
  first case, which checks the coupled solve, now runs without the
  waves. V4 measures the fallback months at L5 and L6.
- **The refit (V6):** the pair scaled by 0.9265, τ_d = 1.062696 and
  κ_v = 0.00531348 m²/kg, gives an L4/150 mean of 288.001 K. Cover is
  0.519 (limit ≥ 0.5). The cloud shortwave is −41.1 W/m² and the
  longwave +22.8 W/m². Every expectation passes. Logs:
  `build/adr0028/refit_*.log`.
- **V8 (V3), L4, ten-year spin-up then three years** (log
  `build/adr0028/v8_v11.log`). It passes by agreement:

  | | Climate | Reference | Difference |
  |---|---:|---:|---:|
  | Mean (K) | 290.403 | 290.257 | −0.146 |
  | Land (K) | 287.011 | 286.623 | −0.39 |
  | Ocean (K) | 291.789 | 291.742 | −0.05 |
  | Precipitation (m/yr) | 1.430 | 1.424 | ratio 0.996 |

  Land − ocean is −4.8 K in climate mode and −5.1 K in reference mode.
  Land's air stays drier, as §7 expected. Its latent heat delivered is
  1.7 against 23.0 W/m², and its tropical bottom-layer humidity 5.4
  against 10.6 g/kg. The P2 equator-to-pole difference is 38.9 K
  against 48.8 K.
- **V11 (V4) stopped in month 0 at first.** L5, L6 and the L6 restart
  all threw, because ADR-0024 §9's fallback failed after the coupled
  solve diverged. ADR-0024 §9.6 (accepted 2026-10-26) fixes it: the
  fallback restarts its columns from the step's start, and a fallback
  that fails too moves no heat. Rerun on that code (log
  `build/adr0028/v11_96.log`):

  | Run | Wall (s) | Budget (s) | Fallback months | No-heat months | Energy / water (of gate) | Mean (K) |
  |---|---:|---:|---:|---:|---|---:|
  | L5/250 | 502.6 | 607.5 | 3 (months 0, 1, 3) | 0 | 1.0e-5 / 2.0e-4 | 287.50 |
  | L6/250 | 1 803.7 | 2 025 | 4 (months 0, 2, 3, 29) | 1 (month 0) | 1.3e-5 / 4.1e-4 | 287.66 |

  Both are within budget, with no failed circulation month and every
  expectation passing. The fallback months rise from 1 to 3 at L5 and
  from 3 to 4 at L6, but all except L6's month 29 fall in the cold
  start's first year. V4's "no more fallback months than now" is
  therefore met only after the first year.
- **V5:** L6 restarts from a PSNAP at tick 87659, before month 2. Four
  monthly hashes match through ticks 131488–262975, on one worker
  uninterrupted against four after the restart. Two of those months
  are fallback months. The final hash is `4544171262541673357`.
  `test_fallback_restart` gives the same L6 first month on 4 and 16
  workers.

## 11. Amendment: c = 25 m/s (accepted 2026-10-30 with the user, investigation INV-04)

**11.1 Why.** INV-04's option A (ADR-0029) and its column sweep measured
what c = 50 m/s costs (climate mode, L4, ten-year spin-up then three
years; `tools/v8/climate_column_probe.cpp`):
- **The waves cool the tropical air and suppress the Hadley overturning.**
  At the same sea-surface temperature, climate mode's tropical air is
  9–12 K colder than reference mode's, and its bottom-layer mass flux at
  10° S / 10° N is −4 / −13 Sv against reference mode's +52 / −36 Sv.
  Their diffusion exports the tropics' heat in the overturning's place.
- **They flatten the planet's temperature gradient:** the P2
  equator-to-pole difference is 38.9 K against reference mode's 48.8 K.
  V8's pass at c = 50 is partly a compensation.

**11.2 The measurements** (reference mode, which has no waves, from V8
at L4 on 2026-10-30: 290.26 K; land 286.62, ocean 291.74, land − ocean
−5.12 K; P2 48.8 K; 1.424 m/yr; bottom-layer mass flux +51.9 / −36.0 Sv;
tropical bottom air 290.4 K). Before any refit:

| c (m/s) | Global (K) | Land − ocean (K) | P2 (K) | Rain (m/yr) | Mass flux 10° S / 10° N (Sv) | Tropical bottom air (K) |
|---|---:|---:|---:|---:|---|---:|
| 50 | 290.40 | −4.79 | 38.9 | 1.43 | −3.9 / −12.9 | 281.1 |
| 37.5 | 289.85 | −5.12 | 43.8 | 1.45 | −2.9 / −15.5 | 282.0 |
| **25** | **289.45** | **−5.84** | **47.5** | **1.48** | **−0.1 / −18.2** | **283.1** |
| 12.5 | 289.21 | −7.33 | 50.6 | 1.54 | +9.0 / −19.6 | 284.4 |
| 0 | 289.10 | −9.52 | 50.9 | 1.59 | +18.2 / −17.5 | 285.7 |

**11.3 Decision.** c = 25 m/s in the Earth-like preset. It brings the
P2 equator-to-pole difference to within 1.3 K of reference mode's (the
waves' purpose: the tropics' heat exchange, not V8's land–ocean split),
and the northern Hadley branch converges again, at the cost of a land
1.3 K colder (land − ocean −5.8 against −5.1 K).

**11.4 Consequences.**
- **V8 is expected to fail:** before the refit, the global gap is about
  +0.8 K (reference minus climate). The pass at c = 50 rested on the
  compensation in §11.1. The result is recorded as a known limitation
  with ADR-0029's, not tuned away.
- **What remains is not the waves':** with them off, the tropical air is
  still 4–5 K colder than reference mode's and the overturning half as
  strong; climate mode's rain is 99 % Betts–Miller's from its bottom
  layer, reference mode's 79 % from resolved ascent aloft (INV-04,
  "The remaining 4–5 K").
- PSNAP and field meanings are unchanged; the radiation pair is refitted
  (V6), as for every change of the climate.

**11.5 Validation:** §8's V1–V6 again: the refit to 288 K at L4/150
(V6), V8 at L4 (V3, recorded whether it passes or not), V11 at L5 and
L6 (V4), and V5.

**11.6 Implementation (2026-10-30).**
- c = 25 m/s in the Earth-like preset; V2's test runs it as the preset.
- **The refit (V6):** c = 25 cooled the L4/150 mean to 286.093 K; τ_d and
  κ_v scaled by 1.0632 give τ_d = 1.1298584 and κ_v = 0.0056492919 m²/kg
  and 288.009 K (×1.073 gave 288.296 K). The L4/150 acceptance test
  passes: cover 0.538, cloud effects −42.6 / +22.4 W/m², one fallback
  month, energy and water within their gates.
- **Two tests pinned to the configuration they exercise:**
  `test_fallback_restart` (ADR-0024 §9.6's no-heat first month at L6) and
  the L4 snapshot restart in `test_convective_moistening_run` (its
  month-2 fallback) now set c = 50 m/s (and, for the first, the previous
  pair): §11's preset takes other paths through those months.
- **`test_zonal_coupling`'s jet window** compared the bands' centres
  exactly with 37.5°, but the centre is 37.499999999999979°; with c = 25
  the northern winter jet is a single merged one at that band
  (31.3 m/s). The window now allows for the rounding.
- The suite passes (111). The L3 two-year state hash is
  `d4ccd25158c01ed7`.
- **Still to run:** V8 at L4 (V3) and V11 at L5 and L6 (V4), on the RTX
  5070 Ti machine's P-cores once the L5 V8 run there ends.

