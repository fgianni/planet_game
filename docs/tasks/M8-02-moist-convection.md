# Task M8-02 — Moist convection (Betts–Miller)

- **Milestone:** P0 / M8 (second task)
- **Status:** in progress: the column physics are done and tested; V7 fails
  (see Results), also with M7's convection
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
    - the convecting column runs from the bottom layer to the highest of
      the contiguous buoyant layers above it, at least one;
    - the reference is the parcel's temperatures and RH_bm q_sat of them,
      shifted by one constant s;
    - s solves Σ c_p (T_ref − T) + L min(0, q_ref − q) = 0 (safeguarded
      Newton), so that no layer is moistened and the moist enthalpy is
      kept. Where nothing dries, only temperature relaxes: the shallow
      case;
    - the relaxation is by r = 1 − exp(−Δt/τ_bm). The water the column
      loses is the convective precipitation.
  - **In the column solve**, with `ColumnMoisture::moist_convection`:
    - the solve condenses the whole excess at the new temperatures
      (τ_c → 0), so that the step's latent heat is in the implicit
      balance;
    - then the dry adjustment, Betts–Miller, and the condensation pass
      after them, at once (ADR-0023 §4.3.1);
    - the transport's linear response follows the convecting layers as
      a profile of the parcel's shape (dT_ref/dT₀);
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
- **The convective precipitation field**
  (`forcing().convective_precipitation_kg_m2_s`, registered in M8-01) is
  filled each step.

## Deviation from ADR-0023 to record

§4.2 and §4.3.1 order the steps solve, convection, then condensation.
That order fails in climate mode. The month's evaporation enters the
implicit solve as vapour without its latent heat. The transport's Newton
then drives one L3 column's bottom layer to 7 K under a −1 047 W/m² source
in the first spin-up month. So the solve condenses the whole excess
inside the implicit balance (as M7 did with τ_c, whose fraction is 1 for
a month), and the once-only pass after convection mops up what convection
leaves. There is still no τ_c. The ADR needs an amendment saying so.

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
  - in the column solve, at 10 minutes and a month, with and without
    evaporation: water closes to 1e-12, no layer ends above saturation,
    and the convection's enthalpy change is round-off.
- **V7** (`tests/physics/test_reference_moist_convection.cpp`, L3): FAILS,
  as does the same run with M7's convection. CTest runs 30 days from
  rest: the budgets close (energy 0.03 of the gate, water 1e-5) and
  convection rains (35 % of the total). The measurement run gates V7.
  - **The handover:** after a 2- or 10-year climate spin-up, winds reach
    143 m/s (M7: 146 m/s) on the first reference day, then decay to about
    40 m/s by day 30. After 5 years, M7's run passes the sub-step rule's
    150 m/s bound in the third hour, on the committed M8-01 tree too.
  - **Grid-scale storms:** single cells' latent heating reaches
    5–30 kW/m² in a step, about 5 kW/m² as a daily mean, and 1.6 kW/m²
    over 30 days (M7: 10 kW/m², 4.3 kW/m², 1.4 kW/m²).
- **Parity (V8)**, measured (L3, 10-year spin-up, a year in each mode):
  - moist convection: climate 288.14 K, 0.94 m/yr; reference 293.14 K,
    1.24 m/yr; +5.01 K, ×1.32;
  - M7: 288.20 K and 293.65 K, +5.45 K, ×1.29, M7-07's numbers exactly.
- **Performance:** the climate year at L3 takes 2.68 s with moist
  convection against 0.49 s without (×5.5; V11 allows ×1.5). Not yet
  profiled.
- All 94 tests pass; the CI checks (fp flags, presentation includes,
  field registry) pass.

## Open

- **V7:**
  - the climate-to-reference handover shock and the grid-scale storms are
    in M7's reference mode already;
  - the fix (a balanced handover and some damping of grid-scale
    convergence, for instance) is a decision to take before M8-05.
- **The ADR-0023 amendment** for the order of condensation and
  convection.
- **Performance:** profile the climate step (the coupled Newton's
  iterations against the parcel solves).
- Γ_c keeps its other role, the circulation's static stability
  (`zonal_coupling.cpp`), until the refit.
