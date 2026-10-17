# Task M8-03 — Cloud water to precipitation

- **Milestone:** P0 / M8 (third task)
- **Status:** complete (2026-10-17)
- **Governing decisions:** ADR-0023 §4.3, §4.5, §4.7, §11.1, V1, V2, V5

## Done

- **The physics** (`cloud_microphysics`, `sim/planet/atmosphere/atmosphere_column.hpp`),
  after the column solve, the dry adjustment, Betts–Miller and the
  condensation pass, on the equal-mass layers:
  - each layer's condensate (the solve's and the pass's) joins its cloud
    water q_l;
  - cloud water in a subsaturated layer evaporates by
    1 − exp(−Δt/τ_e) of itself, never beyond saturation (§4.3.4);
  - above q_l0 the rest converts to precipitation by 1 − exp(−Δt/τ_a) of
    the excess (Kessler 1969; §4.3.2);
  - precipitation falls from the top. Entering a layer it evaporates by
    ε (1 − RH) (1 − exp(−Δt/τ_e)) of itself, never beyond saturation, then
    gains the layer's autoconversion and its Betts–Miller drying, which is
    convective precipitation as §4.2.4 says. The convective part
    evaporates in proportion (§4.3.3);
  - every evaporation cools its layer by L/c_p per kg/kg, so each layer's
    c_p T + L q is exact. L is the column's latent heat, L_v plus L_f for
    the share that falls as snow, as for condensation (§4.3.5, ADR-0021
    §10.2);
  - `evaporation_to_saturation` solves q + x = q_sat(T − L x / c_p) by
    Newton safeguarded by bisection and returns the bracket's lower end, so
    no evaporation supersaturates a layer. Newton alone overshot across
    q_sat's change of slope at 0 °C (the ice branch).
- **In the column solve**, with moist convection: the result carries each
  layer's cloud water, the surface precipitation and its convective part,
  the evaporation of cloud water and of rain, and the net latent heat
  L (P + Δ cloud water) / Δt in place of the condensate's.
  - The transport's linear response does not see the microphysics (the
    evaporations' dependence on temperature is weak).
  - Off, the solve is M7's bit for bit, cloud water untouched.
- **Parameters** (`AtmosphereParameters`, validated):
  `cloud_water_threshold_kg_kg` (q_l0 = 0.3 g/kg),
  `autoconversion_time_s` (τ_a = 1 h), `rain_evaporation_efficiency`
  (ε = 0.5), `rain_evaporation_time_s` (τ_e = 1 h).
- **The step** (`step_surface_energy`): the cloud water is read into each
  column and written back where moist convection runs.
  - The water budget counts it: `water_cycle_residual_kg` adds its change,
    the gate its stock.
  - The diagnostics gain `cloud_water_change_kg`, `cloud_water_kg`,
    `convective_precipitation_kg`, `cloud_evaporation_kg` and
    `rain_evaporation_kg` (§4.7). `condensation_latent_J` is the net heat,
    which may be negative, so the energy gate takes its magnitude.
  - `planet_cli parity` and `test_reference_moist_convection` report them.

## Results

- **V5, V1, V2** (`tests/unit/test_cloud_water.cpp`):
  - the evaporation to saturation solves its equation to 6e-15 and never
    passes saturation;
  - autoconversion: a reference step converts 1 − exp(−Δt/τ_a) of the
    excess, a climate month brings every layer over q_l0 to q_l0; below
    q_l0 nothing rains; rain through saturated air reaches the surface
    whole;
  - cloud water in air at 50 % evaporates by 1 − exp(−Δt/τ_e) in a
    reference step; in a month it all evaporates, or the layer ends
    saturated to 1e-12;
  - falling precipitation over 2 000 random columns (both steps): no layer
    ends above saturation, water closes to 5e-16 and each layer's moist
    enthalpy to 5e-16; 17 % of the rain formed re-evaporates;
  - one layer's re-evaporation is ε (1 − RH) r P exactly;
  - in the column solve from a cloudy start with evaporation, both steps:
    vapour + cloud water + precipitation close to 1e-12, no layer
    supersaturated, the net latent heat is L (P + Δ cloud water), and a
    month leaves each condensing layer at q_l0.
