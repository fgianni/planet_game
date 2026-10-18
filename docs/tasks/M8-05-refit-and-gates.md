# Task M8-05 — Refit and gates

- **Milestone:** P0 / M8 (fifth task)
- **Status:** in progress (started 2026-10-17)
- **Governing decisions:** ADR-0023 §4.4, §4.6, §13, V6, V8, V9, V11

## Done

- **Reference mode's dry drift explained** (ADR-0023 §13.1): under §4.4
  reference mode's clouds reflected about three times as much per gram of
  cloud water as climate mode's (−120 against −40 W/m² for similar cloud
  water). Sundqvist's fraction is nonlinear in RH, and the modes spread
  the same cloud water differently: climate mode packs it in the bottom
  layer at the convective floor, reference mode spreads it thin and
  overcast, high cloud included. A diagnostic fraction from humidity
  diverged further (§13.2).
- **ADR-0023 §13** (accepted 2026-10-17), implemented:
  - `cloud_optics` (`sim/planet/atmosphere/atmosphere_column.hpp`): each
    layer is cloudy over f_k = min(1, q_l m / W_c), with the in-cloud water
    path W_c = `cloud_in_cloud_water_path_kg_m2` (0.1 kg/m² before the
    refit). The optics are §4.4's. Below overcast each layer's reflection
    and longwave depth are proportional to its cloud water;
  - `cloud_critical_relative_humidity` and `convective_cloud_fraction`
    are removed, with the start-of-step Betts–Miller trigger the optics ran
    (§12.3);
  - a layer without cloud water is clear, whatever its humidity.
- **The longwave cloud effect's bias removed:** the step took the clear
  column's outgoing longwave at the final temperatures but the cloudy
  one's at the Newton solution's, before the adjustments. Both are now
  taken at the final temperatures. A clear month now reports 0 W/m²
  (it reported +16.5). Task M8-04's longwave effects carry that bias.
- **Tests:**
  - `test_cloud_radiation` (V6 as §13.5): the fraction is the cloud water
    over W_c whatever the humidity; reflection and longwave depth are
    proportional to the cloud water below overcast, and the mean of two
    states' optics is the optics of their mean to 1e-14; above overcast
    the reflection saturates; the overcast optics of task M8-04 (50 g/m²:
    τ = 7.5, R = 0.36, depth 6.5) with W_c = 50 g/m²;
  - `test_cloud_radiation_run`: the first month, without cloud water, is
    clear (cover and both effects zero); from the second, the effects'
    signs as before.
- All 97 tests pass; the CI checks pass.

- **ADR-0023 §14** (accepted 2026-10-18), implemented: Betts–Miller's
  drying joins each layer's condensate in the exact autoconversion
  (`cloud_microphysics`). Convective precipitation is the convective share
  of the layer's source applied to its autoconversion, at most the layer's
  convective condensate (the autoconversion also drains cloud water from
  earlier steps). `ColumnSolveResult::convective_condensate_kg_kg` carries
  each layer's.
  - `test_cloud_water`: six hours of steady condensation, half of it
    convective, give the same cloud water and rain in one step as in 36
    ten-minute steps; a month leaves each layer at its total condensation
    rate times τ_a; the convective rain never exceeds the convective
    condensate.

## Results before the refit

