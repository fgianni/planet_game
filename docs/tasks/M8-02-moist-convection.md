# Task M8-02 — Moist convection (Betts–Miller)

- **Milestone:** P0 / M8 (second task)
- **Status:** complete (2026-10-17, ADR-0023 §11 accepted): the physics
  are tested, reference mode is stable over ten years, the climate solve
  converges, and V7 as read in §11.3 passes
- **Governing decisions:** ADR-0023 §4.2, §4.3.1, §4.5, V2–V4, V7

## Done

- **The physics** (`sim/planet/atmosphere/atmosphere_column.hpp`):
  - **Dry adjustment:** the convective adjustment of ADR-0010 §3.3 B to
    the dry adiabat, (p/p₀)^(R_d/c_p) (`ColumnRadiation::exner_dry`), in
    place of Γ_c.
  - **`betts_miller`:**
    - the parcel lifted from the bottom layer keeps its moist static
      energy c_p T + Φ + L q, with Φ hydrostatic in the environment;
    - its saturated temperature comes from Newton on the bracket
      [T_dry, T_dry + L q/c_p], safeguarded by bisection. Newton alone
      overshoots from below on a month's evaporation, to where e_s > p;
    - each layer k ≥ 1 joins the convecting column with weight
      w_k = smoothstep(b_k / 1 K), b_k its parcel buoyancy, and no more than
      the layer below; the bottom layer joins as layer 1. The convecting
      column runs from the bottom to the highest layer with w > 0, at least
      one above the bottom;
    - the reference is the parcel's temperatures and RH_bm q_sat of them,
      shifted by one constant s;
    - s solves Σ w (c_p (T_ref − T) + L min(0, q_ref − q)) = 0
      (safeguarded Newton), so that no layer is moistened and the moist
      enthalpy is kept. Where nothing dries, only temperature relaxes: the
      shallow case;
    - each layer relaxes by r w_k, with r = 1 − exp(−Δt/τ_bm). The water
      the column loses is the convective precipitation.
  - **In the column solve**, with `ColumnMoisture::moist_convection`:
    - the solve condenses the whole excess at the new temperatures
      (τ_c → 0), so that the step's latent heat is in the implicit
      balance;
    - then the dry adjustment, Betts–Miller, and the condensation pass
      after them, at once (ADR-0023 §4.3.1);
    - the transport's linear response through convection is Betts–Miller's
      own finite difference along the pre-convection response. The layers
      that condensed in the solve keep saturation (δq = q_sat′ δT). A
      warmer column dries less, so it keeps only about
      c_p / (c_p + L RH q_sat′) of a warming, a quarter in the tropics;
    - the result carries the convective precipitation, the top layer and
      the convection's moist-enthalpy change.
  - Off, the solve is M7's bit for bit.
- **Parameters:**
  - `AtmosphereParameters::moist_convection` (off until M8-05),
    `moist_convection_time_s` (τ_bm = 2 h) and
    `moist_convection_relative_humidity` (RH_bm = 0.8);
  - the scenario entry `moist_convection`, as `water_cycle`: the manifest
    records it, and a manifest without it (before M8) reads as off;
  - `planet_cli reference|parity --moist-convection 0|1`.
- **Reference mode's start** (`AtmosphereDynamics::step`): the start from
  rest also puts p_s on the orography the winds see, with the mass held,
  as the balanced start already did.
  - Before, p_s stayed on the true terrain. At L3 a 3 km plateau cell at
    77.8 kPa sat beside a smoothed 742 m surface, and the core shed the
    difference in gravity waves of about 10 kPa.
  - `test_climate_circulation` now checks that both starts adjust by less
    than 2 kPa in two days: 1 058 Pa from the balanced start, 1 360 Pa
    from rest.
- **The convective precipitation field**
  (`forcing().convective_precipitation_kg_m2_s`, registered in M8-01) is
  filled each step.

## Deviations from ADR-0023 to record

1. **Condensation in the solve.** §4.2 and §4.3.1 order the steps solve,
   convection, then condensation. That order fails in climate mode.
   - The month's evaporation enters the implicit solve as vapour without
     its latent heat.
   - The transport's Newton then drives one L3 column's bottom layer to
     7 K under a −1 047 W/m² source in the first spin-up month.
   - So the solve condenses the whole excess inside the implicit balance
     (as M7 did with τ_c, whose fraction is 1 for a month). The once-only
     pass after convection mops up what convection leaves. There is still
     no τ_c.
2. **A continuous onset.** §4.2.2's trigger (a layer joins where the
   parcel is warmer) switches a moist column's drying on at full
   strength.
   - The monthly transport solve then has no root: its coupled Newton
     ran to its 30-iteration limit in almost every month, ending at
     0.01–7 W/m² against 1e-4.
   - The weights w_k = smoothstep(b_k / 1 K) make the relaxation fade in
     over 1 K of buoyancy. Newton then takes 4–7 iterations to 1–7e-5
     W/m², as in M7.

