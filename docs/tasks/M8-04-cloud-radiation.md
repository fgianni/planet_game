# Task M8-04 — Cloud radiation

- **Milestone:** P0 / M8 (fourth task)
- **Status:** complete (2026-10-17, ADR-0023 §12 accepted)
- **Governing decisions:** ADR-0023 §4.4, §4.7, §12, V5, V6

## Done

- **Cloud optics** (`cloud_optics`, `add_cloud_longwave`,
  `sim/planet/atmosphere/atmosphere_column.hpp`), from each column's state
  at the start of the step and held through the step, as the vapour path
  is:
  - **fraction:** f_k = 1 − sqrt((1 − RH_k) / (1 − RH_c)) above RH_c = 0.8,
    with RH_k = (q + q_l) / q_sat, and 1 at saturation (Sundqvist et al.
    1989). Where Betts–Miller's trigger gives a layer the weight w_k on
    the start-of-step column, f ≥ f_conv w_k with f_conv = 0.2 (§12.3);
  - **optical depth:** τ_k = 3 LWP_k / (2 ρ_w r_e), with
    LWP_k = q_l m / f_k and r_e = 10 µm (Stephens 1978);
  - **shortwave:** R_k = f_k (1 − g) τ_k / (2 + (1 − g) τ_k) with g = 0.85;
    the layers combine by random overlap, T = Π (1 − R_k). The tiles
    receive S T; the light they reflect leaves to space;
  - **longwave:** each layer's depth gains κ_l q_l m with κ_l = 0.13 m²/g,
    ε' = 1 − (1 − ε) exp(−Δτ);
  - only layers with f > 0 radiate as cloud. The result also carries the
    column's cloud fraction 1 − Π (1 − f_k) and its optical depth
    Σ f_k τ_k, for M8-06.
- **Parameters** (`AtmosphereParameters`, validated):
  `cloud_critical_relative_humidity`, `convective_cloud_fraction`,
  `cloud_effective_radius_m`, `cloud_asymmetry`,
  `cloud_longwave_absorption_m2_kg`.
- **The step** (`step_surface_energy`), with moist convection:
  - each cell's insolation reaching the tiles is multiplied by T, and its
    layers' longwave depth gains the clouds';
  - the derived fields `cloud_fraction` (per layer),
    `cloud_shortwave_effect_W_m2` and `cloud_longwave_effect_W_m2`, now
    allocated in the state, are filled each step;
  - **shortwave effect:** −S (1 − T) Σ f_t (1 − α_t), from the tiles'
    albedos;
  - **longwave effect:** the clear column's outgoing longwave at the step's
    final temperatures and surface emission, minus the cloudy column's;
  - the diagnostics gain `cloud_cover_m2`, `cloud_shortwave_effect_W` and
    `cloud_longwave_effect_W` (§4.7).
- `compute_atmosphere_heating`, the climate circulation's diabatic heating,
  sees the same clouds.
- **ADR-0023 §12** (decided with the user 2026-10-17): autoconversion is
  integrated exactly, with the step's condensation as a constant source,
  and q_l0 = 0 (`cloud_microphysics`; see "Why §12").

## Why §12

With §4.3.2 as written (Kessler, q_l0 = 0.3 g/kg), cloud radiation took
the climate from its initial state at L3 to collapse:

| Month | Surface (K) | Cover | Cloud water (g/m²) | Shortwave effect (W/m²) | Longwave effect (W/m²) | Absorbed (W/m²) |
|---|---|---|---|---|---|---|
| 1 | 302.3 | 0.74 | 2 180 | −188 | +120 | 111 |
| 5 | 294.8 | 0.98 | 2 647 | −262 | +138 | 17 |
| 17 | 275.0 | 0.96 | 2 834 | −246 | +124 | 16 |

In the 18th month a column fell below 10 K. A climate month leaves q_l0 in
every condensing layer, about 1 kg/m² at L3 (task M8-03).

With §12, over the same span (`test_cloud_radiation_run 36`):

| Month | Surface (K) | Cover | Cloud water (g/m²) | Shortwave effect (W/m²) | Longwave effect (W/m²) |
|---|---|---|---|---|---|
| 1 | 306.3 | 0.58 | 155 | −43 | +71 |
| 11 | 301.0 | 0.60 | 148 | −44 | +49 |
| 23 | 295.8 | 0.74 | 136 | −43 | +42 |
| 35 | 292.4 | 0.81 | 126 | −35 | +37 |