L3, from the initial state, W_c = 0.1 kg/m², the other constants §12's
(probe in the session's scratchpad: monthly means of the step diagnostics):

| | Climate, month 6 | Climate, month 12 | Reference, month 6 | Reference, month 12 |
|---|---|---|---|---|
| Cloud water (g/m²) | 155 | 153 | 129 | 118 |
| Cover | 0.88 | 0.85 | 0.61 | 0.59 |
| Shortwave effect (W/m²) | −165 | −180 | −117 | −120 |
| Per gram (W/g) | −1.06 | −1.17 | −0.90 | −1.02 |
| Longwave effect (W/m²) | +78 | +48 | +55 | +59 |

- The shortwave effect per gram agrees within 10–20 % (§4.4: a factor of
  three).
- At W_c = 0.1 kg/m² the clouds are far too bright: climate mode falls to
  263.5 K by month 36, reference mode to 293.6 K by month 12, both still
  cooling. The refit starts here.

## The refit so far (exploration, L3, climate mode, 25 years)

A temporary override of the constants in the run (not committed) swept
them. Final-year means:

| τ_a (s) | W_c (kg/m²) | r_e (µm) | τ_d, κ_v scale | Surface (K) | Cover | SW (W/m²) | LW (W/m²) | Cloud water (g/m²) |
|---|---|---|---|---|---|---|---|---|
| 3600 | 0.3 | 10 | 1 | 265.5 | 0.33 | −83 | +24 | 99 |
| 3600 | 0.8 | 10 | 1 | 294.4 | 0.19 | −56 | +51 | 154 |
| 600 | 0.04 | 10 | 1 | 276.0 | 0.58 | −61 | +21 | 24 |
| 600 | 0.04 | 14 | 1.15 | 284.2 | 0.63 | −52 | +20 | 26 |
| 600 | 0.04 | 18 | 1.1 | 286.2 | 0.66 | −45 | +22 | 28 |
| 600 | 0.04 | 18 | 1.25 | 290.7 | 0.68 | −46 | +20 | 28 |
| 1000 | 0.06 | 18 | 1.2 | 283.6 | 0.66 | −61 | +21 | 42 |

- At τ_a = 1 h the climate holds 100–160 g/m² of cloud water, and no W_c
  gives both a cover near 0.6 and a shortwave effect near −47 W/m²
  (covering 0.6 of the sky with −47 W/m² needs an in-cloud optical depth
  near 5, about 30 g/m² in all). A shorter τ_a lowers the cloud water.
- **Candidate:** τ_a = 600 s, W_c = 0.04 kg/m², r_e = 18 µm, τ_d and κ_v
  scaled by about 1.15: near 288 K, cover 0.66, −45 and +21 W/m², all
  inside V9's bands. r_e = 18 µm is large for liquid cloud; it also
  stands for the absorption the two-stream limit leaves out.

**Both modes with the candidate**, first year from the initial state:

| | Climate | Reference |
|---|---|---|
| Surface (K) | 303.5 | 305.4 |
| Cloud water (g/m²) | 34 | 22 |
| Cover | 0.66 | 0.41 |
| Shortwave effect (W/m²) | −50 | −30 |
| Per gram (W/g) | −1.46 | −1.35 |
| Convective share of rain | 0.10 | 0.42 |

The shortwave effect per gram agrees within 8 % (§13 works), but
reference mode holds a third less cloud water. Betts–Miller's condensate
falls as rain at once (§4.2.4) and never becomes cloud water, and
reference mode rains four times more convectively. Its large-scale rain
is 0.66 of climate mode's, which is the cloud water's ratio.

**Prototype: convective condensate through the cloud water** (Betts–Miller's
drying joins the layer's condensate before autoconversion; the convective
share of the rain follows its share of the source). First year:

| | Climate | Reference |
|---|---|---|
| Surface (K) | 303.1 | 304.3 |
| Cloud water (g/m²) | 38 | 42 |
| Cover | 0.69 | 0.56 |
| Shortwave effect (W/m²) | −54.6 | −54.2 |
| Longwave effect (W/m²) | +26 | +40 |

The shortwave effects agree. The longwave differs: reference mode's
clouds sit higher (resolved ascent saturates the upper layers, which
monthly-mean states seldom do). This changes ADR-0023 §4.2.4 and §4.3,
so it waits for the user.

## The refit (2026-10-18)

- **Cloud constants** (`AtmosphereParameters`), from 25-year L3 climate
  sweeps with §14 (a temporary override, not committed): τ_a = 600 s,
  W_c = 0.05 kg/m², r_e = 18 µm. With the vapour path's scale near 1.17,
  the sweep gave 288.6 K, cover 0.59, −46 and +20 W/m² (§4.6's targets:
  ~0.6, −47 and +26). r_e = 18 µm is large for liquid cloud; it also
  stands for the absorption the non-absorbing two-stream limit leaves out.
- **Moist convection on in the earth_like preset** (decided with the user,
  as M7-07 did for the water cycle; `d3ac031`), **then off again**
  (decided with the user, 2026-10-18) until the coupled transport takes
  moist columns at L6 (V11 below). The refitted τ_d and κ_v are kept as
  `earth_like_moist_dry_optical_depth` and
  `earth_like_moist_vapour_absorption_m2_kg` while it is off. **On again**
  after ADR-0024's V4 (decided with the user, 2026-10-18), with its own
  refit below; the separate moist constants are gone.
- **τ_d and κ_v**, refitted with moist convection on: `planet_cli thermal --subdivision 4 --years 150 --layers 3
  --coupled --water --dry-depth 1.19576 --vapour-absorption 0.0059788
  --calibrate 288` (seed 1, 16 min): scale 1.0277, so **τ_d = 1.2289237,
  κ_v = 0.0061446183 m²/kg** (M7: 1.0220175, 0.0051100875).
  - **287.98 K**; land 275.9 K, ocean 292.9 K;
  - **P2 equator-to-pole difference 45.1 K** (M7: 53.3 K; target 42 K),
    peak poleward transport 2.75 PW (M7: 3.60);
  - closure 8.4e-6 of its gate.
- **τ_d and κ_v refitted with ADR-0024** (the eddies on T_m):
  `planet_cli thermal --subdivision 4 --years 150 --layers 3 --coupled
  --water --moist-convection 1 --dry-depth 1.2289237 --vapour-absorption
  0.0061446183 --calibrate 288` (seed 1, 14 min): scale 0.97383, so
  **τ_d = 1.19676046, κ_v = 0.00598380212 m²/kg**, the preset's.
  - **288.00 K**; land 277.5 K, ocean 292.3 K;
  - **P2 equator-to-pole difference 48.1 K** (45.1 K with the eddies on
    θ_c; M7 53.3 K; target 42 K), peak poleward transport 2.75 PW: the
    eddies see the layers' mean temperature, a smaller contrast than θ_c's
    (ADR-0024 §4);
  - no failed months; closure 7.2e-6 of its gate.
- **Tests that read the preset:**
  - `test_atmosphere_plateau` pins M7's column: its bounds are Γ_c's lapse
    rate (4–9.8 K/km). With moist convection the plateau's is 3.6 K/km
    (before the τ_d refit).
  - `test_zonal_coupling` now finds the eddy-driven jet as the top
    layer's strongest wind between 37.5° and 67.5°, a local maximum. It
    took the hemisphere's strongest wind, and the southern January profile
    has two maxima of similar size, so the check flipped with small
    changes. Its own comment recorded the flip in M7's first year.

    | Southern January (L4, 3 years) | Subtropical max | Eddy-driven max |
    |---|---|---|
    | M8's physics, refitted | 32.4 m/s at 27.5° | 25.7 m/s at 67.5° |
    | M7's physics, new τ_d | 35.7 m/s at 32.5° | 29.6 m/s at 57.5° |
    | M8's state, clouds left out of the heating | 32.4 m/s at 27.5° | 30.4 m/s at 67.5° |

    The northern jet stays at 52.5° (36 m/s).
  - `test_cloud_water` and `test_cloud_radiation` pin the constants their
    checks write out (τ_a = 1 h; r_e = 10 µm, W_c = 0.1 kg/m²).
- All 97 tests pass; the CI checks pass.

## V8 (ADR-0023 §15, accepted 2026-10-18)

- **The reference start:** the ten-year climate state holds single land
  cells 70–85 K colder than their ocean neighbours (L3, January: cell 423
  at 38° N, 84° W, surface 226 K, layers 218/189/179 K, beside an ocean
  cell at 300 K; 63 edge-layer pairs over 60 K). M7's physics gives the
  same, stronger (85 K, the ocean cell at 311 K). The reference winds pass
  150 m/s within hours (150.8 m/s with the refitted preset). It is climate
  mode's: its columns exchange little heat with their neighbours.
- **§15:** V8 compares each mode's eleventh year from the initial state,
  each spun up in its own physics. `planet_cli parity` now spins the
  reference half up in reference mode (the climate half before tick 0, as
  before).
- **Result** (`planet_cli parity --subdivision 3 --spin-up-years 10 --days
  365 --workers 12`, seed 1; the reference half 26 min):

  | | Climate | Reference |
  |---|---|---|
  | Surface (K) | 289.30 | 295.92 |
  | Precipitation (m/yr) | 1.580 | 1.560 |
  | Convective share | 0.06 | 0.40 |
  | Cloud water (g/m²) | 29.6 | 30.6 |
  | Re-evaporated cloud water (m/yr) | 0.0004 | 0.11 |

  **Precipitation passes** (ratio 0.987; M8-02: ×1.30). **Temperature
  fails: +6.62 K** against 0.3 K (M8-02: +4.98 K; M7: +5.45 K). Recorded
  failing, decided with the user; the gap is carried forward (ADR-0023
  §15.4).
- From the probe's monthly means, year 11: cover 0.59 and 0.47, shortwave
  effect −46.8 and −46.1 W/m², longwave +20.6 and +38.9 W/m² (reference
  mode's clouds sit higher). Reference mode holds 296 K from year 8 (no
  dry drift); its strongest wind over the eleven years is 95 m/s.

## V11 (performance) and the transport solve at L5 and L6

- **First attempt:** with the preset's physics, L5 and L6 stopped within
  seconds: a column's bottom layer fell to a few kelvin under a transport
  source of −840 to −1 220 W/m². L5 in its second month, the first with
  clouds; without the clouds' longwave it ran.
- **L5, cause:** the coupled transport's Newton (ADR-0011 §17.1) evaluates
  each trial step at full length first. With a nearly black cloudy bottom
  layer some full steps asked columns for sources they cannot balance at
  any positive temperature (the column Newton walked the bottom layer to
  7 K with its residual still positive), and the column threw before the
  line search could reject the step. Once that was caught, the line
  search's non-monotone rule (ADR-0009 §9: a full step across a kink, up
  to four times) accepted a full step that raised the merit a
  million-fold, sending sources to 10⁵ W/m².