- `test_moist_convection` counts the cloud water in its budget; off, the
  solve ignores cloud water.
- **V7 regression** (`test_reference_moist_convection 10`, L3, from rest,
  ten years, 16 threads, 25 min): stable, **passes** (ADR-0023 §11.3).
  - Winds: 93.9 m/s in year 1's start-up, then 55.7–72.6 m/s.
  - Highest daily-mean latent heating after year 1: 1 246 W/m² (limit
    2 894). The climatological maximum, reported for V9, is 416 W/m² (M8-02:
    408).
  - Budgets: energy 0.039 of its gate, water 2.7e-5 of its gate, cloud
    water included.
  - 1.20 m/yr of precipitation over the run, 33 % convective; 0.073 m/yr
    re-evaporated (rain and cloud water); 204 g/m² of cloud water at the
    end.
- **Climate mode** (L3, ten-year spin-up, then a year; `planet_cli parity
  --moist-convection 1`): 288.81 K, 0.96 m/yr, 10 % convective (M8-02:
  288.22 K, 0.94 m/yr).
  - Re-evaporation is small: 0.07 m/yr of cloud water and 7e-5 m/yr of
    rain. A month's rain forms in, and falls through, layers the month's
    evaporation keeps saturated.
  - **Cloud water: 2 589 g/m²**, against Earth's ~100 g/m². A month leaves
    every layer that condenses at q_l0 exactly, and at L3 a layer holds
    about 3 400 kg/m² of air, so q_l0 = 0.3 g/kg is about 1 kg/m² per
    cloudy layer.
- **Performance:** a climate year at L3 takes 0.39–0.41 s, against 0.28 s
  with M7's physics in the same binary (×1.43; V11 allows ×1.5; M8-02:
  ×1.41).
- All 95 tests pass; the CI checks (fp flags, presentation includes, field
  and channel registries) pass.

## Found: reference mode's start from the spun-up climate state

`planet_cli parity --subdivision 3 --spin-up-years 10 --days 365
--moist-convection 1` (V8) now stops 2.7 h into the reference year: wind
of 154.8 m/s, over the sub-step rule's bound of 150 m/s.
- **This is not caused by M8-03.** From the same start, M8-02 peaks at
  143 m/s and M7's convection at 149.5 m/s, both in the first seven
  hours. M8-02's parity year stayed under the bound by chance.
- **The cause:** the ten-year climate state, in January, has a cell at
  52° N, 69° W whose bottom layer is at 283–286 K beside one at
  212–219 K, a 64–74 K contrast across a single edge. The reference
  dynamics collapse it: the neighbours' surface pressures part by 36 kPa
  in 50 minutes, and the top layer's wind reaches 140 m/s within the hour.
- A smaller q_l0 (0.03 g/kg) changes nothing, so the stored cloud water
  is not the cause.
- V8 is M8-05's gate. Its reference half needs a start the dynamics can
  take, or a climate state without such contrasts.

## Open

- **Superseded at M8-04:** ADR-0023 §12 (accepted 2026-10-17) integrates
  the autoconversion exactly with q_l0 = 0, after cloud radiation took the
  climate to collapse with this task's cloud water (task M8-04). The
  figures above are this task's.
- **The cloud water's amount (M8-04, M8-05):**
  - a month leaves every condensing layer at q_l0, which is 13 times the
    cloud water the reference run keeps (2 589 against 204 g/m²), and
    about 25 times Earth's;
  - M8-04's cloud radiation depends on that water path, so V8 parity and
    the CERES targets will need q_l0 (a calibration constant, §4.6)
    refitted, or the threshold read per unit of column mass. M8-05
    decides.
- **The reference start** above (V8, M8-05).
- The transport's linear response leaves out the microphysics. Its
  Newton iterations were not measured; the climate year's cost moved only
  from ×1.41 to ×1.43 of M7's at L3. M8-05 checks it at L5 and L6 (V11).
