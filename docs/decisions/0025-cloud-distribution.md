# ADR-0025 — Where the clouds are: sub-grid condensation, the eddies' variance and convective anvils

- **Status:** Accepted, staged (2026-10-19, with the user): §4.1, §4.3,
  §4.4 and §4.5 accepted; §4.2's b_eddy accepted only after task M8-08's
  measurement (V3), within the V8 investigation
- **Date:** 2026-10-19
- **Accepted:** 2026-10-19 (staged, §9)
- **Milestone:** P0 / M8, reopened for its cloud distribution (tasks below);
  before M9
- **Context documents:** `docs/DEVELOPMENT_SPEC_v0_8.md` §13 M8, §23, §24;
  task M8-06 §Open (the measured cloud field); task M8-05 (the refit, V8)
- **Related:** ADR-0001 V2 (climate–reference parity), ADR-0011 §17 and
  ADR-0024 (the climate transport's eddies: D_k, E), ADR-0021 §4.4 and §4.5
  (rainout; one physics in both modes), ADR-0023 (moist convection, cloud
  water, cloud radiation; §11.1 condensation in the solve, §13 the
  fraction from the cloud water, §14 convective condensate through the
  cloud water, §15 V8)
- **Amends on acceptance:** ADR-0023 §11.1 and §4.3.1 (what a layer
  condenses), §4.3.4 (how cloud water evaporates), §14.2 (where convective
  condensate goes), §4.6 (the calibration targets gain a zonal profile and
  high cloud)

## 1. Context

M8 met its global cloud targets (task M8-05: cover near 0.6, cloud
radiative effects near −47 and +26 W/m², 288 K). The cloud field that
meets them is unlike Earth's (task M8-06, L4, one year after a 5-year
spin-up):

| | Model | Earth (CERES/ISCCP, roughly) |
|---|---|---|
| Cover, 20° S–20° N | 0.71–0.83 | 0.6–0.7 (the ITCZ) |
| Cover, 20–35° | 0.70–0.78 | 0.4–0.5; deserts about 0.2 |
| Cover, 50–60° (storm tracks) | 0.36–0.41 | about 0.8, the cloudiest |
| Cover, poles | 0.07–0.16 | 0.5–0.7 |
| Cloud above the bottom layer | about 0.01 | high cloud about 0.2–0.3 |
| Mean optical depth | about 3 | about 4–10 |

The global means are right because errors cancel: too much tropical and
subtropical cloud, too little at high latitudes.

**Why, in the physics.**

1. **A layer condenses only at saturation.** Since ADR-0023 §11.1 the
   solve condenses each layer's whole excess over q_sat, and §13 makes
   its cloud fraction q_l m / W_c from the cloud water so condensed. A
   layer whose mean humidity is below saturation has no cloud, whatever
   the air inside the cell does. Earth's clouds form at mean relative
   humidities of 0.6–0.9, because air within a cell and within a month
   varies.
2. **Climate mode has no sub-monthly weather.** Its eddies are
   down-gradient diffusion (ADR-0011 §17, ADR-0024), smooth by
   construction. The storm tracks' clouds come from the synoptic
   systems' ascent, which monthly means never saturate. Reference mode
   resolves those systems at L3–L6 (task M8-05: its clouds sit higher,
   §14.3, §15.3).
3. **Warm air holds most water.** With condensation only at saturation,
   cloud water follows the water supply, so it is greatest where the air
   is warmest and wettest. The subtropics' descent, which dries the air
   below saturation without removing its water, makes no difference to a
   saturation threshold the mean already misses.
4. **Convection's condensate stays where it formed.** §14 puts
   Betts–Miller's drying into each dried layer's cloud water, which is
   mostly the bottom layer's. Real deep convection carries its condensate
   up and detrains it near the top as anvils and cirrus: Earth's high
   cloud.
5. **The modes partition their rain differently.** The convective share is
   0.06 in climate mode and 0.40 in reference mode (§15.3). The climate
   month condenses its evaporation in the solve before Betts–Miller acts
   (§11.1), so that condensate counts as large-scale whatever the column
   does.

