# Investigation INV-04 — V8 transport and radiation

- **Milestone:** P0, after INV-03 and before M9
- **Status:** investigation in progress (2026-10-22); current-preset V8 measured (4.146 K, fails)
- **Governing decisions:** ADR-0001 V2, ADR-0023 §15 (independent starts),
  ADR-0024 §9 (fallback), ADR-0025 §14 and ADR-0026 §8 (deferred options)
- **Predecessor:** [INV-03](INV-03-l6-startup-transport.md)
- **Scope:** measure the remaining temperature gap; distinguish cloud,
  cryosphere, transport and timestep effects before choosing a model change.
  V8's 0.3 K and 10 % limits remain unchanged. M9 is outside this work.

## Reporting audit

Two reporting errors were corrected without changing any physics:

1. `planet_cli parity` compared a layer index against the optional CLI
   `--layers` argument. With that argument absent it printed no vapour
   layers. It now uses the scenario's resolved layer count.
2. The CLI's precipitation ratio has always been **reference / climate**,
   although INV-02 and the status files called it climate / reference.
   The historical 0.968746 value is reference / climate; the historical
   table itself was correct. The CLI now prints the direction explicitly,
   and the documentation's direction is corrected. The gate is unchanged.

The report now includes land/ocean means, snow/ice cover, layer temperatures,
measurement duration, final state hash, TOA imbalance and a clear-sky
shortwave decomposition. At each actual step, using its starting snow/ice
and its forcing:

```
bare-surface absorbed sunlight − snow reflection − ice reflection
    = actual absorbed sunlight − cloud shortwave effect
```

These are read-only diagnostics. Reflection terms are positive losses of
absorbed energy relative to the same bare ground/open water, in global
W/m². They are not equilibrium forcings or causal temperature attributions.
`albedo_decomposition_error_W_m2` reports the maximum per-step discrepancy.
Snow/ice cover is the existing post-step cover diagnostic; the reflection
terms use the start-of-step albedos actually used by the solver.

`planet_cli_parity_report` checks default versus explicit three layers,
matching state hashes and diagnostic values, the shortwave decomposition,
tile-weighted temperature, finite layer outputs and the ratio direction.
Its one-day reference run and one-month climate run are only a smoke test,
not V8.

## Current-preset baseline

```bash
./build-release/planet_cli parity --subdivision 3 --seed 1 --layers 3 \
  --spin-up-years 10 --days 365 --workers 4
```

Each mode runs its own ten-year spin-up before measurement, as required by
ADR-0023 §15. The output includes the actual measurement duration: climate
mode takes complete monthly steps, whereas reference mode takes 365 days.
The existing protocol is retained, including this small duration difference.

The climate result is 290.090 K, with land 278.320 K and ocean 294.898 K.
Its clear-sky absorption is 280.803 W/m² against 295.870 W/m² for the same
bare surfaces: snow reflects 8.95660 W/m² and sea ice 6.10986 W/m².
The decomposition closes within 3.77e-13 W/m².

**Result (2026-10-22, `build-release/inv04/parity_L3_current.log`):**
V8 fails, at a reference-minus-climate gap of **4.146 K** (INV-02:
4.148 K). The precipitation ratio (reference / climate) is 0.9693, which
passes.

| Quantity | Climate | Reference | Reference − climate |
|---|---:|---:|---:|
| Global surface (K) | 290.090 | 294.236 | +4.146 |
| Land surface (K) | 278.320 | 290.626 | **+12.306** |
| Ocean surface (K) | 294.898 | 295.711 | +0.813 |
| Layer temperatures (K) | 274.7 / 252.5 / 223.6 | 281.9 / 262.4 / 232.5 | +7.2 / +10.0 / +8.9 |
| Snow cover (share of land) | 0.264 | 0.090 | −0.174 |
| Sea-ice cover (share of ocean) | 0.0857 | 0.0103 | −0.075 |
| Bare clear-sky absorbed (W/m²) | 295.870 | 295.863 | −0.007 |
| Snow reflection (W/m²) | 8.957 | 1.700 | −7.257 |
| Ice reflection (W/m²) | 6.110 | 0.729 | −5.380 |
| Clear-sky absorbed (W/m²) | 280.803 | 293.434 | +12.631 |
| SW cloud effect (W/m²) | −46.06 | −44.14 | +1.93 |
| Absorbed (W/m²) | 234.739 | 249.296 | +14.557 |
| OLR (W/m²) | 241.843 | 255.989 | +14.146 |
| LW cloud effect (W/m²) | +27.83 | +40.13 | +12.30 |
| Cover / high cover | 0.571 / 0.031 | 0.451 / 0.192 | −0.120 / +0.161 |
| Vapour by layer (kg/m²) | 23.19 / 8.85 / 0.97 | 30.99 / 11.39 / 1.62 | |
| TOA imbalance (W/m²) | −7.104 | −6.694 | |
| Energy / water residual ÷ gate | 7.7e-6 / 1.2e-4 | 3.2e-2 / 1.6e-5 | |

