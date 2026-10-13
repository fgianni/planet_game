# Task M7-07 — Refit and close

- **Milestone:** P0 / M7 (last task)
- **Status:** complete (2026-10-13)
- **Governing decisions:** ADR-0021 §4.8, V8, V9, V12, amended in §13
  (2026-10-13; the split, τ_c, the gates and V9's deferral decided with
  the user); ADR-0001 §5, V2

## Done

- **Performance first** (step A, `6edc246`): one condensation pass after
  the convective adjustment (ADR-0021 §13.3). An L6 water month went from
  1.32 s to 0.52 s.
- **The refit** (V8, ADR-0021 §13.1):
  - `planet_cli thermal` gains `--water`/`--dry`, `--dry-depth` and
    `--vapour-absorption`;
  - with `--water`, `--calibrate KELVIN` bisects a common factor on τ_d and
    κ_v;
  - fit: τ_d = 1.0220, κ_v = 0.005110 m²/kg, giving 287.98 K on L4 after
    150 years.
- **The water cycle on by default:**
  - in the earth_like preset;
  - `Scenario::water_cycle` resolves to the preset's, the manifest records
    it, and a manifest written before M7 reads as off.
- **Fixes found along the way:**
  - saturation is continuous at 0 °C (ADR-0021 §13.2);
  - the tile solve's rounding stop is tightened from 64 ε to 8 ε, because
    short reference steps have large terms;
  - the circulation sizes all its outputs, including when the reference
    dynamics wrote the winds;
  - the evaporation's wind comes only from a coupled circulation or the
    resolved winds, so a diagnostic circulation no longer reaches the
    state.
- **The condensation time** τ_c = 12 h, which keeps reference mode stable
  (ADR-0021 §13.4; `SurfaceEnergyParameters::condensation_time_s`,
  `ColumnMoisture::condensation_fraction`).
- **`planet_cli parity`:** climate against reference from the same
  spun-up state (V9 / ADR-0001 V2).
- **Tests on the dry columns:** ADR-0008's snow and sea-ice tests,
  ADR-0009's diffusion test and the diagnostic-circulation test run with
  the water cycle off, because without the coupled circulation the
  humidity does not move. The jet test spins up 3 years, since the first
  water year is a transient.

## Results

- **V8:**
  - L4, 150 years: 287.98 K, P2 equator-to-pole difference 53.3 K, peak
    poleward transport 3.60 PW;
  - L3 after 10 years: 288.20 K and 0.95 m/yr of precipitation, against
    Earth's about 1.0;
  - the 42 K target is not reachable in M7: inert vapour gives 45.9 K
    (ADR-0021 §13.1).
- **V12** (250 years, 16 workers):
  - L5: 405 s (37 years per minute);
  - L6: 1 350 s (11.1 years per minute);
  - every month's circulation solved;
  - those are the 16-thread machine's times. The 4-vCPU CI runners cannot
    hold them, and CI had failed on the gate's wall-time caps since
    M6-04. Push CI now gates 25 years at each level with runner caps, and
    a nightly workflow runs the 250 years (ADR-0021 §13.6).
- **V9** (L3, 10-year spin-up, one year in each mode): FAILS, recorded
  and deferred to M8.
  - climate: 288.20 K, 0.95 m/yr;
  - reference: 293.65 K, 1.23 m/yr;
  - +5.45 K against the 0.3 K gate, and ×1.29 against ±10 %.
- All 89 tests pass.

## Open

- **V9 parity (M8):** moist convection, the lingering supersaturation
  under τ_c, and climate evaporation from monthly means against reference
  mode's diurnal cycle.
- **The 42 K equator-to-pole difference** (M8 clouds, M11 ocean).
- **Warm-starting the coupled Newton** across months, which needs the
  transport in the state (a PSNAP schema).
