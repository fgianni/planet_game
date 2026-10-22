# Task INV-02 — Moistening refit and parity

- **Milestone:** P0, between M8 and M9
- **Status:** refit and parity measured; L6 startup transport failure recorded (2026-10-20)
- **Governing decisions:** ADR-0026 §8 (option 1 accepted; options 2 and 3
  not taken for P0), ADR-0023 V8 and V9, ADR-0024 V4 and V5
- **Predecessor:** INV-01, findings and conclusion

## Scope

Enable `convective_moistening` in the Earth-like preset with one shared
refit in both modes. Fit τ_d and κ_v together, preserving their split, to
288 K at L4, seed 1, three layers, after 150 coupled climate years. Check
the existing cloud constants against ADR-0023 V9 before changing them.
Keep the sub-grid condensation, non-precipitating cloud and convective
detrainment parameters at zero, as decided in ADR-0025 and ADR-0026.

Measure V8 again: each mode's eleventh year after its own ten-year
spin-up from the initial state (ADR-0023 §15). Report the temperature,
precipitation, cloud effects, total and high cover, and vapour by layer.
Record the remaining gap without changing V8's limits. Check budgets,
determinism and the L5/L6 transport and performance gates. M9 is outside
this task.

## Implementation

- The preset enables moistening; the low-level parameter default stays
  off for explicitly constructed columns.
- `planet_cli parity` reports duration-weighted absorbed sunlight,
  outgoing longwave, the surface-upward-minus-outgoing longwave effect,
  clear-sky absorbed sunlight, cloud shortwave and longwave effects,
  total and high cover, and vapour paths by layer. It also reports the
  worst measurement step's energy and water closure gate ratios. The
  existing cloud-water stock remains the final step's value.
- `test_convective_moistening_run` compares a year with moistening on and
  off at L3, checks each month's energy and water closure and column
  convergence, verifies increased middle-layer vapour, and compares the
  full state hash at one and four workers.

## Calibration

```sh
./build/planet_cli thermal --subdivision 4 --seed 1 --years 150 \
  --layers 3 --coupled --water --workers 16 \
  --dry-depth 1.19676046 --vapour-absorption 0.00598380212 --calibrate 288
```

The common scale is **0.9287109375** of M8-05's pair, preserving the
dry/vapour split:

- **τ_d = 1.1114445287695311**;
- **κ_v = 0.005557222476679688 m²/kg**;
- **288.004958 K**, P2 equator-to-pole difference **49.3793 K** (target
  42 K, reported), peak poleward heat transport **2.74753 PW**.

The cloud constants remain τ_a = 600 s, W_c = 0.05 kg/m² and r_e = 18 µm.
The fitted pair decreases by 7.13 %: convection now carries water into
the middle layer, increasing the greenhouse effect without a change to
the radiation equations.

Bisection's 150-year trials, all from the same initial climate:

| Scale of M8-05's pair | Final-year surface mean (K) |
|---|---:|
| 0.9000000000 | 287.026146 |
| 1.0500000000 | 292.830631 |
| 0.9750000000 | 289.607425 |
| 0.9375000000 | 288.379828 |
| 0.9187500000 | 287.741480 |
| 0.9281250000 | 287.961362 |
| 0.9328125000 | 288.202222 |
| 0.9304687500 | 288.125233 |
| 0.9292968750 | 288.023114 |
| 0.9287109375 | 288.004958 |

## Independent checks on the fitted preset

```sh
ctest --test-dir build --output-on-failure --parallel 4
python3 tools/ci/check_fp_flags.py build
./build/tests/test_convective_moistening_run 4 150 4
./build/tests/test_convective_moistening_run 5 250 16
./build/tests/test_convective_moistening_run 6 250 16
./build/planet_cli parity --subdivision 3 --seed 1 --layers 3 \
  --spin-up-years 10 --days 365 --workers 4
```

The long-test arguments are level, years and workers. Every month's
column convergence and water/energy budgets are checked; the L4/150-year
run also gates the mean and cloud targets. The reported climatological
latent heating is L_v times each cell's final-year mean precipitation,
as in M8-05's V9 report and `test_reference_moist_convection`.

The fitted thermal run gives:

| V9 quantity | Result | Target |
|---|---:|---|
| Surface mean | 288.005 K | 288 K |
| Cloud cover | 0.5712 | 0.5–0.7 |
| Shortwave cloud effect | −45.75 W/m² | −47 ± 30 % |
| Longwave cloud effect | +25.81 W/m² | +26 ± 30 % |
| Largest final-year cell-mean L_v P | 211.32 W/m² | 300 W/m², reported |
| Cover above the bottom layer | 0.02224 | Reported; Earth's high cloud about 0.2–0.3 |
| P2 equator-to-pole difference | 49.38 K | 42 K, reported |