What the measurement shows, without attributing cause:
- **The gap is on land.** Land's +12.3 K at a 0.29 land fraction
  accounts for 3.57 K of the 4.15 K; the ocean differs by 0.8 K.
- **The shortwave difference is snow and ice, not clouds.** Over the same
  bare surfaces (295.87 W/m² in both), climate mode's snow and ice reflect
  12.6 W/m² more. The cloud shortwave effects differ by 1.9 W/m².
  Colder land keeps more snow, so this is consistent with both a cause
  and a feedback of the gap; the decomposition does not separate them.
- **The longwave cloud effect differs by 12.3 W/m²,** with reference
  mode's high cover six times climate mode's (as in INV-02).
- **Neither mode is in balance:** both lose about 7 W/m² at the top of
  the atmosphere during the measured year. Reference mode was still
  cooling in its spin-up (294.50 K in year 9, 293.96 K in year 10).
  So the ten-year spin-up does not reach equilibrium in either mode, and
  the gap compares two drifting states. That bears on V8's protocol
  rather than on the physics.

## V8 after a 20-year spin-up (2026-10-22)

Open task 1. `planet_cli parity --subdivision 3 --seed 1 --layers 3
--spin-up-years 20 --days 365 --workers 16`
(`build/inv04/parity_L3_spinup20.log`, 63 min):

| | Climate, 10 years | Climate, 20 years | Reference, 10 years | Reference, 20 years |
|---|---:|---:|---:|---:|
| Surface (K) | 290.090 | 289.320 | 294.236 | 294.039 |
| Land / ocean (K) | 278.32 / 294.90 | 277.66 / 294.08 | 290.63 / 295.71 | 290.52 / 295.48 |
| Snow (share of land) / sea ice (of ocean) | 0.264 / 0.086 | 0.268 / 0.091 | 0.090 / 0.010 | 0.091 / 0.012 |
| TOA imbalance (W/m²) | −7.10 | −5.79 | −6.69 | −6.15 |

**V8: 4.719 K** (10 years: 4.146 K); precipitation ratio 0.976, passing.

- **The gap moves, and grows:** climate mode keeps cooling with its deep
  ocean (−0.77 K in ten years); reference mode hardly does.
- **Reference mode's year-to-year variation is about ±0.3 K:** its
  spin-up years 10–20 run 293.69–294.40 K. A one-year measurement of
  reference mode is uncertain by about V8's whole 0.3 K limit.
- Both modes still lose about 6 W/m² at the top of the atmosphere.

## Existing transport configurations: sensitivity, not a replacement

```bash
./build-release/planet_cli thermal --subdivision 3 --seed 1 --layers 3 \
  --years 11 --water --workers 4 --coupled
./build-release/planet_cli thermal --subdivision 3 --seed 1 --layers 3 \
  --years 11 --water --workers 4
```

Same radiation pair and cloud constants; year 11 means from the same
initial state, GCC 11.4 Release:

| Quantity | Coupled circulation | Diffusive configuration |
|---|---:|---:|
| Global surface (K) | 290.090438 | 293.120702 |
| Land surface (K) | 278.319733 | 283.043637 |
| Ocean surface (K) | 294.898190 | 297.236686 |
| Peak poleward transport (PW) | 2.84922 | 3.88294 |
| P2 equator-to-pole contrast (K) | 49.0682 | 34.3063 |
| Total cloud cover | 0.5713 | 0.5207 |
| High cloud cover | 0.03061 | 0.01641 |
| SW cloud effect (W/m²) | −46.06 | −41.42 |
| LW cloud effect (W/m²) | +27.83 | +26.83 |
| Final snow mass (kg) | 1.16018e16 | 4.60816e15 |
| Final sea-ice mass (kg) | 1.93361e17 | 4.64452e16 |
| Worst energy residual / gate | 6.31220e-6 | 4.21172e-6 |