Items 2 and 4–5 are also the clouds' share of V8's +6.47 K gap. The
modes' longwave cloud effects differ by about 18 W/m², because reference
mode's clouds sit higher (§15.4).

## 2. Decision drivers

- Clouds where Earth has them, from physical state and fluxes, not a
  prescribed zonal map (CLAUDE.md: no scripted climate modifiers).
- One physics in both modes (ADR-0021 §4.5). Whatever climate mode
  parameterises, reference mode must resolve or parameterise the same
  way, so the clouds agree in the mean. ADR-0023 §13 is the lesson: a
  nonlinear diagnostic of a monthly mean splits the modes by a factor of
  three.
- Keep §13: radiation linear in each layer's cloud water below overcast.
- Few constants, each with a source and a calibration role.
- Continuous in the state, so the coupled transport's Newton keeps
  converging (ADR-0023 §11.2, ADR-0024).
- Works at three to five layers.
- Cost within V11's ×1.5 of M7.

## 3. Options considered

### 3.1 Condensation below mean saturation

**A. A statistical (sub-grid PDF) scheme — chosen.** Each layer's total
water q_t = q + q_l varies about its mean with a triangular distribution
of half-width b in the saturation deficit (Smith 1990; Sommeria and
Deardorff 1977). The equilibrium cloud water and the cloudy area follow
from the mean deficit in closed form. This is how climate models cloud
thick layers, and it recovers saturation adjustment as b → 0.

**B. Sundqvist's fraction from relative humidity.** Already tried for the
radiation: it splits the modes (ADR-0023 §13.2).

**C. A prescribed critical humidity per latitude.** A scripted modifier;
rejected.

### 3.2 The storm tracks in climate mode

**A. The eddies' variance in the PDF's width — chosen.** In climate mode
the eddies that ADR-0024 diffuses also vary the humidity and temperature
within the month. Their mixing length l = D / √E, from the zonal
circulation's diffusivity D_k and eddy kinetic energy E per band, gives
standard deviations σ_q = l |∇q| and σ_T = l |∇T|. Reference mode resolves
these eddies and adds nothing. Each mode's width is then the variability
that mode does not resolve, and the mean clouds should agree.

**B. Frontal cloud diagnosed from the eddy kinetic energy.** A
correlation, not a mechanism; it would need its own constants and has no
counterpart in reference mode.

### 3.3 High cloud

**A. Convective detrainment at the convecting column's top — chosen.**
The condensate of a convecting column is put into the cloud water of its
highest convecting layers in proportion to their Betts–Miller weights at
the top (§4.3), and rains out of them over τ_a. That gives anvils and
cirrus. It also fixes the modes' partition (§1.5), by counting all
condensate formed in a convecting column's layers as convective.

**B. An ice phase with sedimentation.** Deferred (ADR-0023 §8.2):
detrained cloud water is liquid in M8, its optics r_e's.

### 3.4 Low cloud over cold water and in the polar air

**A. The PDF alone, measured first — chosen.** A width proportional to
q_sat is a fixed relative-humidity spread, so cold saturated-in-part air
clouds as warm air does. Whether that gives enough polar and
stratocumulus cloud is measured in the first task.

**B. Stability-dependent low cloud** (Klein and Hartmann 1993: low cover
rises 0.057 per K of θ₇₀₀ − θ_s). Recorded as the fallback if 3.4 A
leaves the subtropical eastern oceans and the poles too clear (§8).

## 4. Decision

### 4.1 Sub-grid condensation (amends ADR-0023 §11.1, §4.3.1, §4.3.4)

For each layer at the solve's temperatures:

- the deficit s = a (q_t − q_sat), with a = 1 / (1 + (L / c_p) q_sat′)
  (the isobaric condensation's linearisation);
- a triangular distribution of s of half-width b; with Q = s / b, the
  equilibrium cloud water is (Smith 1990)
  - q_l* = 0 for Q ≤ −1,
  - b (1 + Q)³ / 6 for −1 < Q ≤ 0,
  - b (Q + (1 − Q)³ / 6) for 0 < Q < 1,
  - b Q for Q ≥ 1;
- the layer condenses towards q_l*: C = max(0, q_l* − q_l), inside the
  implicit balance as §11.1 does now, and its cloud water evaporates
  towards q_l* where q_l > q_l*, f_e of the difference per step (replacing
  §4.3.4's evaporation up to saturation);
- b → 0 is §11.1's saturation adjustment exactly. q_l* is continuous and
  once differentiable in the state.

The cloud fraction for the radiation stays §13's, f = min(1, q_l m / W_c):
the PDF changes how much cloud water there is and where, not how it
radiates. The PDF's own cloudy area, 0 to 1 by the same closed forms, is a
diagnostic, reported against the radiative fraction.

### 4.2 The width

b_k = sqrt(b_sub,k² + b_eddy,k²) per layer:

- **b_sub = a β q_sat,** the unresolved spread within a cell, in both
  modes, in the deficit's units. β is a calibration constant. Cloud then
  starts where the mean deficit reaches −b_sub, at a mean relative
  humidity of 1 − β (β = 0.2: 0.8, as RH_c did).
- **b_eddy, climate mode only** (provisional until M8-08's V3): a (σ_q² + (q_sat′ σ_T)²)^½, with
  σ_q = l |∂q/∂y| and σ_T = l |∂T/∂y| along the meridian, at the layer.
  l = D_k / √E from the zonal circulation's solution, at the cell's band.
  Zero where the circulation has not solved (the fallback month) and in
  reference mode. Taking σ_q and σ_T as uncorrelated is the first
  assumption to measure (V3).

### 4.3 Convective condensate and detrainment (amends ADR-0023 §14.2)

- **Convective condensate:** in a column that convects, a layer's
  condensate counts as convective in proportion to its Betts–Miller
  weight w_k, both the solve's condensation (§4.1) and the relaxation's
  drying. That is independent of the time step, which ends the split of
  §1.5.
- **Detrainment:** the column's convective condensate is moved to its
  detrainment layers. They are the layers at the top of the convecting
  column, weighted by w_k (1 − w_(k+1)), the layer whose weight the next
  layer does not carry, which is continuous in the state. It joins their
  cloud water and rains out of them over τ_a through §12.2's exact
  autoconversion, falling and re-evaporating through the layers below as
  §4.3.3 does. A share e_d of it is detrained, and the rest rains from
  the layer it formed in (e_d a calibration constant, 0.5 to start).
- **Energy:** the relaxation and the condensation applied the latent heat
  where they acted. Moving liquid water moves no latent heat. The
  column's moist enthalpy is kept exactly, and the anvil's later
  evaporation cools the layer it evaporates in.

### 4.4 Calibration (amends ADR-0023 §4.6)

The constants are τ_d, κ_v, W_c, r_e, τ_a (as now), and β and e_d. The
targets add to ADR-0023's global ones (288 K, cover near 0.6, cloud
effects −47 / +26 W/m² within 30 %):

- **the zonal annual cover** within 0.15 of CERES's in every 10° band;
- **cloud above the bottom layer** (the top two layers' random-overlap
  cover) between 0.15 and 0.35 globally;
- **the mean cloud optical depth** between 4 and 10.

The fit is recorded as M8-05's was: command, seed, level, years.

### 4.5 One physics

Both modes run §4.1 and §4.3. Only climate mode adds b_eddy, the
variability its transport parameterises and reference mode resolves. V3
tests that the two then agree.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Water and energy close to round-off with the PDF condensation and detrainment, both modes | budget tests |
| V2 | The PDF scheme: b → 0 is saturation adjustment; the closed forms' limits, continuity and derivative; q_l* rises with q_t | unit tests |
| V3 | One physics: a column forced with a prescribed humidity and temperature variability, stepped at 10 minutes, averages to the climate month whose b_eddy carries that variability (cloud water within 15 %) | unit test |
| V4 | Detrainment: a deep convecting column's condensate reaches its top layers; the column's moist enthalpy is kept; the convective share is independent of the step (10 minutes against a month, within 0.1) | unit tests |
| V5 | The zonal cover and the high-cloud target of §4.4 | calibration gate |
| V6 | ADR-0023 V9's global targets still hold after the refit | calibration gate |
| V7 | V8 measured again: the modes' longwave cloud effects within 5 W/m² (from 18), and the temperature gap reported | gate for the clouds; report for the gap |
| V8 | The coupled Newton converges as now at L3–L6; V11's time within ×1.5 of M7 | performance |
| V9 | The presentation's channels unchanged in meaning; V12 still passes | regression |

## 6. Consequences

- The cloud field follows Earth's pattern from physics the two modes
  share, and the V8 gap loses its cloud share if V7 passes.
- The climate changes. ADR-0023's fit is redone, and the golden runs and
  the climate numbers recorded in M8-05 change.
- PSNAP is unchanged: no new state; the width is a function of the state
  and the month's circulation. The PDF's cloudy area is a new derived
  diagnostic field.
- Two calibration constants are added (β, e_d); ADR-0023's RH_c and f_conv
  stay gone (§13).
- The stability fallback (§3.4 B) is a further option if V5 fails at the
  subtropical east coasts or the poles.

## 7. Milestone mapping

P0 / M8, reopened. M9 waits for these tasks and the V8 investigation, as
decided with the user (2026-10-19).

## 8. Open questions

1. Is the triangular PDF enough, or does the skewed distribution of
   convective air need a skewed PDF (Tompkins 2002)? Measured in V3 and V5.
2. Are σ_q and σ_T uncorrelated? Synoptic eddies carry warm moist air
   poleward together, which narrows the deficit's spread. V3 measures it
   against reference mode.
3. Does the PDF give the poles and the subtropical eastern oceans enough
   low cloud, or is §3.4 B needed?
4. Should detrained cloud water be advected (ADR-0023 §8.1)? Anvils spread
   hundreds of kilometres from their convection.

## 9. Tasks, in order (staging decided with the user, 2026-10-19)

1. **M8-07 — Sub-grid condensation:** §4.1 and b_sub; V1, V2. The coupled
   Newton checked at L6 first; prototype measurements of the zonal cover
   at L3 and L4.
2. **M8-09 — Convective condensate and anvils:** §4.3; V4; the high
   cloud and the convective share measured in both modes. Then a look at
   the cloud field and its pattern.
3. **The V8 investigation**, with **M8-08 — The eddies' variance**:
   b_eddy from the zonal circulation in climate mode; V3; the modes'
   clouds measured against each other. §4.2's b_eddy is accepted only if
   V3 supports it. About 5 K of V8's gap predates the clouds (ADR-0023
   §15.4), so the investigation looks beyond them too.
4. **M8-10 — Refit and gates:** one refit against the final physics,
   §4.4; V5–V9, including V8 measured again.

## 10. References

- Klein, S. A., and Hartmann, D. L. (1993). The seasonal cycle of low
  stratiform clouds. *J. Climate* 6, 1587–1606.
- Loeb, N. G., et al. (2018). Clouds and the Earth's Radiant Energy System
  (CERES) EBAF Ed4.0. *J. Climate* 31, 895–918.
- Rossow, W. B., and Schiffer, R. A. (1999). Advances in understanding
  clouds from ISCCP. *Bull. Amer. Meteor. Soc.* 80, 2261–2287.
- Smith, R. N. B. (1990). A scheme for predicting layer clouds and their
  water content in a general circulation model. *Q. J. R. Meteorol. Soc.*
  116, 435–460.
- Sommeria, G., and Deardorff, J. W. (1977). Subgrid-scale condensation in
  models of nonprecipitating clouds. *J. Atmos. Sci.* 34, 344–355.
- Tompkins, A. M. (2002). A prognostic parameterization for the subgrid-scale
  variability of water vapor and clouds in large-scale models and its use
  to diagnose cloud cover. *J. Atmos. Sci.* 59, 1917–1942.
