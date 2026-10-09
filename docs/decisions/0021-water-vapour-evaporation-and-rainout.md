# ADR-0021 — Water vapour, evaporation, the land bucket and saturation rainout

- **Status:** Accepted
- **Date:** 2026-10-09
- **Accepted:** 2026-10-09
- **Amended:** 2026-10-09 — §4.3: the bulk formula's humidity is the surface air's (§10, decided with the user in task M7-03); §4.4: the condensate's freezing and the saturation adjustment after convection (§10)
- **Milestone:** P0 / M7 (humidity and evaporation)
- **Context documents:** `docs/DEVELOPMENT_SPEC_v0_8.md` §9.3, §9.6, §13 M7–M9, §23, §24; Planetary Civilization Simulator — Design Record v1.4, §5, §15
- **Related:** ADR-0001 (V1 water budget, V2 parity, §8 "first V2 parity tests" at M7), ADR-0003 (PSNAP migration), ADR-0006 (sub-steps), ADR-0007 (surface tiles), ADR-0008 (snow, sea ice, the prescribed-precipitation seam), ADR-0009 (implicit transport), ADR-0010 (layered atmosphere; τ's quartic vapour proxy; Γ_c), ADR-0011 (winds; "humidity advected by the same two paths"; §17's coupled transport)
- **Amends on acceptance:** ADR-0008 §3.3 (model precipitation replaces the prescribed field on the Earth-like planet); ADR-0010 §3.2 B (τ's quartic term becomes the vapour path) and §4.4 (the column solve gains latent terms); ADR-0011 §17.1 (the climate step gains a humidity transport); PSNAP schema 5 → 6

## 1. Context

The atmosphere (ADR-0010) and its winds (ADR-0011) are dry. The surface
loses heat only by radiation and sensible exchange; the poleward transport
is the dry overturning and eddies, 2.8 PW, so the equator-to-pole difference
is 52 K against Earth's 42 K (ADR-0011 §17.7); precipitation is a prescribed
forcing, zero by default (ADR-0008 §3.3 B). Specification §13 gives M7
"saturation, evaporation, humidity transport, latent surface cooling and
global water diagnostics", and M8 condensation, clouds and precipitation.

### 1.1 What the repository has (audit, 2026-10-09, at `af99b99`)

- **Slow state:** hypsometry, sea level, two tiles' temperatures (land
  surface and ground, ocean mixed layer and deep), snow and sea-ice mass,
  and the atmosphere's p_s and N layer temperatures. PSNAP schema 5, an
  ordered migration chain, golden saves per schema.
- **The column solve** (`solve_atmosphere_column`) is one implicit Newton
  per cell over the N layers, the land and ocean tiles inside it (their
  upward longwave and sensible exchange), and the transported source into
  the bottom layer. Its response dT_k/dh feeds the coupled transport.
- **Latent heat exists for snow melt and sea ice** (ADR-0008), with water
  budgets for snow and ice that close to rounding.
- **Climate mode** carries heat by the zonal overturning's layer mass fluxes
  (upwind) and the eddies' diffusion (ADR-0011 §17); **reference mode**
  integrates the primitive equations with flux-form transport of mass and
  θ. Neither carries a tracer yet.