The diffusive configuration is **3.030264 K warmer**, while its high-cloud
cover and longwave cloud effect are smaller. Thus a temperature sensitivity
comparable to the outstanding gap exists outside increased high cloud.
This does not identify a single missing heat flux: disabling the coupled
circulation also changes humidity transport and the winds used by evaporation.
It is not a controlled heat-only comparison, and no replacement or refit is
accepted from it. The diffusive coefficient is the old calibrated fallback,
not a new fit to reference mode.

### Heat-only: the diffusive heat transport with everything else coupled (2026-10-22)

Open task 4. ADR-0024 §9's fallback is the controlled comparison: it
replaces only the coupled heat transport by ADR-0009's diffusion, while
the circulation's winds, the humidity transport and the eddies' latent
heat E stay coupled. A fallback threshold just above zero takes it every
month:

```bash
./build/planet_cli thermal --subdivision 3 --seed 1 --layers 3 \
  --years 11 --water --workers 6 --coupled --transport-fallback 1e-300
```

| Quantity | Coupled | Heat by diffusion, the rest coupled | Fully diffusive (above) |
|---|---:|---:|---:|
| Global surface (K) | 290.090 | 292.944 | 293.121 |
| Land / ocean (K) | 278.32 / 294.90 | 282.61 / 297.17 | 283.04 / 297.24 |
| Peak poleward transport (PW) | 2.849 | 3.829 | 3.883 |
| P2 equator-to-pole contrast (K) | 49.07 | 32.62 | 34.31 |
| Sea ice at the year's end (kg) | 1.934e17 | 4.386e16 | 4.645e16 |
| Snow at the year's end (kg) | 1.160e16 | 6.761e15 | 4.608e15 |
| Cover / high cover | 0.571 / 0.031 | 0.557 / 0.026 | 0.521 / 0.016 |
| Cloud SW / LW effect (W/m²) | −46.06 / +27.83 | −44.36 / +27.50 | −41.42 / +26.83 |

- **The heat transport alone gives 2.85 K** of the fully diffusive
  configuration's 3.03 K; the humidity transport and the evaporation
  winds less than 0.2 K.
- The diffusive heat moves a third more energy poleward (3.83 against
  2.85 PW), takes P2 from 49 K past Earth's 42 K to 33 K, and melts most
  of the sea ice. The clouds hardly change.
- That is V8's pattern (cold land and more snow and ice in climate mode,
  little cloud difference besides the high cloud). Whether reference
  mode's resolved transport is the stronger one is the next measurement;
  the diffusive coefficient is the old calibrated one, not a fit to
  reference mode.

### The modes' temperature profiles: reference mode moves more heat poleward (2026-10-22)

`planet_cli parity` now reports each mode's P2 equator-to-pole
difference (−1.5 P2, as `AnnualSurfaceSummary`) and its zonal surface
profile, duration-weighted. The 10-year parity run again
(`build/inv04/parity_L3_spinup10_p2.log`, 8 workers) reproduces the
baseline exactly (290.090 / 294.236 K, gap 4.146 K, ratio 0.9693).

| 10° band | 80–90° S | 60–70° S | 40–50° S | 0–10° S | 0–10° N | 40–50° N | 60–70° N | 80–90° N | P2 contrast |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Climate (K) | 254.8 | 257.5 | 285.2 | 303.4 | 301.2 | 289.5 | 255.9 | 249.3 | 49.1 |
| Reference (K) | 274.4 | 281.5 | 291.8 | 302.6 | 302.6 | 291.1 | 274.3 | 263.6 | 30.3 |
| Reference − climate | +19.6 | +24.0 | +6.6 | −0.8 | +1.4 | +1.6 | +18.4 | +14.3 | −18.8 |

- **The gap is at high latitudes.** The tropics agree to about a kelvin;
  poleward of 60° reference mode is 14–24 K warmer.