- **The fix:**
  - `solve_trial_column` (`surface_energy.cpp`): a column that throws
    during one of the transport's trial evaluations is restored to its
    start (its guess, its warm start invalidated) and reported as NaN, per
    cell, so the same for any worker count; the trial's merit is then NaN
    and the line search rejects it. The final application still throws.
  - Both transport solves (`heat_transport.cpp`): the non-monotone full
    step is taken only if its merit is finite and at most
    `max_non_monotone_growth` (100) times the current one.
  - `test_advection_diffusion` §4: columns that report a tenth of their
    slope (so full steps overshoot) and are missing beyond 1.5 times the
    solution's largest source: 5 trials are missing, and the solve ends
    finite and conservative.
  - A more exact source floor (solving the layers above with the bottom
    held at 100 K) was tried and is not needed; the floor is unchanged.
- **V11, L5** (`planet_cli run --subdivision 5 --years 250 --workers 16`):
  **345 s** (43.5 years per minute), against M7's 405 s; ×1.5 allows
  608 s. CI's run (25 years, 4 workers): 54 s against the 180 s cap
  (M7: 58 s).
- **V11, L6 fails:** the run stops in its first month, before any cloud
  forms. The transport Newton does not converge in 30 iterations (its
  residual stays near 7 000 W/m²), and the source it then applies breaks
  a column (−1 216 W/m²). M7's physics runs at L6.
  - **Cause:** in hot convecting columns the column mean θ_c *falls* as
    the transport source rises: dθ_c/dh is negative both in the solve's
    linear response (about −0.02 K per W/m²) and by finite difference (to
    −0.27). More than half of the columns logged convect to the top
    layer. Heating such a column mostly evaporates its in-solve condensate
    and shifts Betts–Miller's adjustment. The coupled transport carries
    θ_c and assumes it rises with h: its Newton matrix and its
    preconditioner (ADR-0009's) take θ' > 0, so a negative response makes
    the transport anti-diffusive there. At L3–L5 the coarse groups average
    it out; at L6 they do not.
  - Finite-difference slopes in place of the linear response do not help
    (tried).

## ADR-0024: the eddies on moist enthalpy (accepted 2026-10-18)

The fix for L6, decided with the user and accepted as ADR-0024: with moist
convection on, the climate transport's eddies are driven by each column's
moist enthalpy temperature T_m = (1/N) Σ (T_k + L q_k / c_p), and the
latent heat E the humidity transport's eddies already moved as water comes
off their heat.

- **The column** (`ColumnSolveResult::moist_enthalpy_K`, `…_slope_K_m2_W`):
  T_m after the step, and dT_m/dh from the implicit solve's response,
  (1/N) Σ r_k (1 + f L q_sat′ / c_p where layer k condensed in the solve).
  The ADR neglected the surface's evaporation in the slope. Measured at L6,
  that left the coupled Newton at its 30-iteration cap in most months
  (residuals 1e-2 to 1e-4 W/m²). With the evaporation's change added
  where the bottom layer does not condense (it then stays as vapour), the
  months from the third on converge in 5 to 9 iterations.
- **E** (`transport_humidity`): L_v times each group's eddy water
  convergence from the tracer's solution, G / (c_p N) Σ_l (q′_b − q′_a)
  per face; antisymmetric, so Σ A E = 0 to rounding. It enters as
  `AdvectionDiffusion::eddy_latent_W_m2`, subtracted from the eddies' heat.
  T_m uses the condensate's L (L_v, plus L_f for the share falling as
  snow), as the convection's moist enthalpy does; E uses L_v, so that it
  sums to zero.
