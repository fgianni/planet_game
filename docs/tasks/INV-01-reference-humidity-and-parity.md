# Investigation INV-01 — Reference mode's humidity, the month's variability and V8

- **Milestone:** P0, between M8 and M9 (ADR-0025 §14.3)
- **Status:** findings complete (2026-10-20); closed with ADR-0026 option 1
  accepted and option 3 not taken for P0 (decided with the user). Next:
  the refit with moistening on, then V8 measured again (not started)
- **Governing decisions:** ADR-0025 §14 (the questions); ADR-0001 V2
  (climate–reference parity); ADR-0023 §13 and §15 (V8 and its reading);
  ADR-0021 §4.5 (one physics in both modes)
- **Kind:** an investigation. It measures and explains. Any change to the
  physics it points to gets its own ADR first.

## Questions

1. **Why is reference mode so moist?** Its bottom layer is at 0.92–0.98
   relative humidity and saturated half the time or more in every band,
   at three and at five layers (ADR-0025 §14.2; Earth about 0.7–0.8).
   Climate mode's bottom layer is at 0.82–0.93.
2. **What must a month carry of its sub-monthly variability?** Three
   times the monthly step has split the modes: the cloud fraction
   (ADR-0023 §13), the sub-grid spread (ADR-0025 §4.2) and the
   convective share (ADR-0025 §13, task M8-09: 0.99 against 0.44).
3. **V8's temperature gap:** reference mode runs +6.4 K warmer (ADR-0023
   §15.3). About 5 K predates the clouds (§15.4).

## Plan

1. Where reference mode's bottom layer saturates: in convecting or
   stable columns, by band.
2. What keeps it there: the budget of the saturated cells against the
   unsaturated ones (task M8-07's diagnostics), and what Earth's boundary
   layer does that the model lacks (turbulent mixing, shallow convection
   moving moisture up).
3. Prototypes behind parameters, off by default and bit-identical, to
   test each candidate in both modes. None reaches the preset without an
   ADR.
4. Question 2 from the same runs: each mode's within-month statistics of
   the quantities that split them.
5. Question 3: V8's gap broken down by term once the humidity is
   understood.

## Tools

- `PlanetRun`'s fourth argument, `adjust`: changes the scenario's surface
  and atmosphere parameters before a run, for investigation runs only
  (the manifest does not record it, so such a run is not replayable).
- Task M8-07's vapour budget by 30° band and layer
  (`SurfaceEnergyDiagnostics::vapour_budget`,
  `AtmosphereDynamicsDiagnostics::advection_by_band_kg`).

## Findings

### 1. Where reference mode's bottom layer saturates (L3, three layers)

Reference mode from rest, days 120–150 (the bottom layer's humidity
settles within weeks); a column counts as convecting in a step when it
rains convectively:

| Band | Share convecting | Bottom RH (saturated share): convecting | not convecting |
|---|---|---|---|
| 60–90° N | 0.05 | 0.92 (0.20) | 0.99 (0.68) |
| 30–60° N | 0.29 | 0.91 (0.21) | 0.97 (0.61) |
| 0–30° N | 0.68 | 0.93 (0.41) | 0.92 (0.35) |
| 0–30° S | 0.26 | 0.94 (0.43) | 0.91 (0.37) |
| 30–60° S | 0.03 | 0.87 (0.05) | 0.94 (0.43) |
| 60–90° S | 0.00 | 0.69 (0.00) | 0.92 (0.47) |

- **Convection is intermittent in reference mode:** 0–68 % of the
  columns convect in a step, by band and season. Climate mode's month
  convects in nearly every column (task M8-09: convective share 0.99).
  That is question 2's intermittency, measured.
- **A column that does not convect saturates its bottom layer.** At mid
  and high latitudes it sits at 0.97–0.99. Nothing exports the layer's
  moisture but the resolved winds, and each step condenses its excess in
  place (ADR-0023 §11.1, no τ_c).
- **Convecting columns saturate too** in the tropics (41–43 % of the
  time). Betts–Miller removes moisture towards RH_bm over τ_bm = 2 h,
  while the in-place condensation caps the layer at saturation in every
  10-minute step.
- **Candidate mechanism:** the model has no export of moisture from the
  lowest layer except deep convection and the resolved winds. Earth's
  boundary layer exports it by turbulent mixing and shallow convection
  in every column. Tested next by sensitivity runs and a prototype.

### 2. Convection's constants are not the lever (L3, reference, days 120–150)

Bottom-layer RH in non-convecting / convecting columns:

| Band | τ_bm 2 h, RH_bm 0.8 | τ_bm 0.5 h | RH_bm 0.7 |
|---|---|---|---|
| 60–90° N | 0.99 / 0.92 | 0.98 / 0.89 | 0.98 / 0.87 |
| 30–60° N | 0.97 / 0.91 | 0.97 / 0.89 | 0.97 / 0.86 |
| 0–30° N | 0.92 / 0.93 | 0.93 / 0.92 | 0.89 / 0.91 |
| 30–60° S | 0.94 / 0.87 | 0.94 / 0.83 | 0.93 / 0.80 |

Four times faster convection, or a drier reference profile, lowers the
convecting columns by a few hundredths and leaves the others where they
were.

### 3. Mixing the boundary layer moves the water, not out (prototype, withdrawn)

A prototype exchanged humidity between the bottom two layers, the
equal-mass pair relaxing to its mean over τ_mix in every column:
- **τ_mix = 24 h:** bottom RH 0.89–0.97, saturated 19–43 % of the time
  (from 35–68 %);
- **τ_mix = 6 h:** much the same;
- **the cost:** the layer above rises to 0.80–0.96, and convection stops
  everywhere (the share of convecting columns falls to 0). Moisture taken
  from the bottom layer removes the parcel's buoyancy, so Betts–Miller
  never triggers, and the rain becomes large-scale one layer up.

The prototype is withdrawn. It points from how the water is shared
between the layers to how much there is.
- Reference mode's tropical column holds 80 kg/m² (Earth about 45,
  climate mode 38).