- **Reference mode's resolved circulation carries more heat poleward**
  than climate mode's coupled one: its P2 contrast is 30.3 K against
  49.1 K (Earth about 42 K).
- **The diffusive heat transport (heat-only comparison above) gives a
  32.6 K contrast,** close to reference mode's, and a climate 2.85 K
  warmer: 292.94 K against reference mode's 294.24 K, a gap of 1.30 K
  instead of 4.15 K, from the same 10-year start. Its coefficient is the
  old calibrated one, not a fit to reference mode.
- So most of V8's gap is the coupled circulation's weak poleward heat
  transport, with the snow and ice its consequence at high latitudes.
  The coupled circulation's eddy coefficient c_E (1.39e12) is reference
  mode's fit from ADR-0011; whether that fit still holds with ADR-0024's
  moist eddies and the clouds is the next question.

### Reference mode at L4: its flat profile is L3's (2026-10-22)

`planet_cli parity --subdivision 4 --seed 1 --layers 3 --spin-up-years
10 --days 365 --workers 16` (`build/inv04/parity_L4_spinup10_p2.log`,
about 90 min, nearly all reference mode):

| | L3 climate | L3 reference | L4 climate | L4 reference |
|---|---:|---:|---:|---:|
| Surface (K) | 290.090 | 294.236 | 290.512 | 291.436 |
| Land / ocean (K) | 278.32 / 294.90 | 290.63 / 295.71 | 278.67 / 295.35 | 287.97 / 292.85 |
| P2 contrast (K) | 49.07 | **30.28** | 48.49 | **46.57** |
| Polar bands 80–90° S / N (K) | 254.8 / 249.3 | 274.4 / 263.6 | 250.3 / 256.6 | 255.1 / 252.5 |
| Snow (of land) / sea ice (of ocean) | 0.264 / 0.086 | 0.090 / 0.010 | 0.273 / 0.057 | 0.171 / 0.095 |
| Convective share | 0.99 | 0.38 | 0.99 | 0.23 |
| TOA imbalance (W/m²) | −7.10 | −6.69 | −7.53 | −8.39 |

**V8 at L4: 0.924 K** (L3: 4.146 K); precipitation ratio (reference /
climate) 0.901, just outside its 10 %.

- **Reference mode's flat profile is a property of L3.** At L4 its P2
  contrast is 46.6 K, against 30.3 K at L3 (Earth about 42 K); climate
  mode's barely changes (48.5 against 49.1 K).
- **So at L3 V8 mostly measures reference mode's coarse-grid transport.**
  At L4 the modes' profiles agree within 2 K in P2 and the global gap
  is 0.9 K, with land still 9.3 K warmer in reference mode.
- Reference mode at L4 was still cooling at the end of its spin-up
  (292.50 K in year 9, 291.55 K in year 10), as at L3.
- The L3 transport table below (reference mode's large latent transport)
  is then L3's resolved flow, not a target for climate mode.

### The modes' poleward transports, dry and latent (2026-10-22)

`planet_cli parity` now reports each mode's annual northward transport
across 80° S … 80° N (`build/inv04/parity_L3_spinup10_transport.log`,
the same 10-year runs, results identical to the baseline):

- climate mode: dry, the coupled transport's H (as
  `northward_transport_W`); latent, L_v of the water the humidity
  transport moved north of each latitude;
- reference mode: each dynamics step's change of the columns' dry energy
  Σ μ_k c_p T_k + p_s z_s and latent energy L_v Σ μ_k q_k, summed north
  of each latitude by 10° band of cell centres (kinetic energy left out).

| Latitude | Climate dry | Climate latent | Climate total | Reference dry | Reference latent | Reference total |
|---|---:|---:|---:|---:|---:|---:|
| 80° S | −0.38 | −0.03 | −0.41 | −0.21 | −0.14 | −0.35 |
| 70° S | −1.51 | −0.15 | −1.66 | −0.48 | −0.88 | −1.36 |
| 60° S | −2.06 | −0.29 | −2.35 | −0.48 | −1.43 | −1.91 |
| 50° S | −2.22 | −0.40 | −2.62 | −0.52 | −1.53 | −2.05 |
| 40° N | 2.85 | 0.37 | 3.22 | 0.99 | 2.46 | 3.45 |
| 50° N | 2.64 | 0.31 | 2.95 | 1.17 | 2.37 | 3.54 |
| 60° N | 1.76 | 0.18 | 1.94 | 1.19 | 1.32 | 2.51 |
| 70° N | 1.03 | 0.06 | 1.09 | 0.91 | 0.61 | 1.51 |
| 80° N | 0.43 | 0.03 | 0.46 | 0.46 | 0.20 | 0.66 |