- **Groups:** a group's T_m is its cells' area-weighted mean, as θ_c's is
  (the ADR's "area- and mass-weighted" read as θ_c's weighting).
- **The fallback month** (ADR-0009's diffusion) diffuses T_m the same way;
  no humidity moves in it, so E is zero.
- **Tests:** `test_moist_convection` §8 (V2): a 3-layer column swept over
  h ∈ [−400, 400] W/m² across Betts–Miller's onset: θ_c falls somewhere
  (least dθ_c/dh −0.011 K m²/W), T_m rises throughout (least 0.029), and
  the reported slope matches the finite difference to 1e-6.
  `test_advection_diffusion` §5 (V1): E comes off the eddies' heat
  exactly, sums to zero, and the solve converges to h = H − E,
  conserving energy. V3 holds by construction (off, no T_m and an empty
  E), and the golden runs are unchanged.
- **What T_m does not fix:** a land column whose bottom layer has
  stopped condensing while its surface's evaporation is still on
  ADR-0021's raining branch (R (q_sat(T_s) − q_sat(A)), R ≈ 15). There,
  warming the surface air cuts evaporation by more than the heat added,
  and T_m falls (cell 2925 at L6, month 0: −1.2 K between 118 and
  130 W/m²). The slope reports it; about 150 of L6's 40 962 columns sit
  in such a band in the first month, whose Newton ends at 1 900 W/m² of
  residual (the second month's at 74 W/m²), the source applied still
  conservative.
- **V4 (ADR-0024; ADR-0023 V11) passes** with moist convection on
  (`planet_cli run --subdivision L --years 250 --workers 16
  --moist-convection 1`, the run's new flag):
  - L5: **269 s** (55.9 years per minute), against M7's 405 s (limit
    608 s); before ADR-0024, 345 s;
  - L6: **950 s** (15.8 years per minute), against M7's 1 350 s (limit
    2 025 s);
  - no failed circulation month in either; the last month's Newton took
    4 iterations (consistency 3e-7 and 3e-5 W/m²), closure ratio 3e-6 and
    6e-6.

## Remaining

- **V9** on the fitted preset: the cover and cloud effects at L4, the
  zonal cover, each cell's climatological latent heating against
  300 W/m².
- **ADR-0024 V5:** with V4 passed, whether moist convection goes back on
  in the preset is the user's decision. Then the refit of τ_d and κ_v
  with T_m driving the eddies (288 K at L4), V9's reports, and V8
  measured again (`planet_cli parity --subdivision 3 --spin-up-years 10
  --days 365 --moist-convection 1`).
