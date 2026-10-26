# ADR-0028 — The equatorial waves' heat exchange in the climate transport

- **Status:** Accepted (2026-10-26, option 1, c = 50 m/s)
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

## 10. Implementation (in progress, 2026-10-26)

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