(PW, positive northward.)

- **The split differs most.** Climate mode's latent transport is small
  (0.37 PW at 40° N; Earth's is about 1.5 PW) and its dry eddies carry
  nearly everything. Reference mode carries two thirds or more as vapour.
- **Northern hemisphere:** reference mode carries 30–40 % more energy
  poleward of 50°.
- **Southern hemisphere:** reference mode carries *less* (−1.36 against
  −1.66 PW at 70° S, −0.35 against −0.41 at 80° S), yet its southern
  polar bands are 20–24 K warmer. The total transport does not explain
  the southern gap; the snow and ice on climate mode's southern land
  (snow-covered land 0.26 against 0.09 of the land) are the next
  candidate there.
- So the "weak transport" reading of the P2 profiles holds in the north
  only, and the modes' moisture transports differ by a factor of about
  six.

### The coupled transport's sensitivity to c_E (2026-10-22)

L3, seed 1, year 11, `planet_cli thermal --subdivision 3 --layers 3
--years 11 --water --coupled --eddy-generation c_E`:

| c_E | P2 contrast (K) | Surface (K) | Land (K) | Peak poleward (PW) | Cloud LW (W/m²) | Failed months |
|---:|---:|---:|---:|---:|---:|---:|
| 1.39e12 (preset) | 49.07 | 290.090 | 278.32 | 2.849 | +27.83 | 0 |
| 2.08e12 (×1.5) | 44.91 | 290.824 | 279.28 | 3.064 | +28.48 | 0 |
| 2.78e12 (×2) | 41.27 | 291.715 | 280.25 | 3.250 | +29.22 | 0 |
| 4.17e12 (×3) | 36.75 | 292.796 | 281.40 | 3.447 | +30.47 | 1 |

- The transport grows less than in proportion to c_E: the eddies'
  generation goes with the square of the gradient they flatten.
  Reference mode's 30.3 K would need well beyond ×3, where a circulation
  month already fails.
- **Against Earth, the modes err on opposite sides:** P2 about 42 K
  (ADR-0010 §4.6's target); the preset's coupled circulation gives 49 K,
  reference mode 30 K. c_E × 2 gives Earth's 41 K, but leaves V8's gap
  near 2.5 K.
- So V8's temperature gap is mostly a disagreement between the two
  modes' poleward transports, and making climate mode match reference
  mode would move it away from Earth's contrast. Which mode's transport
  is wrong is the question for the decision.

## Column timestep and the radiation/adjustment ordering

Code audit: `solve_atmosphere_column` captures the budget's OLR and surface
fluxes from the implicit solve **before** dry/moist convective adjustment and
microphysics. The final temperatures include those adjustments. The cloud
longwave diagnostic in `step_surface_energy` recomputes cloudy-minus-clear
radiation at those **post-adjustment** temperatures (and the same upward
surface flux). Therefore cloud LW effect and budget OLR describe different
stages of the step; subtracting one from the other does not give the actual
implicit solve's clear-sky OLR. Both diagnostics have legitimate meanings,
but their difference cannot by itself decompose the budget.

[`tools/v8/column_timestep_probe.cpp`](../../tools/v8/column_timestep_probe.cpp)
is a reproducible controlled experiment using the current three-layer
preset. A held 302 K surface supplies 4e-5 kg/m²/s vapour at 100 kPa. Every
case starts at the same T = [290, 260, 225] K and 80 % RH, evolves for
120 days and averages days 90–120. The radiation and cloud state evolve;
no horizontal transport or diurnal forcing is present. The surface is an
external reservoir, so this is not a planetary climate or V8 test.

```bash
c++ -std=c++20 -O3 -ffp-contract=off -I . \
  tools/v8/column_timestep_probe.cpp build-release/libPlanetSim.a \
  -lzstd -pthread -o build-release/column_timestep_probe
./build-release/column_timestep_probe
```

| Step | Bottom-layer mean (K) | Budget OLR (W/m²) | Post-adjustment OLR minus budget OLR (W/m²) | Rain (mm/day) |
|---|---:|---:|---:|---:|
| 10 minutes | 284.218 | 284.200 | +0.01317 | 3.45493 |
| 1 hour | 284.212 | 283.916 | +0.08979 | 3.45493 |
| 1 day | 286.434 | 295.899 | +1.91252 | 3.35986 |
| 5 days | 285.168 | 270.364 | +5.56623 | 3.45672 |
| 30 days | 282.781 | 269.077 | +4.11702 | 3.49731 |

All columns converge; maximum water closure error is 2.58e-14 kg/m² and
maximum energy closure error 1.13e-9 W/m² across these runs. This is a
finite-duration comparison, not proof of equilibrium or monotone timestep
convergence. It demonstrates measurable timestep dependence even without
winds, and a radiation-diagnostic stage difference much larger at long
steps. It does not establish how many kelvin of global V8 error comes from
the splitting, nor that changing the order alone would remove it.

## Same starting planet, shorter surface steps

[`tools/v8/surface_timestep_probe.cpp`](../../tools/v8/surface_timestep_probe.cpp)
starts every case from the same ten-year monthly L3 climate. It retains
each month's mean sunlight and computes its circulation and balanced
pressure once at the month's start. It then divides the existing
surface/column/transport solve into 1, 2, 6 or 30 equal pieces. Moisture,
clouds, snow and ice evolve within those pieces. The following year's
duration-weighted means are:

| Pieces/month | Surface (K) | Absorbed SW (W/m²) | Budget OLR (W/m²) | Rain (m/year) | High cover |
|---|---:|---:|---:|---:|---:|
| 1 | 290.090439 | 234.738775 | 241.843228 | 1.553374 | 0.030606 |
| 2 | 289.792049 | 233.710467 | 239.176538 | 1.592598 | 0.048692 |
| 6 | 289.540179 | 232.914795 | 238.543600 | 1.610751 | 0.084076 |
| 30 | 288.463644 | 231.771232 | 252.229462 | 1.702587 | 0.030367 |

The one-piece control reproduces the CLI's climate result. All cases
converge without transport fallbacks or failed circulation months. Maximum
energy/water residuals are 1.820e-4 / 1.172e-4 of their gates. On one worker,
the measurement year takes 1.32, 1.87, 3.40 and 13.22 s respectively on
this machine while the separate reference experiment is running; these
are timings of the experiment, not performance-gate measurements.

This is a one-year response to a changed timestep, not independently
spun-up climates or V8. It holds monthly forcing and circulation, so it
does not isolate the column from transport, nor reproduce reference
mode's resolved weather. Nevertheless, **simple surface subcycling cools
this planet by up to 1.627 K**, away from the warmer reference result.
In this experiment, shorter steps do not offer a direct temperature-parity
fix. No production subcycling is introduced.

Build/run as for the column probe, replacing `column_timestep_probe.cpp`
with `surface_timestep_probe.cpp` and the executable name accordingly.

## The 30-piece case: an oscillation of the lagged cloud longwave (2026-10-22)

Open task 3. Thirty pieces a month are steps of about a day, and the
column probe's 1-day case is out of line too (budget OLR 295.9 W/m²
between 284 at an hour and 270 at five days). The column probe now takes
step lengths as arguments, `--no-cloud-longwave` and
`--fixed-vapour-radiation`, and reports the spread of OLR over days
90–120 and its largest change between consecutive steps:

```bash
./build/column_timestep_probe 600 3600 21600 43200 86400 172800 432000 2592000
./build/column_timestep_probe --no-cloud-longwave 3600 43200 86400 432000
```

| Step | OLR (W/m²) | OLR spread | Largest step-to-step change | Without cloud LW: OLR / largest change |
|---|---:|---:|---:|---:|
| 10 minutes | 284.20 | 0.04 | 6e-5 | |
| 1 hour | 283.92 | 0.04 | 4e-4 | 326.63 / 6e-4 |
| 6 hours | 299.99 | 7.98 | 16.2 | |
| 12 hours | 300.99 | 10.88 | 32.1 | 326.97 / 2.39 |
| 1 day | 295.90 | 11.72 | 35.4 | 327.04 / 2.35 |
| 2 days | 283.28 | 2.66 | 8.5 | |
| 5 days | 270.36 | 0.05 | 0.04 | 325.83 / 0.07 |
| 30 days | 269.08 | 0 | 0 | |

- **Steps of 6 hours to 2 days oscillate from step to step,** by up to
  35 W/m² in OLR. Shorter and longer steps are smooth.
- **The cloud longwave drives it.** The radiation takes the cloud water
  at the start of the step (ADR-0023 §4.4, held through the step).
  Without the clouds' longwave the largest change is 2.4 W/m²; holding
  the radiation's vapour at its starting value changes nothing (34.0
  W/m² at a day).