All global cloud targets pass without another cloud-constant adjustment.
The zonal cloud pattern and missing high cloud remain the limitations
recorded in ADR-0025 §14. The final year's land/ocean means are
276.36/292.76 K, TOA imbalance −2.36 W/m² (relative −0.01002), and
energy closure 6.79e-6 of its gate. This is the specified 150-year fit,
not a claim of a stationary cryosphere or full ocean equilibrium.

The Release build uses GCC 11.4.0. After removing the temporary solver
diagnostics, the full headless suite passes: **100/100**, 38.02 s. The
floating-point policy check passes on **151 translation units**, zero
violations.

The independent V9 run also passes: **288.004 K**, cover 0.571181,
shortwave −45.7525 and longwave +25.8083 W/m²; its 0.001 K difference
from the thermal fit is below the fit tolerance. Across all 150 years,
energy closes within 1.14e-5 of its gate and water within 1.32e-4, with
no unconverged final column and no failed circulation month.

**Initial transport limitation:** five L4 solves hit the 30-iteration
cap, all in the first six months (zero-based indices 0, 1, 2, 4, 5),
worst consistency residual 106.57 W/m². A repeat of the first year finds
the same five. No later month hits the cap during the 150-year run;
the final month's residual is 1.54e-6 W/m². The source applied is
conservative and the column budgets close, as in ADR-0024's documented
initial L6 months; this refit makes no change to the solver. Full
transport convergence from the initial state is therefore still a
limitation, not a passing assertion.

## V8 parity

The prescribed independent-start measurement completes at L3 (ten-year
spin-up plus year 11 in each mode). Precipitation passes its relative
gate, while the temperature limit fails:

| Year-11 quantity | Climate | Reference |
|---|---:|---:|
| Surface mean | 290.100 K | 294.248 K |
| Precipitation | 1.55345 m/year | 1.50490 m/year |
| Total cloud cover | 0.571318 | 0.450904 |
| High cloud cover | 0.030635 | 0.191524 |
| Cloud shortwave effect | −46.0659 W/m² | −44.0182 W/m² |
| Cloud longwave effect | +27.822 W/m² | +40.044 W/m² |
| Greenhouse proxy (surface-up minus OLR) | 170.252 W/m² | 168.161 W/m² |
| Clear-sky absorbed sunlight | 280.807 W/m² | 293.477 W/m² |
| Vapour by layer, bottom to top | 23.2025 / 8.86352 / 0.970055 kg/m² | 30.9753 / 11.3886 / 1.61982 kg/m² |
| Energy closure / gate | 6.90e-6 | 0.03182 |
| Water closure / gate | 8.32e-5 | 1.49e-5 |

The reference-minus-climate temperature gap is **4.148 K** against the
0.3 K limit. The reference/climate precipitation ratio is **0.968746**,
within the 10 % limit. Reference mode retains substantially more high
cloud and vapour in every layer; its larger longwave cloud effect and
clear-sky absorbed sunlight coexist with a similar greenhouse proxy.
The refit therefore does not close V8's temperature gap. No V8 threshold
was changed. The run exits with status 3 for the failed temperature gate.

## L5/L6 transport and performance

The long tests check all monthly column solves and global budgets, in
addition to reporting transport residuals. V8 used four workers and its
reference measurement year took 142.56 s; the two performance runs are
sequential, each on sixteen workers. The V11 time limits remain M7's
405/1,350 s times 1.5: 607.5/2,025 s.

| Level, 250 years | L5 | L6 |
|---|---:|---:|
| Wall time | 299.35 s | Fails at first month |
| Simulated years/minute | 50.11 | — |
| Worst energy closure / gate | 1.20e-5 | — |
| Worst water closure / gate | 2.06e-4 | — |
| Failed circulation months | 0 | — |
| Months at the transport iteration cap | 6 | — |
| Worst transport consistency residual | 203.19 W/m² | — |
| Final month's transport residual | 6.59e-7 W/m² | — |
| Final-year surface mean | 288.257 K | — |
| Final-year cover | 0.57975 | — |
| Final-year SW/LW cloud effects | −46.14/+25.82 W/m² | — |
| Largest final-year cell-mean L_v P | 218.51 W/m² | — |

L5 passes the performance and budget checks. A first-year repeat locates
its six capped transport solves at zero-based months 0, 3, 5, 8 and 9
(month 3 contains two capped solves); no later months cap during the
250-year run. The first L6 month does not complete: after 30 Newton
iterations the worst transport consistency residual is about 2.2 kW/m²,
and finishing the unconverged columns drives a layer temperature
non-positive. A diagnostic repeat showed a negative minimum source slope
(about −1.0 to −1.3) and persistent residual above 2.1 kW/m². Raising the
iteration limit experimentally to 200 still did not converge (residuals
of roughly 0.28–1.1 kW/m² in the first months); that experiment was
removed and is not part of the implementation. The prior M8 L6 result
does not establish this refit's L6 gate. A transport-method decision is
needed before claiming L6 support for the moistening preset. The solver
equations and the 30-iteration limit remain unchanged here.
