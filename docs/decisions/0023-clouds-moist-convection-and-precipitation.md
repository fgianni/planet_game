# ADR-0023 — Cloud water, moist convection, precipitation and cloud radiation

- **Status:** Accepted
- **Date:** 2026-10-15
- **Accepted:** 2026-10-16
- **Amended:** 2026-10-16 — §4.2: condensation inside the column solve and a continuous onset of convection; §5: V7 tests stability, the climatological latent heating moves to V9; the start from rest on the winds' orography (§11, task M8-02; accepted 2026-10-17)
- **Milestone:** P0 / M8 (clouds and precipitation)
- **Context documents:** `docs/DEVELOPMENT_SPEC_v0_8.md` §13 M8, §23, §24, §31.2 (the cloud channels); Planetary Civilization Simulator — Design Record v1.4, §5, §15
- **Related:** ADR-0001 (V1 water budget, V2 climate–reference parity), ADR-0003 (PSNAP migration), ADR-0006 (sub-steps), ADR-0008 (snow, rain and snow at the surface), ADR-0010 (layered atmosphere, grey longwave, Γ_c adjustment, §3.3 C deferring Betts–Miller to M8), ADR-0011 (winds, ascent), ADR-0018 (presentation channels `cloud_cover`, `cloud_thickness`), ADR-0021 (vapour, evaporation, saturation rainout; §13.7 V9 deferred to M8)
- **Amends on acceptance:** ADR-0010 §3.3 B (the dry adjustment to Γ_c becomes a dry adjustment to the dry adiabat, and moist convection replaces Γ_c's role); ADR-0021 §4.4 (condensate becomes cloud water, not precipitation at once), §10.3 (the condensation pass after convection is removed) and §13 (τ_c is removed); ADR-0010 §3.2 B and ADR-0021 §4.6 (the grey optical depth gains a cloud term; the shortwave meets the atmosphere for the first time); PSNAP schema 6 → 7; presentation snapshot schema 5 → 6

## 1. Context

M7 made water a state: vapour in every layer, evaporation into the bottom
one, a land bucket, humidity carried by the winds and the climate step,
and saturation rainout (ADR-0021). Specification §13 gives M8
"condensation, physical cloud water, latent atmospheric heating,
precipitation and simplified cloud-radiation coupling. Godot renders
authoritative cloud state."

### 1.1 What the repository has (audit, 2026-10-15, at `9fc1db4`)

- **State (PSNAP 6):** per layer temperature and specific humidity, the
  land bucket, snow and sea ice, surface tiles' temperatures. No cloud
  water: condensate falls the moment it forms (ADR-0021 §4.4).
- **Convection:** a dry adjustment of adjacent layers to the critical lapse
  rate Γ_c = 6.5 K/km, which stands for the moist adiabat (ADR-0010 §3.3 B),
  followed by a condensation pass that alternates with it until nothing more
  condenses (ADR-0021 §10.3). Reference mode condenses 1 − exp(−Δt/τ_c) of the
  excess per step, τ_c = 12 h, because condensing at once fed a grid-scale
  moist instability (latent heating of 1 300–2 900 W/m², winds above
  150 m/s; ADR-0021 §13).
- **Radiation:** grey longwave, τ(p) = τ_d p/p₀ + κ_v W(p); the shortwave
  passes the atmosphere untouched and only the surface reflects.
- **Calibration:** τ_d = 1.0220, κ_v = 0.00511 m²/kg give 288 K, a 53 K
  equator-to-pole difference (P2 fit) and about 1 m/yr of precipitation.
- **Gates handed to M8:** ADR-0021 §13.7: V9 (climate–reference parity,
  ADR-0001 V2) fails, +5.45 K against 0.3 K and ×1.29 in precipitation
  against ±10 %; the 42 K equator-to-pole target (with M11).
- **Presentation:** the channels `cloud_cover` and `cloud_thickness` are
  reserved (ADR-0018, R4) and absent.

### 1.2 Decided with the user before this record (2026-10-15)

1. Moist convection is a Betts–Miller relaxation, one physics in both modes.
2. Cloud water is a prognostic state per layer, not advected at first.
3. Clouds reflect shortwave by their water path and add to the longwave
   optical depth, with a cloud fraction from humidity; the constants are
   refitted with τ_d and κ_v.
4. The cloud channels are filled from the authoritative state and drawn by
   both styles, with a readability gate.

## 2. Decision drivers

1. **Conservation is exact.** Water (vapour, cloud water, precipitation,
   the surface stores) and energy (sensible, latent, radiative) close to
   round-off in every step, as in M7 (ADR-0001 V1).
2. **One physics in both modes.** V9 needs climate and reference mode to
   differ only by their time step. Every process with a time scale is
   written as a relaxation by 1 − exp(−Δt/τ), which is complete in a
   climate month and gradual in a reference step.
3. **Stable at the reference step.** The moist instability that τ_c held
   back must not return.
4. **Physical constants with sources,** a few calibration constants with a
   recorded fit, no climate modifiers.
5. **Performance.** The 250-year L6 run takes 1 350 s on the reference
   machine; M8 may not double it.
6. **Room for M9–M12:** soil water, ocean, aerosols (cloud droplet size) and
   ice clouds plug in without reshaping the column.

## 3. Options considered

### 3.1 Moist convection

**A. Betts–Miller relaxation — chosen.** Where a parcel lifted from the
bottom layer is buoyant, temperature and humidity relax towards a moist
adiabatic reference profile over τ_bm (Betts 1986; Betts and Miller 1986),
with the reference shifted so the column's enthalpy is conserved. It has a
time scale, so one formula serves a month and an hour.

**B. Moist convective adjustment (Manabe et al. 1965).** Instantaneous; at
the reference step it is the grid-scale release that needed τ_c.

**C. A mass-flux scheme.** Closer to observations, far more constants, and
its benefits are below what three to eight layers resolve.

### 3.2 Cloud water

**A. A prognostic cloud water q_l per layer — chosen.** Large-scale
condensation feeds it, autoconversion drains it to precipitation, it
evaporates in subsaturated air. Not advected at first (§8): its life,
hours, is short against the transport's time across a cell at L6.

**B. Diagnostic clouds** from relative humidity alone. No state, but no
"physical cloud water" either, as specification §13 asks.

### 3.3 Cloud radiation

**A. Water path, fraction and two simple optics — chosen.** Cloud
fraction from relative humidity (Sundqvist et al. 1989); shortwave
reflection from the layer's optical depth by a non-absorbing two-stream
formula; longwave as an absorption by the cloud water, added to the grey
optical depth. Each has a source and one or two constants.

**B. A banded radiation code.** Beyond P0, and the grey longwave would not
use its detail.

## 4. Decision

### 4.1 State

- **Cloud water** q_l,k (kg/kg) in every layer, beside q_k. PSNAP 6 → 7;
  migration sets q_l = 0 (the M7 world holds no condensate).
- Precipitation stays a flux of the step, not a state.

### 4.2 Moist convection (Betts–Miller)

Each step, after the column solve:

1. **Dry adjustment** of layers whose lapse rate exceeds the dry adiabat
   g/c_p, mixing to neutral and conserving enthalpy (ADR-0010 §3.3 B with
   Γ_d = g/c_p in place of Γ_c).
2. **The parcel:** the bottom layer's air lifted along its pseudo-adiabat
   (conserving moist static energy h = c_p T + g z + L_v q, saturating at its
   lifting condensation level). Layers where it is warmer than the
   environment form the convecting column, from the bottom to the highest
   buoyant layer. A column with fewer than two buoyant layers does not
   convect.
3. **The reference profile:** T_ref,k on the parcel's moist adiabat;
   q_ref,k = RH_bm q_sat(T_ref,k, p_k) with RH_bm = 0.8 (Betts and Miller
   1986). T_ref is shifted by one constant over the convecting column so
   that Σ m_k c_p (T_ref,k − T_k) + L_v Σ m_k (q_ref,k − q_k) = 0, the
   column's moist enthalpy unchanged.
4. **Relaxation:** T_k += (T_ref,k − T_k) r and q_k += (q_ref,k − q_k) r
   with r = 1 − exp(−Δt/τ_bm), τ_bm = 2 h. The water the column loses,
   Σ m_k (q_k − q_new,k) when positive, is **convective precipitation**; its
   latent heat is the heating the relaxation already applied.
5. Convection drying an over-moist column below q_ref moistens nothing:
   q_new ≤ q. Where the shift would need it, the reference is left
   unshifted for humidity and only temperature relaxes (the shallow case of
   Betts 1986).

This replaces Γ_c in its role as the moist adiabat, the condensation pass
of ADR-0021 §10.3, and τ_c.

### 4.3 Large-scale condensation, cloud water and precipitation

1. **Condensation:** a layer still supersaturated after convection
   condenses its excess isobarically into cloud water,
   c_p ΔT = L (q − q_sat), exactly (ADR-0021 §10.3's formula, once).
2. **Autoconversion** (Kessler 1969): cloud water above q_l0 converts to
   precipitation, dP_k = (q_l,k − q_l0) (1 − exp(−Δt/τ_a)) m_k, with
   q_l0 = 0.3 g/kg and τ_a = 1 h (calibration, §4.6). A climate month
   reaches the steady state q_l = q_l0, all condensate above it raining.
3. **Falling and re-evaporation:** precipitation leaving a layer enters the
   one below and evaporates into it at
   E_r = ε (1 − RH_k) P (1 − exp(−Δt/τ_e)), never beyond saturation, with
   ε = 0.5 and τ_e = 1 h (calibration); it cools the layer by L E_r.
4. **Cloud water evaporates** where its layer is subsaturated, up to
   saturation, the same way.
5. **Phase:** precipitation reaching the surface falls as snow where the
   surface is at or below 0 °C (ADR-0008); condensate does not freeze in the
   column in M8 (no ice clouds, §8), but snow's L_f is charged as in
   ADR-0021 §10.2.

### 4.4 Cloud fraction and radiation

1. **Fraction** (Sundqvist et al. 1989):
   f_k = 1 − sqrt((1 − RH_k) / (1 − RH_c)) for RH_k > RH_c, else 0, with
   RH_k counting cloud water, (q + q_l)/q_sat, and RH_c = 0.8 (calibration);
   f_k = 1 at saturation. Convecting layers carry at least f_conv = 0.2.
2. **Optical depth:** τ_c,k = 3 LWP_k / (2 ρ_w r_e), LWP_k = q_l,k m_k / f_k
   the in-cloud water path, r_e = 10 µm (Stephens 1978).
3. **Shortwave:** a layer reflects R_k = f_k (1 − g) τ_c,k / (2 + (1 − g) τ_c,k),
   g = 0.85, the non-absorbing two-stream limit; layers combine by random
   overlap, T = Π (1 − R_k). The surface receives S T; light the surface
   reflects leaves to space (no second bounce in M8). This is the
   atmosphere's first shortwave term.
4. **Longwave:** the grey optical depth gains κ_l f_k LWP_k per layer, with
   κ_l = 0.13 m²/g (Stephens 1978; liquid water's effective absorption), so
   a cloud of 50 g/m² is nearly black.

### 4.5 One physics in both modes

Every time scale above (τ_bm, τ_a, τ_e) enters as 1 − exp(−Δt/τ). A
climate month takes each process to its limit; a reference step takes it
part way. The physics and the constants are the same: V9 then measures the
time step, as ADR-0001 V2 intends. The diurnal cycle of evaporation, which
climate mode averages over the month (ADR-0021 §13.7), stays a known
difference, reported by V9.

### 4.6 Calibration

The calibration constants are τ_d, κ_v, RH_c, q_l0 and r_e. Targets: a
288 K global mean (ADR-0021's), a global cloud fraction near 0.6 and a
cloud radiative effect near −47 W/m² shortwave and +26 W/m² longwave
(CERES; Loeb et al. 2018), the P2 equator-to-pole difference reported
against 42 K. The fit is recorded as M7's was: command, seed, level, years.

### 4.7 Diagnostics

The global and per-cell budgets gain cloud water; the run reports
convective and large-scale precipitation, cloud fraction (global and
zonal), the cloud radiative effect (shortwave, longwave) and the column's
moist enthalpy change by convection (zero to round-off).

### 4.8 Presentation

The presentation snapshot (schema 5 → 6) gains the column cloud fraction
(random overlap) and the cloud optical depth. The channels:

- `cloud_cover` = the column cloud fraction, 0–1;
- `cloud_thickness` = 1 − exp(−τ_c,col / 10), normalised optical depth.

Both styles draw clouds from them (R4): a cloud layer above the surface
whose opacity follows `cloud_cover` and whose brightness and density follow
`cloud_thickness`, in every altitude band, procedural detail keyed by
position (ADR-0022 §4.9). PSNAP is unchanged by the presentation part.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Water closes to round-off: vapour, cloud water, precipitation, surface stores, per step and per run, both modes | budget tests |
| V2 | Energy closes: convection conserves the column's moist enthalpy; condensation, re-evaporation and their latent heat balance | unit tests |
| V3 | The dry adjustment acts only on super-adiabatic layers | unit test |
| V4 | Betts–Miller: a buoyant column relaxes towards the moist adiabat by 1 − exp(−Δt/τ_bm); a climate month reaches the reference; a stable column is untouched | unit tests |
| V5 | Autoconversion reaches q_l0 in a month; re-evaporation never supersaturates a layer | unit tests |
| V6 | Clouds: reflection rises with water path and fraction; the shortwave cloud effect is negative and the longwave positive | unit and run tests |
| V7 | Reference mode stays stable without τ_c: winds below 100 m/s and latent heating below 300 W/m² over a 10-year run (read as §11.3) | regression |
| V8 | ADR-0021 V9 (ADR-0001 V2): climate and reference mode within 0.3 K and ±10 % precipitation (L3, 10-year spin-up, a year each) | gate |
| V9 | 288 K after the refit; cloud fraction 0.5–0.7; cloud radiative effects within 30 % of §4.6's targets; the equator-to-pole difference reported against 42 K; each cell's climatological latent heating reported against 300 W/m² (§11.3) | calibration gate |
| V10 | PSNAP 6 → 7 migration and golden saves; presentation schema 6 round trip | regression |
| V11 | The performance gates hold (ADR-0021 §13.6): 250 years at L5 and L6 within 1.5 times M7's times | performance |
| V12 | Cloud channels move the right way (a cloudy vs clear snapshot) and clouded vs clear terrain passes the readability gate in both styles | unit tests, readability harness |

## 6. Consequences

- Precipitation has two physical sources and re-evaporates; the dry belts
  and the wet tropics come from ascent and convection, not from saturation
  alone.
- The atmosphere reflects shortwave, so the planet's albedo has a cloud
  part, and cloud feedbacks exist (the design's climate levers need them).
- τ_c and the condensation pass go; one relaxation formula serves both
  modes.
- PSNAP grows by one layered field.
- The 42 K target may still need M11's ocean transport; V9 reports it.

## 7. Milestone mapping

| Milestone | Uses this record for |
|---|---|
| M8 | everything above |
| M9 | precipitation into soil and rivers; snowmelt |
| M10 | rainfall recycling through evapotranspiration |
| M11 | ocean heat transport against the 42 K target |
| M12 | aerosols changing r_e (indirect effect); CO₂ in the longwave |
| R4 | the cloud channels drawn |

## 8. Open questions

1. Is cloud water's advection needed at L6, or is its short life enough?
   M8-05 measures the loss against an advected test.
2. Ice clouds (Bergeron, sedimentation of ice) and their longwave: M8 or
   later?
3. Convective detrainment into anvil cloud water, which would give high
   cloud from deep convection.
4. Climate mode's evaporation from monthly means against the diurnal cycle
   (ADR-0021 §13.7): if V8 still fails after M8, is a diurnal correction to
   climate-mode evaporation acceptable, or is the gate relaxed?

## 9. Proposed tasks

1. **M8-01 — Cloud water state:** q_l per layer, PSNAP 7 and migration,
   budgets, diagnostics (V1, V10).
2. **M8-02 — Moist convection:** the dry adjustment to the dry adiabat and
   Betts–Miller, replacing Γ_c's role, the condensation pass and τ_c
   (V2–V4, V7).
3. **M8-03 — Cloud water to precipitation:** large-scale condensation into
   cloud water, autoconversion, falling and re-evaporation (V1, V5).
4. **M8-04 — Cloud radiation:** fraction, shortwave reflection, longwave
   absorption (V6).
5. **M8-05 — Refit and gates:** the calibration, V8 parity, V9, V11
   performance; the open questions measured.
6. **M8-06 — Clouds on screen:** presentation schema 6, the two channels and
   their headless tests, the cloud layer in both styles, readability
   (V12; specification §31.10's R4 cloud part).

## 10. References

- Betts, A. K. (1986). A new convective adjustment scheme. Part I. *Q. J. R. Meteorol. Soc.* 112, 677–691.
- Betts, A. K., and Miller, M. J. (1986). A new convective adjustment scheme. Part II. *Q. J. R. Meteorol. Soc.* 112, 693–709.
- Kessler, E. (1969). *On the distribution and continuity of water substance in atmospheric circulations.* Meteorol. Monogr. 32.
- Loeb, N. G., et al. (2018). Clouds and the Earth's Radiant Energy System (CERES) EBAF Ed4.0. *J. Climate* 31, 895–918.
- Manabe, S., Smagorinsky, J., and Strickler, R. F. (1965). Simulated climatology of a general circulation model with a hydrologic cycle. *Mon. Wea. Rev.* 93, 769–798.
- Stephens, G. L. (1978). Radiation profiles in extended water clouds. II. *J. Atmos. Sci.* 35, 2123–2132.
- Sundqvist, H., Berge, E., and Kristjánsson, J. E. (1989). Condensation and cloud parameterization studies with a mesoscale numerical weather prediction model. *Mon. Wea. Rev.* 117, 1641–1657.

## 11. Amendment: condensation in the solve, a continuous onset, V7's reading (proposed 2026-10-16, accepted 2026-10-17, task M8-02)

**11.1 Condensation inside the column solve (amends §4.2 and §4.3.1).**
§4.2 orders each step as the column solve, then convection, then
condensation. In climate mode that order fails:
- the month's evaporation enters the implicit solve as vapour without its
  latent heat;
- the transport's Newton then drove one L3 column's bottom layer to 7 K,
  under a −1 047 W/m² source, in the first spin-up month.

The solve therefore condenses the whole excess at its new temperatures,
inside the implicit balance, as M7 did with τ_c's fraction of one for a
month. After it come the dry adjustment, Betts–Miller and the single
condensation pass of §4.3.1, which takes what convection leaves
supersaturated. There is still no τ_c: the solve's condensation is
complete in both modes. From M8-03 that condensate becomes cloud water
(§4.3), wherever it forms.

**11.2 A continuous onset (amends §4.2.2 and §4.2.3).** §4.2.2's trigger
admits a layer where the parcel is warmer than it, which switches a moist
column's drying on at full strength. The monthly transport solve then has
no root: its coupled Newton reached its 30-iteration limit in almost every
month, ending at 0.01–7 W/m² against 1e-4.
- Each layer k ≥ 1 joins with weight w_k = smoothstep(b_k / 1 K), b_k the
  parcel's buoyancy against it, and no more than the layer below. The
  bottom layer joins as layer 1. A column convects when some layer above
  the bottom has w > 0.
- The shift s of the reference solves
  Σ w_k (c_p (T_ref,k − T_k) + L min(0, q_ref,k − q_k)) m_k = 0, so the
  moist enthalpy is kept and no layer is moistened in one formula; where
  nothing dries, only temperature relaxes (§4.2.5's shallow case).
- Each layer relaxes by r w_k, r = 1 − exp(−Δt/τ_bm).

Newton then converges in 4–7 iterations to 1–7e-5 W/m², as in M7.
Sweeping a layer through the parcel's temperature in 1 mK steps moves
every layer by at most 0.022 K, where the step trigger jumped by about
19 kg/m² of rain.

**11.3 V7 tests stability; the climatological latent heating is
calibration (amends §5; decided with the user).** V7 guards against the
grid-point runaway that τ_c held back: latent heating of 1.3–2.9 kW/m²
sustained, winds above 150 m/s (ADR-0021 §13.4). It now reads:
- winds below 100 m/s over the whole 10-year run;
- after the first year's start-up, every cell's daily-mean latent heating
  below 100 mm/day of rain (L_v × 100 kg/m² per day, 2.9 kW/m²), a
  tropical-cyclone day at the cell's scale.

A cell's climatological latent heating, against Earth's wettest ~800 km
boxes at 300–350 W/m², measures the climate, not the scheme's stability.
At M8-02 it is 408 W/m², because the climate state's tropics are about
20 K too hot (sea at 307–309 K within 15°). V9 reports it against
300 W/m² after the clouds and the refit (M8-04, M8-05).

**11.4 Reference mode starts on the winds' orography (records a fix to
ADR-0011's start).** The start from rest left p_s on the true terrain
while the winds see the smoothed one; at L3 a 3 km plateau cell at
77.8 kPa beside a 742 m smoothed surface shed the difference in gravity
waves of about 10 kPa. The start from rest now puts p_s on the winds'
orography with the column mass held, as the balanced start already did.
Both starts adjust by less than 2 kPa in two days
(`test_climate_circulation`).

**11.5 Γ_c's remaining role.** Betts–Miller replaces Γ_c as the moist
adiabat. Γ_c still sets the climate circulation's static stability
(`zonal_coupling.cpp`) until the refit (M8-05) replaces it with the
moist profile's or records why it stays.

**11.6 Results at M8-02** (L3, `test_reference_moist_convection 10`, from
rest; re-run 2026-10-16 with §11.3's gate): winds 94 m/s in year 1's
transient, then 54–70 m/s; the highest daily-mean latent heating in any
cell after year 1 is 1 288 W/m², against the 2 894 W/m² bound; V7 as read
in §11.3 passes. The climatological maximum, now V9's, is 408 W/m². Parity (V8): +4.98 K and ×1.30 in precipitation (M7:
+5.45 K, ×1.29), M8-05's gate. A climate year at L3 costs ×1.41 M7's
(V11 allows ×1.5).