- **So the 30-piece case is this oscillation, not a timestep trend,** and
  its 1.6 K cooling is not evidence about the month's step. The 1-, 2-
  and 6-piece cases (steps of 30, 15 and 5 days) are outside the window.
  Neither production step is in it: climate mode takes a month,
  reference mode 10 minutes.
- **The cloud longwave is also most of the column's step dependence:**
  without it budget OLR moves by 0.8 W/m² between an hour and five days;
  with it, by 13.6 W/m² (the cloud longwave effect 42.7 W/m² at an hour,
  55.5 at five days, with similar cloud water, 26.8 and 24.2 g/m²). How
  much of that reaches V8 is not measured: on the planet reference
  mode's cloud longwave effect is the larger one (+40.1 against +27.8
  W/m²).

## ADR-0025's options on top of ADR-0026's moistening (2026-10-22)

Open task 6. Climate mode, L4, seed 1, three layers, ten years from the
initial state, the current preset (`planet_cli thermal --subdivision 4
--years 10 --layers 3 --coupled --water` plus the option):

| Option | Surface (K) | Cover | High cover | 20–40° | Poles | Cloud LW (W/m²) |
|---|---:|---:|---:|---:|---:|---:|
| Preset | 290.72 | 0.596 | 0.030 | 0.68–0.69 | 0.06–0.14 | +27.7 |
| β = 0.2 (§4.1) | 290.72 | 0.596 | 0.030 | 0.68–0.69 | 0.06–0.14 | +27.7 |
| β = 0.2, W_np = 0.003 kg/m² (§11) | 305.60 | 0.654 | 0.174 | 0.64–0.67 | 0.34–0.40 | +72.0 |
| e_d = 0.5 (§4.3, §13) | 312.73 | 0.488 | 0.355 | 0.46–0.47 | 0.13–0.26 | +78.4 |