- Its water stays about 15 days (Earth about 9, climate mode 7).
- Reference mode is also 6.5 K warmer, which alone raises q_sat by about
  60 %, and the grey vapour feedback is strong (ADR-0021 §12: bistable).
- The humidity and V8's temperature gap may then be one problem: next,
  the gap's anatomy (absorbed sunlight, outgoing longwave, the cloud
  effects, the vapour path by layer) in both modes' eleventh year.

### 4. V8's anatomy: climate mode carries no water up (L3, each mode's eleventh year)

Each mode spun up ten years in its own physics (ADR-0023 §15), then its
eleventh year (reference mode sampled hourly; its precipitation is not
quoted, as the probe sampled one step in six):

| | Climate | Reference | Difference |
|---|---|---|---|
| Surface temperature | 289.07 K | 295.51 K | +6.4 K |
| Absorbed sunlight | 235.3 W/m² | 248.8 W/m² | +13.5 |
| Cloud shortwave effect | −46.2 | −45.4 | +0.8 |
| Cloud longwave effect | +21.6 | +39.2 | +17.6 |
| Greenhouse effect, σT_s⁴ − OLR | 154.6 | 177.5 | +22.9 |
| Cover / above the bottom layer | 0.58 / 0.013 | 0.46 / 0.20 | |
| Vapour by layer (kg/m²) | 21.6 / 3.8 / 1.0 | 33.3 / 12.6 / 1.7 | the middle ×3.3 |
| Layer temperatures | 274.6 / 251.6 / 223.7 K | 283.2 / 263.7 / 233.0 K | +9 to +12 K |
| Convective share | 0.99 | 0.44 | |

- **Reference mode's extra greenhouse, +23 W/m²:** about 18 from its high
  cloud (0.20 against 0.013), about 5 from clear-sky vapour (three times
  climate mode's in the middle layer).
- **Its extra absorbed sunlight, +13.5 W/m²,** with the same cloud
  shortwave effect: the surface's albedo, most likely less snow and sea
  ice on the warmer planet (not yet measured).
- **The cause is in climate mode.** Reference mode's resolved ascent
  lifts water (finding 3; task M8-07's budget: 15 times climate mode's
  into the middle layer), and its high cover (0.20) is near Earth's
  0.2–0.3. Climate mode keeps its water in the bottom layer:
  - the month's evaporation condenses there in the solve (ADR-0023
    §11.1);
  - Betts–Miller never moistens a layer (§4.2.5).
  So it has almost no high cloud and less greenhouse, holds more ice, and
  runs 6.4 K colder.
- **V8's gap, the missing high cloud and the middle troposphere's
  dryness in climate mode are one cause.** Task M8-09's detrainment aimed
  at it, but with climate mode counting 0.99 of its rain as convective it
  detrained all of it and warmed 25 K.

### 5. ADR-0026 option 1: Betts–Miller moistening (prototype)

`AtmosphereParameters::convective_moistening` (off: bit-identical, the
state hash unchanged): the convecting column relaxes every layer towards
RH_bm q_sat(T_ref), the deep case raining the net loss, the shallow case
scaling the reference profile to add no water
(`tests/unit/test_convective_moistening.cpp`, V1 and V2).

Climate mode, L3, year 11:

| | Before | With moistening | Reference, before |
|---|---|---|---|
| Surface temperature | 289.07 K | 292.33 K | 295.51 K |
| Middle-layer vapour | 3.8 kg/m² | 11.3 | 12.6 |
| Top-layer vapour | 1.0 | 1.1 | 1.7 |
| Greenhouse, σT_s⁴ − OLR | 154.6 W/m² | 172.3 | 177.5 |
| High cover | 0.013 | 0.037 | 0.20 |
| Cloud longwave effect | +21.6 | +26.5 | +39.2 |
| Absorbed sunlight | 235.3 | 236.4 | 248.8 |

The middle troposphere's water nearly reaches reference mode's, and the
greenhouse gap falls from 23 to 5 W/m².

Reference mode with moistening, year 11: 295.46 K (295.51 without),
cloud longwave effect +38.9, high cover 0.201, vapour 33.3 / 12.6 /
1.7 kg/m²: unchanged, as its resolved ascent and convection already lift
water. So option 1 changes climate mode only.

| Both with moistening | Climate | Reference | Gap |
|---|---|---|---|
| Surface temperature | 292.33 K | 295.46 K | **+3.1 K** (from +6.4) |
| Greenhouse, σT_s⁴ − OLR | 172.3 W/m² | 177.6 | +5.3 (from +22.9) |
| Cloud longwave effect | +26.5 | +38.9 | +12.4 |
| High cover | 0.037 | 0.201 | |
| Clear-sky absorbed sunlight (absorbed − cloud SW effect) | 282.9 | 294.2 | +11.3 |

- **Option 1 halves V8's gap.**
- **High cloud carries most of the rest** (0.04 against 0.20): options 3
  and 2.
- **The surface's albedo carries the other part.** The atmosphere absorbs
  no sunlight, so the clear-sky difference is the surface: clear-sky
  planetary albedo 0.168 against 0.135, more snow and ice on the colder
  planet (Earth about 0.15). The ice-albedo feedback amplifies whatever
  gap the clouds leave.

### 6. ADR-0026 option 3: the month's convective share (measured, not taken)

Reference mode with moistening, L3, 120 days from rest then 60 days
hourly: each cell's share of steps convecting against the parcel
buoyancy of its 60-day mean column (`BettsMillerResult::buoyancy_K`):

| Mean buoyancy (K) | Cells | Share convecting (sd) | Convective share of rain |
|---|---|---|---|
| below −8 | 137 | ~0.00 | ~0.00 |
| −8 to −6 | 56 | 0.01–0.02 | 0.02–0.04 |
| −6 to −3 | 107 | 0.04–0.07 | 0.10–0.15 |
| −3 to −2 | 25 | 0.14 (0.13) | 0.26 |
| −2 to −1 | 46 | 0.20 (0.12) | 0.42 |
| −1 to 0 | 85 | 0.46 (0.16) | 0.66 |
| 0 to 1 | 153 | 0.81 (0.09) | 0.77 |

- The relation is clean and monotone.
- It does not transfer to climate mode as it stands. Climate mode's
  month-end columns (year 11, moistening on) sit at −6 to −4 K, 65 % of
  the cell-months, because a month's step completes the relaxation
  (r ≈ 1). Reference mode's 10-minute steps relax 8 % each and keep the
  mean column near its trigger. The curve would give climate mode a share
  near 0.1 against reference mode's 0.42.
- A fitted offset would make it match, an empirical closure on three
  layers too coarse for physical anvils and with liquid anvils (task
  M8-09: +25 K). **Not taken for P0** (decided with the user, 2026-10-20).

## Conclusion

- **Question 1:**
  - reference mode's bottom layer saturates where columns do not
    convect (findings 1–3). Its whole atmosphere is too moist, mainly
    because it is warmer (finding 4);
  - three layers are too coarse for the upper troposphere's humidity
    (task M8-07);
  - five layers dry climate mode's lower and middle troposphere but not
    reference mode's (ADR-0025 §14.2).
- **Question 2:** the monthly step completes what reference mode does
  intermittently: the cloud fraction (ADR-0023 §13), the convective share
  (task M8-09; finding 6) and, by the same token, the sub-grid spread.
  The intermittency is measured (finding 6) but not carried in P0.
- **Question 3:** V8's gap was mostly climate mode carrying no water up
  (finding 4). ADR-0026 option 1, accepted, halves it to +3.1 K before
  the refit. The rest is high cloud (0.04 against 0.20) and the
  ice-albedo feedback.
- **Next (not started):** the refit with `convective_moistening` on in
  the earth_like preset (τ_d, κ_v to 288 K; the cloud constants to
  ADR-0023 V9's global targets), then V8 measured again, then a
  decision on M9.

