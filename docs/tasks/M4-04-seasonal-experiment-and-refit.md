# Task M4-04 — Seasonal snow and ice experiment, climatology, refit

- **Milestone:** P0 / M4 (fourth task; M4-01 to M4-03 are complete)
- **Requested:** 2026-10-01
- **Scope:** the monthly climatology of ADR-0008 §4.6 and ADR-0006 §7; the
  refit of `D` and `g` with sea ice active (ADR-0008 §4.7, ADR-0009 §4.4 and
  §10, specification §24); the seasonal experiment (ADR-0008 V7, the
  specification's "stable seasonal snow/ice experiment" acceptance of M4).
  ADR-0003's M4 history work is M4-05.
- **Governing decisions:** ADR-0008, ADR-0009 (§10–12), ADR-0006 §7,
  ADR-0001 §4.1, specification §13 M4, §23, §24

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Climatology.** Per cell and sub-step `k mod 12`: running mean and
   population variance (Welford, in double, stored as `float`) of the
   cell's radiating surface temperature, and the means of land snow and sea
   ice, over the climate-mode steps since the last reset. Four
   `climatology`-partition fields (12 layers) and the derived per-cell
   `surface_temperature_K` written by every surface step; never persisted;
   spin-up does not contribute. The scheduler's climate process accumulates
   it.
2. **Refit.** Joint bisection as in ADR-0009 §10, with sea ice active and no
   precipitation: L4, seed 1, 150 spin-up years, to 288 K and a 42 K P2
   equator-to-pole difference; checked at L5. The record states the
   resulting sea-ice extents against Earth's and the remaining drift.
3. **Seasonal experiment (V7).** The calibrated Earth-like planet after
   spin-up, without and with prescribed snowfall (1e-5 kg/m²/s), and the
   aqua planet: two consecutive years' largest and smallest sea-ice and
   snow-covered areas by hemisphere. The gate is the repeat of the areas
   (≤ 1e-3 relative); ice volume and perennial snow are recorded with their
   drift, since nothing yet limits perennial ice thickness or land ice sheets
   (no ocean heat transport before M11, no ice-sheet flow).

## 2. Steps

1. Climatology fields, accumulator and tests.
2. Refit; constants and record.
3. Snow-covered area diagnostics; the seasonal experiment as a physics test
   and through `planet_cli thermal`.
4. Documentation (ADR-0008 implementation record, README, audit, status),
   full CI matrix and the ADR-0001 gates.

## 3. Acceptance

ADR-0008 V7 as stated in §1.3, the climatology tests, the refit record, and
the unchanged suite, warnings as errors, the floating-point policy, the
registry check and the ADR-0001 performance gates.