The moistening leaves M8-07's and M8-09's findings as they were: β alone
changes nothing in climate mode, and the non-precipitating cloud and the
anvils add high liquid cloud whose longwave effect warms the planet by
15–22 K before any refit. They stay off.

## The spin-up's drift: the deep ocean (2026-10-22)

Climate mode, L3, seed 1, three layers, from the initial state with no
spin-up before tick 0, yearly means. Year 11 reproduces the parity run
(290.090 K, −7.104 W/m²), so the pre-tick-0 spin-up is equivalent to
running forward. Reproduce with
[`tools/v8/climate_drift_probe.cpp`](../../tools/v8/climate_drift_probe.cpp)
(built as the column probe; argument: the number of years).

| Year | 1 | 11 | 25 | 50 | 100 | 150 |
|---|---:|---:|---:|---:|---:|---:|
| Surface (K) | 305.05 | 290.09 | 289.22 | 288.83 | 288.25 | 287.90 |
| Land (K) | 299.05 | 278.32 | | 277.17 | 276.60 | 276.34 |
| TOA net (W/m²) | −24.95 | −7.10 | −5.58 | −4.58 | −3.15 | −2.17 |
| Deep ocean, ocean-weighted (K) | 309.72 | | 307.49 | 305.33 | 302.06 | 299.82 |
| Snow on land (kg) | 2.24e15 | | 1.85e16 | 2.98e16 | 5.26e16 | 7.47e16 |
| Sea ice (kg) | 6.12e15 | | 3.11e17 | 4.13e17 | 5.47e17 | 6.53e17 |