- **The surface wind** exists in climate mode as a derived field (the
  circulation's bottom layer, task M6-04) and in reference mode as the fast
  state.

### 1.2 Decided with the user before this record (2026-10-09)

- **M7 closes the water cycle with saturation rainout:** vapour above
  saturation in a layer condenses, releases its latent heat in that layer
  and falls at once. M8 adds cloud water, re-evaporation of falling
  precipitation, moist convection and cloud radiation on top.
- **Land water is a Manabe (1969) bucket** until M9 grows it into soil.

## 2. Decision drivers

1. **Water and energy close exactly** in every step and mode (ADR-0001 V1;
   specification §9.3: evaporation removes latent energy from its surface,
   condensation releases it into the air).
2. **Physical state, not modifiers:** humidity is a field; τ follows it; no
   prescribed relative humidity.
3. **Monthly stability:** the climate step is a month. Evaporation's
   temperature dependence is strong (≈ 10 W/m²/K over warm ocean), so it
   must be implicit where heat capacities are small (land, the air).
4. **One more tracer, the same paths** (ADR-0011): the overturning and
   eddies in climate mode, the resolved winds in reference mode.
5. **Room for M8–M9:** clouds, moist convection and soil plug in without
   moving the state.
6. **The budget:** the 250-year gates are at 219 s of 240 (L5) and 588 s of
   600 (L6).

## 3. Options considered

### 3.1 Where condensation enters the energy solve

| Option | For | Against |
|---|---|---|
| A. After the step, as an adjustment | Simple | A month's latent heating (≈ 80 W/m² globally) arrives without the radiation that balances it: tens of kelvin in one step |
| **B. Inside the column solve** | Implicit: warmer layers condense less, so the latent heating is balanced within the month | The column Newton gains a piecewise term (condensation switches on at saturation) |

**B** is chosen.

### 3.2 Coupling humidity transport to the column physics in climate mode

| Option | For | Against |
|---|---|---|
| A. Fully coupled (humidity in the transport Newton) | Consistent | N more unknowns per group; the coupled Newton would be rebuilt (the reason ADR-0011 §17.1 kept one unknown) |
| **B. Split: transport first, then the columns** | The transport is a linear implicit tracer solve with fixed fluxes; evaporation and condensation stay local and implicit in each column | Horizontal moisture convergence lags the month's evaporation by one step |

**B** is chosen: it is ADR-0011 §17's split, applied to a tracer.

### 3.3 Radiation

ADR-0010 §3.2 B's quartic term in τ(p) stands for vapour. It becomes the
vapour path above each level, so τ = τ_d (p/p₀) + κ_v · W(p), W the water
vapour column above p: τ_d stands for the well-mixed absorbers (CO₂, M12's
lever) and κ_v is a mass absorption coefficient. The column's emissivities
then follow humidity, which gives the water-vapour feedback.

## 4. Decision

### 4.1 State

New registry group `0x0007`:

| Field | Partition | Layout, type | Unit |
|---|---|---|---|
| `atmosphere_specific_humidity_kg_kg` | slow | cell × N, `float64` | kg/kg |
| `land_surface_water_kg_m2` | slow | cell, `float64` | kg/m² (the bucket) |
| `precipitation_kg_m2_s` | derived | cell, `float32` | the step's mean rain and snow |
| `evaporation_kg_m2_s` | derived | cell, `float32` | the step's mean |
| `runoff_kg_m2_s` | derived | cell, `float32` | bucket overflow, to the ocean |
| climatology of precipitation and evaporation | climatology | cell × 12 | kg/m²/s |

- PSNAP **schema 6**, with a migration step 5 → 6 whose initialisers are
  declared (ADR-0003 §3.6): humidity at 60% relative humidity of each layer's
  temperature (the classic profile's mid value; spin-up erases it), and the
  bucket half full on land.
- The water reservoirs are `float64`, as snow and ice.

### 4.2 Saturation

- q_sat(T, p) = ε e_s / (p − (1 − ε) e_s), with e_s over water above 0 °C
  and over ice below (Bolton, 1980; Murphy and Koop, 2005, for ice), and
  the matching latent heats L_v and L_s.
- One function, used by evaporation, condensation and the presentation.

### 4.3 Evaporation and the bucket

- **Bulk formula** per tile: E = ρ_s C_E V_e β (q_sat(T_s) − q_0), with
  q_0 the bottom layer's humidity, C_E the tile's drag coefficient
  (ADR-0011 §4.3's C_D), V_e = |V_bottom| + w_g, w_g = 5 m/s gustiness
  (the monthly mean wind misses the synoptic wind).
- **Ocean:** β = 1 on open water; sublimation (β = 1, L_s) over sea ice.
- **Land:** β = min(1, W / (0.75 W_max)) with W the bucket, W_max = 150 mm
  (Manabe, 1969); over snow, sublimation from the snowpack (β = 1, L_s).
- **The bucket:** rain and snowmelt fill it; evaporation empties it;
  overflow above W_max runs off to the ocean (`runoff`). Its water is booked;
  M9 replaces the instant runoff with rivers.
- **Implicit:** E enters each tile's energy balance as −L E with its
  dependence on T_s, inside the column solve, so a month's step is stable
  on land and ice.

### 4.4 Saturation rainout

- After the month's horizontal transport, each layer holds q*_k; the
  bottom layer adds the month's evaporation, q*_0 += E Δt / m_0.
- Inside the column solve, each layer condenses
  C_k = max(0, q*_k − q_sat(T_k, p_k)) and gains L C_k m_k / Δt. It is a
  function of the unknown T_k, so the solve balances latent heating with
  radiation; the switch at saturation is the column Newton's kink, handled
  as its existing phase-change kinks.
- **Precipitation** is the column's Σ C_k m_k / Δt. It falls as snow where
  the surface is at or below 0 °C (ADR-0008's rule, now fed by the model)
  and as rain otherwise; snow's L_f is ADR-0008's.
- **No precipitation re-evaporates** and no cloud water is stored (M8).

### 4.5 Transport

- **Climate mode:** before the column solve (§3.2 B), one implicit
  flux-form upwind solve per layer for q with the month's fixed overturning
  mass fluxes and vertical fluxes (ADR-0011 §4.4), and the eddies'
  diffusion with D_e — linear, on the coarse graph (BiCGSTAB, the coupled
  transport's preconditioner). Its column sum conserves water exactly.
- **Reference mode:** q is advected by the primitive-equation core, flux
  form with a limiter (ADR-0002 §4.2), in each RK3 stage; the column
  physics evaporates and rains out every reference step.
- The latent energy flux L q F is carried implicitly: the water moves and
  its heat is released where it condenses. With it the equator-to-pole
  target of 42 K (ADR-0011 §17.7) becomes the calibration's again.

### 4.6 Radiation

- τ(p) = τ_d (p/p₀) + κ_v W(p), with W the column's vapour mass above p.
- τ_d and κ_v are calibration constants (§4.8); the quartic term and its
  fraction f_l (ADR-0010 §3.2 B) are retired.
- Shortwave absorption by vapour is not added (M8 does the shortwave).

### 4.7 Diagnostics

- Global water: atmosphere, bucket, snow, sea ice, ocean (the reference
  inventory), and the step's E, P and runoff; the budget closes to rounding
  (ADR-0001 V1).
- Energy: the latent terms in the surface and column budgets (ADR-0007 V2).

### 4.8 Calibration

τ_d, κ_v and c_E are fitted jointly to 288 ± 0.5 K and 42 ± 1 K at L4,
seed 1, N = 3 (specification §24), with the global precipitation recorded
against Earth's ~1 m/yr (not fitted). C_E, w_g, W_max and the 75% bucket
threshold are documented constants, not fits.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | e_s against reference tables (Hyland–Wexler over water, Murphy–Koop over ice), −60 to 40 °C | 0.2% (Bolton is 0.14% high at 40 °C) |
| V2 | Water budget of every step and mode: Δ(atmosphere + bucket + snow + ice) = E − P, P − E − runoff into the ocean | rounding |
| V3 | Energy budget with latent terms (ADR-0007 V2) | ADR-0007 V2 gate |
| V4 | A single column: evaporation into a dry column saturates and rains out to radiative–convective balance; condensation stops above saturation | analytic limits |
| V5 | Implicit stability: a monthly step over a wet, low-heat-capacity land tile does not oscillate | no sign change of ΔT between iterations |
| V6 | Transport: a uniform q with divergence-free column fluxes stays uniform; the tracer's column sum is conserved | rounding |
| V7 | Climate mode, Earth-like, L5, decade: global precipitation 0.7–1.3 m/yr; an ITCZ maximum within 15° of the equator; subtropical minima | recorded first, gated after the refit |
| V8 | Calibration: 288 ± 0.5 K and 42 ± 1 K | §4.8 |
| V9 | ADR-0001 V2 parity, climate against reference (first gates): mean temperature ± 0.3 K, precipitation ± 10% | ADR-0001 V2 |
| V10 | Migration 5 → 6 and the schema-6 golden save | load and step ten years |
| V11 | Determinism: workers 1/2/8, replay | bit-identical |
| V12 | The 250-year gates | L5 ≤ 240 s, L6 ≤ 600 s |

## 6. Consequences

- The surface gains its largest cooling term; the surface–air difference
  falls towards Earth's.
- The water-vapour feedback appears through τ, so climate sensitivity
  changes and every calibrated number moves (specification §23 lesson 1).
- Latent transport adds to the dry 2.8 PW; the 42 K target returns.
- Precipitation drives snow (ADR-0008's seam is filled) and the bucket;
  M9 inherits a water budget.
- **Cost:** each column solve gains the latent terms; climate mode gains
  one tracer solve per layer per month; reference mode one tracer. L6 has
  2% of headroom, so M7 will need its own cuts.

## 7. Milestone mapping

| Milestone | What this record provides |
|---|---|
| M7 | §4.1–4.8; V1–V12 |
| M8 | cloud water as a state between condensation and precipitation; re-evaporation; moist convection replacing Γ_c; cloud radiation |
| M9 | the bucket becomes soil moisture; runoff becomes rivers |
| M10 | vegetation's transpiration enters the land β |
| M11 | the ocean's own transport; sea-surface salinity from E − P |

## 8. Open questions

1. Is a 5 m/s gustiness right in the deep tropics, where the monthly mean
   wind is weakest? Measured against reference mode in V9.
2. Does the bucket need a separate snowmelt infiltration limit before M9?
3. Should τ_d be fitted, or fixed to a CO₂-like value and κ_v alone fitted?

## 9. Proposed tasks

1. **M7-01 — State and saturation.** §4.1–4.2: the fields, PSNAP schema 6
   with its migration and golden save; q_sat and its tests (V1, V10).
2. **M7-02 — Evaporation and the bucket.** §4.3 in the tile and column
   solves, latent cooling, water and energy budgets with a prescribed
   humidity (V2, V3, V5).
3. **M7-03 — Saturation rainout.** §4.4 in the column solve; model
   precipitation replaces the prescribed field; single-column tests (V4).
4. **M7-04 — Transport in climate mode.** §4.5's implicit tracer solve in the
   coupled climate step (V6, V7 recorded).
5. **M7-05 — Vapour radiation.** §4.6.
6. **M7-06 — Reference mode.** The core's tracer and the column physics
   every reference step.
7. **M7-07 — Refit and close.** §4.8 (V8), parity (V9), performance (V12),
   records.

## 10. Amendment: the surface air's humidity, snow's freezing, saturation after convection (accepted 2026-10-09, task M7-03)

**10.1 The bulk formula's humidity (amends §4.3; decided with the user).**
With saturation rainout the bottom layer holds q_sat(T₀, p₀) where it rains.
On three layers, T₀ is the temperature at σ₀ = 5/6, about 1.5 km up and
10 K below the surface. Its saturation is about 60% of the surface's, so
the formula as written evaporated 185 W/m² over the year: 2.4 m/yr against
Earth's 1.0. That was measured on L3 with the M6 climate, which sits 4–5 K
above 288 K before the refit. More layers only narrow the gap: 153 W/m² on
five layers, 122 on eight.

E = ρ C_E V_e β (q_sat(T_s) − q_a) now uses the surface air's humidity:
the bottom layer's relative humidity carried to the surface air
temperature A, along the same Γ_c that gives A:

  q_a = q₀ q_sat(A, p_s) / q_sat(T₀, p₀),

which is q_sat(A, p_s) where the layer rains. This gave 127 W/m² on the
same run. Alternatives that were measured and rejected:
- a critical relative humidity below 1: 158 W/m² at 0.8, because the drier
  air evaporates more;
- C_E = 1.5e-3 on land: 124 W/m².

The 5 m/s gustiness (104 W/m² at 3 m/s) is left to the refit (§4.8).

**10.2 Snow's freezing (amends §4.4).** Condensate that falls as snow
(on a land tile that starts the step frozen, ADR-0008 §4.3) releases
L_v + L_f in the column, not L_v. Otherwise the L_f its melt later takes
would be energy from nowhere: about 0.3 W/m² globally.

**10.3 Saturation after convection (amends §4.4, V4).** The convective
adjustment that follows the column solve (ADR-0010 §3.3 B) carries the
condensation's heat upward. It leaves the layers below supersaturated at
their new temperatures: a mean relative humidity of 1.36 in the bottom
layer after a year. Each layer then condenses its excess isobarically,
c_p (T' − T) = L (q − q_sat(T')), alternating with the convective
adjustment until nothing more condenses. Energy and water are exact. Moist
convection (M8) replaces the pair.