Both are recorded in ADR-0023 §11 (accepted 2026-10-17).

## Results

- **V2–V4** (`tests/unit/test_moist_convection.cpp`):
  - the dry adjustment leaves a stable column bit for bit, and a profile
    neutral to Γ_c (stable to the dry adiabat) also;
  - it mixes a super-adiabatic column to the dry adiabat, Σ T kept;
  - at r = 1 a buoyant column reaches the shifted parcel profile to
    1e-9 K. The parcel's moist static energy is constant to 1e-6 J/kg;
    the moist enthalpy is kept to 1e-12; no layer is moistened; the
    water lost is the precipitation;
  - a part step moves every layer by r of the full one;
  - a stable column is untouched bit for bit;
  - a dry buoyant column moistens nothing;
  - sweeping layer 1 through the parcel's temperature in 1 mK steps moves
    every layer by at most 0.022 K and the rain by 0.025 kg/m² per step:
    the onset is continuous (a step trigger jumps by about 19 kg/m²);
  - in the column solve, at 10 minutes and a month, with and without
    evaporation: water closes to 1e-12, no layer ends above saturation,
    and the convection's enthalpy change is round-off.
- **What broke reference mode** (L3; M7's reference mode has the same
  causes):
  - **The start from rest** left p_s on the true terrain, which the core
    shed in gravity waves of about 10 kPa. Fixed (above).
  - **The climate state's tropics are about 20 K too hot.** Within ±15°
    the ocean averages 307–309 K, and the hottest cells reach 318–321 K;
    their bottom layer is at 307 K.
    - In climate mode such a cell is in a self-consistent equilibrium:
      the surface air is within 1 K of the sea and evaporates 180 W/m².
    - Within the first reference hour the resolved dynamics cool the
      bottom layer by about 8 K. The cell then evaporates 1.5–3 kW/m²,
      and the "grid-point storms" of the first months are those cells
      shedding that heat.
    - That is the climate calibration (M8-04's clouds, M8-05's refit, the
      42 K target), not reference mode's stability.
  - **The initial state** (`initialise_climate`) starts the equatorial
    sea at 327 K, so a run started in reference mode without a spin-up
    begins with the same imbalance, stronger.
- **V7** (`test_reference_moist_convection 10`, L3, from rest, ten years,
  16 threads): stable throughout.
  - The budgets close in every step (energy 0.04 of the gate, water
    2.5e-5 of it). Convection gives 33 % of the rain.
  - Winds: 94 m/s in year 1's start-up transient, then 54–70 m/s every
    year: **the 100 m/s bound holds**.
  - Latent heating: the highest single-cell daily mean is 5.7 kW/m² in
    year 1, then 0.88–1.29 kW/m² each year. Each cell's climatological
    mean over years 2–10 reaches **408 W/m²** (14 mm/day), against
    Earth's wettest ~800 km boxes at 300–350 W/m². **V7's latent part,
    read as that climatological mean, fails**; a daily reading fails
    further. The excess follows from the too-hot tropics.
  - Global precipitation falls from 1.88 m/yr in year 1 to 1.04 m/yr in
    year 10 (climate mode: 0.94).
- **Parity (V8)** (L3, 10-year spin-up, a year in each mode): climate
  288.22 K and 0.94 m/yr; reference 293.21 K and 1.22 m/yr; **+4.98 K,
  ×1.30** (M7: +5.45 K, ×1.29).
- **Performance:** a climate year at L3 takes 0.69 s with moist
  convection, against 0.49 s with M7's (×1.41; V11 allows ×1.5).
- All 94 tests pass; the CI checks (fp flags, presentation includes,
  field registry) pass.

## V7 as amended (ADR-0023 §11.3, decided with the user 2026-10-16)

V7 tests stability: winds below 100 m/s, and every cell's daily-mean
latent heating after the first year below 100 mm/day of rain (2 894 W/m²).
The climatological figure moves to V9's calibration report.
`test_reference_moist_convection 10` (L3, from rest, 16 threads), re-run
with that gate:
- winds: 94.0 m/s in year 1, then 54.2–69.6 m/s;
- highest daily-mean latent heating after year 1: 1 288 W/m² (year 2),
  then 876–1 057 W/m² a year; **passes**;
- climatological maximum 408 W/m², reported for V9;
- closure 0.040 of the energy gate, 2.5e-5 of the water gate; 1.20 m/yr
  over the run, 33 % convective.

## Carried forward

- **The climatological latent heating** (408 W/m²): reported by V9 after
  the clouds and the refit cool the tropics (M8-04, M8-05).
- **V8 parity** (+4.98 K) is M8-05's gate.
- Γ_c keeps its other role, the circulation's static stability
  (`zonal_coupling.cpp`), until the refit.
