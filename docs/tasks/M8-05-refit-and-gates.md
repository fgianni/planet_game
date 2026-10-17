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

## Remaining

- **The refit** (§4.6, V9): W_c, r_e, τ_a with τ_d and κ_v, against a
  288 K mean, cover 0.5–0.7 and cloud effects within 30 % of −47 and
  +26 W/m², one fit for both modes.
- **The reference start** for V8 (task M8-03): the ten-year climate state
  holds a 64–74 K contrast across one edge near 52° N that the reference
  dynamics cannot take.
- **V8** parity, **V9** calibration and **V11** performance.
