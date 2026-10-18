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
  as M7-07 did for the water cycle). Manifests written before M8 still
  read as off.
- **τ_d and κ_v** (`earth_like_dry_optical_depth`,
  `earth_like_vapour_absorption_m2_kg`), refitted with the preset's
  physics: `planet_cli thermal --subdivision 4 --years 150 --layers 3
  --coupled --water --dry-depth 1.19576 --vapour-absorption 0.0059788
  --calibrate 288` (seed 1, 16 min): scale 1.0277, so **τ_d = 1.2289237,
  κ_v = 0.0061446183 m²/kg** (M7: 1.0220175, 0.0051100875).
  - **287.98 K**; land 275.9 K, ocean 292.9 K;
  - **P2 equator-to-pole difference 45.1 K** (M7: 53.3 K; target 42 K),
    peak poleward transport 2.75 PW (M7: 3.60);
  - closure 8.4e-6 of its gate.
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

## Remaining

- **V9** on the fitted preset: the cover and cloud effects at L4, the
  zonal cover, each cell's climatological latent heating against
  300 W/m².
- **The reference start** for V8 (task M8-03): the ten-year climate state
  holds a 64–74 K contrast across one edge near 52° N that the reference
  dynamics cannot take.
- **V8** parity, **V9** calibration and **V11** performance.