- **The imbalance is real and is the deep ocean draining.** The start is
  deliberately warm (ADR-0010 §4.6, `initialise_surface_temperatures`):
  every column's grey equilibrium, deep ocean included (about 310 K).
  The deep layer's 2.5e9 J/m²/K over the ocean fraction, cooled by 9.9 K
  in 150 years, is about 3.7 W/m² on average: essentially the whole
  imbalance. Its e-folding time is about 135 years, and at year 150 it
  is still about 5 K above the surface ocean.
- **So a ten-year spin-up measures both modes in transit.** Climate mode
  cools another 2.2 K between years 11 and 150 (land 2.0 K). Reference
  mode cools faster near year 10 (0.54 K from year 9 to 10, against
  climate mode's 0.29 K).
- **Snow on land grows without bound** (about 4.4e14 kg a year at the
  end, not slowing): no store removes it. Its latent heat is about
  0.01 W/m², and its albedo saturates (W / (W + W_m)), so it barely
  affects the energy budget, but the land's water never reaches
  equilibrium.
- **Brute force is too expensive:** reference mode takes about 4 minutes
  per L3 year on 4 workers, so a spin-up of several centuries would take
  more than a day.

## Next decision

The gap is a land and cryosphere gap (above), with the high-cloud
longwave difference as before. Before choosing a model change: measure
both modes after a spin-up long enough to bring the top-of-atmosphere
imbalance near zero, and compare land temperature with snow cover held
equal in the two modes, to separate the snow-albedo feedback from its
cause. No solver, field meaning,
PSNAP schema, preset or acceptance threshold has changed. ADR-0026's
previously deferred options remain deferred; temperature tuning alone is
not evidence for accepting them.

## Open tasks (2026-10-22)

**V8 (this investigation), in order:**
1. ~~**V8 with a 20-year spin-up.**~~ Done (above): 4.719 K; climate
   mode keeps cooling, reference mode varies ±0.3 K a year.
2. **Depending on 1:**
   - if the gap moves, propose amending ADR-0023 §15's independent
     starts: spin up the deep ocean once in climate mode, then give both
     modes that state and their own 10–20-year spin-ups;
   - if it holds, compare land temperature with snow cover held equal
     in both modes, to separate the snow-albedo feedback from its cause.
3. ~~**The 30-piece surface-timestep case.**~~ Explained (above): steps
   of 6 hours to 2 days oscillate through the lagged cloud longwave; the
   case is not a timestep trend.
4. ~~**A heat-only transport comparison.**~~ Done (above): the heat
   transport alone gives 2.85 K of the 3.03 K. Next: reference mode's
   poleward heat transport against the coupled circulation's 2.85 PW.
5. **High cloud:** reference mode's 0.19 against climate mode's 0.03,
   which gives a 12.3 W/m² longwave cloud difference here (V7's known
   limitation is about 18 W/m²).
6. ~~**ADR-0025's cloud work measured again**~~ (above): unchanged by
   the moistening; it stays off.

**Model, found on the way:**
7. **Snow on land grows without bound** (about 4.4e14 kg a year at L3
   after 150 years): nothing removes it. A sink, such as ice-sheet flow
   or a cap, needs a decision; it is probably outside P0.
8. **L6's coupled transport:** a solver that handles the kinks in the
   columns' response (ADR-0024 §9.5) would make the fallback rarer.
9. **The L4 fit's fragility:** it passes because its cold start has no
   fallback month. If a refit brings one back, the option is a fallback
   triggered by the columns failing (INV-03), by amendment.
10. **`PlanetRun` has no resume constructor,** and PRUN replay cannot
    start from a snapshot (INV-03).
11. **M8's known limitations** (ADR-0025 §14): §4.4's zonal cloud
    targets (V5) and V7's longwave difference.

**Decisions waiting:**
12. **ADR-0020** (the planet's read contract) is proposed. It must not
    be implemented until it is accepted.
13. **M9** starts only on explicit request.

**Housekeeping:**
14. Push the local commits.
15. `git stash`: `stash@{0}` (the continuous fallback blend) can be
    dropped. `stash@{1}` holds only Godot's import changes to
    `godot/project.godot`, which are always reverted; it can be dropped
    too.
16. Rebuild `build` and `build-godot` (Release) after every pull.