§4.6's targets: cover ~0.6, −47 and +26 W/m².

## Results

- **V6** (`tests/unit/test_cloud_radiation.cpp`):
  - the fraction is zero up to RH_c, Sundqvist's above it and one at
    saturation, cloud water counted;
  - a convecting column carries f_conv in its convecting layers, even where
    it is dry, and without cloud water nothing reflects or absorbs;
  - 50 g/m² of overcast cloud: τ = 7.5, R = 0.36, longwave depth 6.5,
    emissivity above 0.998;
  - reflection rises with the water path at a fixed fraction, and with the
    fraction at a fixed in-cloud path; random overlap multiplies the
    layers' transmissions.
- **V6 on the planet** (`tests/physics/test_cloud_radiation_run.cpp`, L3,
  climate, three months in CTest): the energy and water budgets close
  every month; the cover lies in (0, 1); the longwave effect is positive;
  from the second month (the first starts without cloud water) the
  shortwave effect is negative globally and in every cell.
- **V5 as amended** (`tests/unit/test_cloud_water.cpp`):
  - each layer's cloud water follows the exact solution in both modes,
    with q_l0 = 0 and with 0.3 g/kg; a month leaves q_l0 + C τ_a / Δt;
  - six hours of steady condensation give the same cloud water and rain in
    one step as in 36 ten-minute steps, from cloudy and clear starts, to
    1e-12;
  - the column solve's month leaves each layer at q_l0 + C τ_a / Δt.
- **The climate with clouds** (`test_cloud_radiation_run 300`, L3, from the
  initial state, 25 years): it settles at **284.4 K** (M8-03 without
  cloud radiation: 288.8 K):

  | Year | Surface (K) | Cover | Cloud water (g/m²) | Shortwave effect (W/m²) | Longwave effect (W/m²) |
  |---|---|---|---|---|---|
  | 1 | 301.0 | 0.59 | 148 | −43.6 | +48.8 |
  | 5 | 289.2 | 0.83 | 114 | −33.2 | +34.7 |
  | 10 | 285.8 | 0.87 | 105 | −35.7 | +34.0 |
  | 25 | 284.4 | 0.88 | 99 | −36.3 | +33.9 |

  - Against §4.6's targets (V9, M8-05): cover 0.88 against ~0.6;
    shortwave −36 against −47; longwave +34 against +26. The net effect is
    −2 W/m², against Earth's −21.
  - The cloud water, 99 g/m², is Earth's.
- **Climate year** after a ten-year spin-up (`planet_cli parity
  --moist-convection 1`): 285.75 K, 1.03 m/yr, 9 % convective; 105 g/m² of
  cloud water; re-evaporation 0.006 m/yr of rain and 0.001 of cloud water;
  0.34 s.
- **V8** still stops at the reference start (155 m/s; task M8-03's
  finding).
- **V7** (`test_reference_moist_convection 10`, L3, from rest, 35 min):
  **passes, with almost no margin.**
  - Winds: 95.0 m/s in year 1, then 72–99.6 m/s, rising as the run cools.
    The highest is 99.6 m/s against the 100 m/s bound (M8-03: 56–73 m/s).
  - Highest daily-mean latent heating after year 1: 1 316 W/m² (limit
    2 894). The climatological maximum is 165 W/m² (M8-03: 416), now
    under V9's 300 W/m².
  - Budgets: energy 0.050 of its gate, water 2.0e-5 of it.
  - **Reference mode drifts dry:** precipitation falls every year, from
    1.76 m/yr in year 1 through 0.94 in year 3 to 0.45 in year 10; 12 %
    convective; 43 g/m² of cloud water at the end. The climate run from
    the same initial state levels off near 1.0 m/yr. The reference
    run's surface temperature is not recorded.
- All 97 tests pass; the CI checks pass.

## Carried forward

- **The calibration (M8-05):** cover 0.88, a net cloud effect of
  −2 W/m² and 284.4 K, against §4.6's targets; RH_c, r_e, κ_l, τ_a with
  τ_d and κ_v.
- **Reference mode's dry drift and V7's margin** (99.6 m/s): clouds
  without the refit cool reference mode more than climate mode (0.45
  against ~1.0 m/yr of rain after ten years). M8-05 measures the reference
  run's temperature and finds why, before V8.
- **The reference start** for V8 (task M8-03).
