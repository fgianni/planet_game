# Investigation INV-04 — V8 transport and radiation

- **Milestone:** P0, after INV-03 and before M9
- **Status:** investigation in progress (2026-10-23). V8 at L4 (ADR-0023 §16) fails at 0.936 K and
  0.901; the cause found so far is climate mode's dry land (below). No solver or preset change
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

## V8 at L4 over three years: the baseline (2026-10-22)

ADR-0023 §16's measurement (`planet_cli parity --subdivision 4 --seed 1
--layers 3 --spin-up-years 10 --days 1096 --workers 20`, 6,263 s wall,
log `build/inv04/v8_L4_3yr.log`):

| | Climate | Reference |
|---|---|---|
| Surface, years 11–13 (K) | 290.359 | 291.295 |
| Land / ocean (K) | 278.57 / 295.18 | 287.78 / 292.73 |
| Snow share of land / ice share of ocean | 0.275 / 0.058 | 0.175 / 0.096 |
| P2 equator-to-pole contrast (K) | 48.7 | 47.0 |
| Precipitation (m/yr) | 1.610 | 1.451 |
| Convective share | 0.99 | 0.23 |
| Top-of-atmosphere imbalance (W/m²) | −7.0 | −8.2 |

- **V8 fails:** 0.936 K (limit 0.3 K), precipitation ratio 0.901
  (limit ±10 %). The one-year L4 values were 0.924 K and 0.901, so
  averaging three years changes little.
- **The gap is land against ocean:** reference land is 9.2 K warmer,
  its ocean 2.4 K colder. Weighted by area (0.29 land) that is +2.67 K
  and −1.74 K, which give the 0.94 K. A gap near zero in the global mean
  would hide both.
- Reference mode has less snow on land but more sea ice; the zonal
  means differ most at 30–40° S (reference +3.7 to +4.3 K) and at the
  poles (90–80° S +4.8 K, 80–90° N −4.4 K).
- Reference mode carries most of its poleward heat as vapour (2.5 PW at
  40° N against climate mode's 0.46 PW); climate mode carries it dry.

## Why reference land is warmer at L4 (2026-10-23)

V8 under ADR-0023 §16.2 again, with `planet_cli parity`'s land
diagnostics (commit `c21d957`; `build/inv04/v8_L4_3yr_land.log`, 16
workers, 151.6 min). It reproduces §16.4's baseline exactly (0.935815 K,
0.901286).

**Land minus ocean by 10° band (K), south to north:**

| Band from | 90S | 80S | 70S | 60S | 50S | 40S | 30S | 20S | 10S | 0 | 10N | 20N | 30N | 40N | 50N | 60N | 70N | 80N |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Climate | −6.0 | −24.4 | −23.2 | −30.4 | −27.8 | −18.6 | −8.8 | −10.0 | −10.3 | −10.1 | −11.8 | −13.0 | −15.4 | −27.8 | −29.5 | −24.2 | −22.5 | −26.5 |
| Reference | −0.9 | −2.5 | −5.4 | −4.3 | −2.7 | −4.1 | −5.6 | −5.5 | −4.0 | −3.3 | −4.4 | −3.8 | −4.0 | −3.9 | −7.7 | −4.2 | −1.5 | −3.0 |

**The land tiles' budget, per m² of land (W/m²):**

| | Absorbed sunlight | Source from the air | Emitted | Evaporation | Melt | Residual |
|---|---:|---:|---:|---:|---:|---:|
| Climate | 186.69 | 212.65 | 340.50 | 58.82 | 0.03 | −9e-14 |
| Reference | 196.33 | 242.68 | 377.38 | 60.45 | 1.19 | −7e-11 |
| Reference − climate | +9.64 | **+30.03** | +36.88 | +1.64 | +1.16 | |

**Snowmelt:** climate 0.0026 m/yr of land, 21 % in months whose land
surface averaged below 273.15 K; reference 0.112 m/yr, **73 %** in such
months.

What it shows:
- **Climate mode's land is cold at every latitude, not only where there
  is snow.** In the tropics (30° S–30° N), where there is no snow, its
  land is 9–13 K colder than its ocean; reference mode's is 3–6 K
  colder. Poleward of 40° the difference grows to 22–30 K in climate
  mode, but stays 1–8 K in reference mode.
- **Three quarters of reference land's extra heat comes from the air:**
  +30.0 W/m² of source (downward longwave and sensible heat), against
  +9.6 W/m² of sunlight (less snow). Reference land emits 36.9 W/m²
  more, as a surface about 9 K warmer would.
- **Sub-monthly melt is real but secondary.** Reference mode melts 44
  times as much snow, three quarters of it in months whose mean is below
  freezing, which a monthly step cannot do. But that melt's heat is
  1.2 W/m², and the snow's whole sunlight effect is 9.6 W/m², a quarter
  of the land difference.
- **So the main gap is in how climate mode's land and its air exchange
  heat with the ocean's.** A climate land column receives much less from
  its air than a reference one. Two places it could come from:
  - the transport: it gives each group of about four cells (ADR-0009
    §12, `agglomerated_transport_graph`) one source per m², and diffuses
    the groups' moist-enthalpy temperature, so it may deliver too little
    heat to land columns;
  - the land column itself: its radiation, convection and clouds over
    dry land. Climate mode's convective share is 0.99 against 0.23.

The next measurement separates the two: the air's bottom-layer
temperature and humidity over land and over ocean by band in both modes,
the heat the transport delivers to land columns against ocean columns,
and the cloud longwave effect over land.

## Climate mode's land is dry: its transport brings almost no water (2026-10-23)

V8 under ADR-0023 §16.2 with the air and transport over land and ocean
(commit `1c94d89`; `build/inv04/v8_L4_3yr_air.log`, 170.5 min). It
reproduces §16.4 exactly (0.935815 K, 0.901286). Over land and over
ocean, each cell weighted by its land or ocean area:

**Bottom-layer air, land minus ocean (K), by 10° band from 90° S:**

| | 90S | 80S | 70S | 60S | 50S | 40S | 30S | 20S | 10S | 0 | 10N | 20N | 30N | 40N | 50N | 60N | 70N | 80N |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Climate | −3.6 | −20.4 | −15.9 | −23.1 | −22.2 | −15.6 | −8.5 | −9.8 | −10.1 | −10.1 | −11.6 | −11.9 | −13.1 | −23.3 | −22.3 | −16.8 | −16.1 | −20.9 |
| Reference | +0.5 | −0.8 | −1.9 | −0.2 | +0.2 | −2.1 | −4.6 | −5.5 | −4.5 | −3.9 | −4.6 | −3.2 | −2.2 | −1.6 | −3.6 | −0.8 | +0.3 | −1.0 |

**Over land (per m² of land) and over ocean:**

| | Climate land | Reference land | Climate ocean | Reference ocean |
|---|---:|---:|---:|---:|
| Dry heat delivered by the transport (W/m²) | 28.14 | 21.14 | −11.49 | −8.63 |
| Latent heat delivered (W/m²) | **1.55** | **23.32** | −0.63 | −9.52 |
| Air humidity, equator band (g/kg) | 6.46 | 12.26 | 12.28 | 16.14 |
| Cloud longwave effect (W/m²) | **19.52** | **40.22** | 30.65 | 37.87 |
| Cloud shortwave effect (W/m²) | −21.15 | −27.39 | −58.61 | −42.84 |
| Precipitation (m/yr) | 0.832 | 1.124 | 1.928 | 1.584 |
| Convective share | 0.984 | 0.057 | 0.992 | 0.285 |

- **Reference mode's air over land is nearly as warm as over the ocean
  at the same latitude** (within 0–5.5 K). Climate mode's land air is
  8.5–23 K colder. So climate mode's land is cold because its air is
  cold, not only because the surface is poorly coupled to its air.
- **The difference is water, not dry heat.** Climate mode's transport
  delivers about as much dry heat to land as reference mode's (28 against
  21 W/m²), but 15 times less latent heat: 1.55 against 23.3 W/m², a
  moisture convergence of 0.02 against 0.29 m/yr (Earth's land is about
  0.27 m/yr). Its land air is about half as humid.
- **Dry land air means a weak greenhouse over land.** The cloud longwave
  effect over land is 19.5 W/m² in climate mode against 40.2 in
  reference mode: 20.7 W/m² of the 30.0 W/m² more that reference land
  receives from its air (above). Weaker water-vapour absorption adds to
  it.

**The coast, from the transport's groups** (climate mode, L4, after a
ten-year spin-up, three years;
[`tools/v8/land_groups_probe.cpp`](../../tools/v8/land_groups_probe.cpp)).
Both transports work on groups of about four cells (642 groups of 2 562
cells at L4). Each group's heat source is applied per m² to all its
cells, and each cell's humidity is scaled by its group's change, so
within a group land and ocean do not exchange heat or water.

| Land in | Share of land | Latent delivered (W/m²) | Dry delivered (W/m²) | Land surface (K) | Air q (g/kg) |
|---|---:|---:|---:|---:|---:|
| All-land groups (≥ 95 % land) | 0.527 | 3.00 | 31.35 | 282.47 | 4.86 |
| Mixed groups | 0.473 | −0.04 | 24.76 | 274.16 | 3.41 |

- **Nearly half of climate mode's land shares a group with ocean, and
  receives no water at all.** Its surface is 8.3 K colder than land in
  all-land groups. The two sets differ in latitude too, so the 8.3 K is
  not all the group's effect.
- **Even land in all-land groups receives 3.0 W/m²,** an eighth of
  reference land's 23.3 W/m². So the groups are not the whole cause:
  climate mode's humidity transport also converges little water over
  land where it can.

## The water reaches land only through the eddies (2026-10-23)

Climate mode, L4, seed 1, ten-year spin-up, then three years (36 months,
all with moist eddies;
[`tools/v8/land_moisture_split_probe.cpp`](../../tools/v8/land_moisture_split_probe.cpp),
7.8 s; log `build/inv04/land_moisture_split.log`). The surface step now
also reports each cell's group's E (`cell_eddy_latent_transport_W_m2`,
ADR-0024 §4.3: the latent heat of the water the humidity transport's
eddies moved); the rest of the group's delivery is the mean flow's.
Group values, weighted by the land in each group (by the ocean for
all-ocean groups):

| Groups | Total (W/m²) | Eddies | Mean flow |
|---|---:|---:|---:|
| All land (≥ 95 %) | 3.01 | 3.00 | 0.01 |
| Mixed | −0.21 | −0.54 | 0.33 |
| All land groups together | 1.49 | 1.33 | 0.16 |
| All ocean | −0.92 | −0.83 | −0.08 |

| Group latitude | Share of land | Total | Eddies | Mean flow |
|---|---:|---:|---:|---:|
| 90–60° S | 0.03 | 5.25 | 4.58 | 0.66 |
| 60–30° S | 0.19 | 1.56 | 1.65 | −0.09 |
| 30–0° S | 0.34 | 0.20 | 0.76 | −0.56 |
| 0–30° N | 0.21 | 0.06 | −1.20 | 1.26 |
| 30–60° N | 0.12 | 1.85 | 1.64 | 0.21 |
| 60–90° N | 0.12 | 6.31 | 5.79 | 0.52 |

- **The mean flow brings land almost no water** (0.16 W/m²): nine tenths
  of the little that arrives is the eddies'. The reason is by design:
  since ADR-0011 §17.6 (2026-10-06) the transport's layer mass fluxes are
  the zonal-mean overturning's alone; the azonal flow was withdrawn
  because, without a thermodynamic equation, its divergent part was
  unbounded and the coupled solve had no solution. The humidity transport
  uses the same fluxes (ADR-0021 §4.5). A zonally uniform overturning
  cannot converge water onto land rather than onto the ocean at the
  same latitude, so the mean flow's water goes to land and ocean alike,
  and in the tropics it nearly cancels (−0.56 and +1.26 W/m²).
- **The eddies are weak over land.** They diffuse the group humidity
  down its gradient, so they bring water only where land air is already
  drier than the ocean's, and in the northern tropics (0–30° N) they
  take it away (−1.20 W/m²).
- Reference mode's 23.3 W/m² therefore comes from flow climate mode does
  not have: azonal, monsoon-like convergence onto land, which its
  resolved dynamics carry. §17.6 named the cost as stationary eddies'
  heat transport; this is a second cost, the land's water, and through
  it the land's greenhouse.
- Read-only: the L3 state hash and the tests are unchanged.

## Sensitivity tests: more water for land does not warm it relative to the ocean (2026-10-23)

Two off-by-default knobs on the humidity transport only (not in any
preset or manifest): `SurfaceEnergyParameters::humidity_azonal_share`,
the share of the azonal balance's mass fluxes that also carries the
water (ADR-0011 §17.6 keeps them out of the heat), and
`humidity_eddy_factor`, a factor on the water's eddy exchange. Both act
through the spin-up. Climate mode, L4, ten-year spin-up, then three
years (`./build/land_moisture_split_probe <share> <factor>`, 8 s each;
log `build/inv04/humidity_sensitivity.log`). Ocean temperatures follow
from the mean and land at 0.29 land.

| Azonal share | Eddy factor | Mean (K) | Land (K) | Ocean (K) | Land − ocean (K) | Land air q (g/kg) | Latent to land (W/m²) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1 | 290.36 | 278.55 | 295.18 | −16.6 | 4.18 | 1.49 |
| 0.1 | 1 | 295.30 | 283.55 | 300.10 | −16.6 | 6.72 | 0.93 |
| 0.3 | 1 | 297.00 | 285.59 | 301.65 | −16.1 | 8.55 | 1.33 |
| 1 | 1 | 297.47 | 286.95 | 301.76 | −14.8 | 9.79 | 2.27 |
| 0 | 2 | 289.93 | 277.68 | 294.93 | −17.3 | 4.11 | 1.96 |
| 0 | 4 | 289.49 | 277.15 | 294.53 | −17.4 | 4.06 | 2.35 |
| 0 | 10 | 288.77 | 276.30 | 293.86 | −17.6 | 3.99 | 2.72 |
| Reference mode | | 291.30 | 287.78 | 292.73 | −4.9 | | 23.3 |

No fallback months in any run.

- **The azonal flow's water warms the whole planet,** by 5–7 K: its
  vertical fluxes lift vapour into the upper layers, a stronger
  greenhouse everywhere. Land air becomes more than twice as humid, yet
  land stays 15–17 K colder than the ocean (reference: 4.9 K). A refit of
  the radiation would undo the global warming and leave the contrast.
- **Stronger water eddies cool the planet slightly** and do not humidify
  land air: the eddies' conductance follows the zonal model's
  diffusivity, small in the tropics where 55 % of the land is, and at
  0–30° N they take water from land.
- **So the land's water is not the whole cause.** The land–ocean
  contrast of the air (8.5–23 K in climate mode, 0–5.5 K in reference
  mode, above) needs heat moved between land and ocean at the same
  latitude too. Climate mode has no such transport: its mean flow is the
  zonal-mean overturning (meridional only), its eddies act at the group
  level, and within a group land and ocean exchange nothing. Reference
  mode's zonal winds carry air across a continent in days.

## Sensitivity tests: the contrast sits inside the transport groups (2026-10-23)

Three more off-by-default knobs (INV-04 only, not in any preset or
manifest): `zonal_advection_share`, the zonal-mean zonal wind's layer
fluxes added to the transport's for heat and water (divergence-free by
construction: each layer's streamfunction Ψ_l(φ) = ∫ μ̂ ū_l R dφ' taken
at the coarse cells' corners, the outflow across an edge the difference
of its corners' Ψ); `humidity_zonal_share`, the same for the water only;
and `humidity_group_mixing`, the share of each cell's humidity mixed to
its group's after the transport (conserving the group's water). Same
probe and runs as above (`./build/land_moisture_split_probe <azonal
share> <eddy factor> <zonal> <zonal water> <group mixing>`, 8–10 s
each). The probe now also reports the bottom-layer air of land cells
(land fraction above 0.5) minus ocean cells within each mixed group, and
of all-land minus all-ocean groups in the same 10° band.

| Zonal (heat and water) | Zonal (water) | Group mixing (water) | Mean (K) | Land (K) | Land − ocean (K) | Air within mixed groups (K) | Air between groups (K) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 290.36 | 278.55 | −16.6 | −30.2 | −10.7 |
| 0.1 | 0 | 0 | 291.26 | 280.77 | −14.8 | −29.0 | −8.4 |
| 1 | 0 | 0 | 292.63 | 283.25 | −13.2 | −25.0 | −7.2 |
| 0 | 1 | 0 | 290.04 | 278.33 | −16.5 | | |
| 0 | 0 | 1 | 289.87 | 279.56 | −14.5 | −21.1 | −10.5 |
| 1 | 0 | 1 | 292.54 | 284.67 | −11.1 | −16.2 | −7.0 |
| Reference mode | | | 291.30 | 287.78 | −4.9 | | |

No fallback months in any run.

- **Most of the contrast is inside the transport groups.** Within a mixed
  group, land cells' bottom air is 30 K colder than its ocean cells', a
  few hundred kilometres apart. Between all-land and all-ocean groups at
  the same latitude it is 10.7 K. The transport applies one heat source
  per m² to all of a group's cells (ADR-0009 §12) and scales each cell's
  humidity by its group's change, so each fine cell's air stays in
  balance with its own surface: nothing mixes air between neighbouring
  cells of a group.
- **The zonal wind's advection helps between groups** (−10.7 to −7.2 K;
  land − ocean −16.6 to −13.2 K) and warms the planet 2.3 K. It solves
  without fallback months.
- **Mixing the water within groups** brings the within-group air
  contrast from 30 to 21 K. With the zonal advection as well, land is
  11.1 K colder than the ocean (reference 4.9 K).
- **The rest needs the heat mixed within groups too:** each cell's share
  of its group's source would depend on its air, not only on its area.
  That changes the transport's distribution of H (ADR-0009 §12) and the
  coupled solve's response, so it needs a decision before a prototype.

## Heat mixed within the transport groups (2026-10-23)

A prototype, off by default (`SurfaceEnergyParameters::heat_group_mixing`,
m; coupled months only). A cell's source is its group's H plus a shift δ,
held fixed through each coupled solve and updated between three solves:
δ ← δ + λ m (θ* − θ_c) / s_c, with θ_c and s_c = dθ_c/dh the cell's
column at H + δ (θ_c or, with moist eddies, T_m) and
θ* = Σ A θ_c / s_c / Σ A / s_c, so Σ A δ = 0 and energy is unchanged.
λ ≤ 1 keeps every cell's source at least half way above its own floor
(its bottom layer at 100 K). A pass whose solve ends with a larger
consistency residual than the one before is undone. At m = 1 a group's
cells share one θ_c to first order.

Two first versions failed and are not kept: recomputing δ inside every
Newton response (24 Newton iterations a month, many months at the cap
with residuals of 2–19 W/m²), and the fixed-point without the floor and
undo (one month's solve diverged to 28 771 W/m² with the zonal advection
on).

Climate mode, L4, ten-year spin-up, then three years, as V8's climate
half (the baseline reproduces it: 290.361 K, 1.610 m/yr). Reference mode
does not use this transport and V8 spins each mode up on its own, so its
291.295 K and 1.451 m/yr are unchanged; the V8 columns use them. Log
`build/inv04/group_mixing_sensitivity.log`.

| Zonal | Water mixing | Heat mixing | Mean (K) | Land (K) | Land − ocean (K) | Air within mixed groups (K) | Air between groups (K) | V8 gap (K) | Precipitation ratio | Newton (36 months) | Wall (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 290.361 | 278.55 | −16.6 | −30.2 | −10.7 | 0.934 | 0.901 | 132 | 7.7 |
| 0 | 0 | 0.5 | 290.927 | 284.39 | −9.2 | −1.3 | −9.6 | 0.368 | 0.901 | 141 | 13.6 |
| 0 | 0 | 1 | 290.819 | 284.50 | −8.9 | −0.1 | −9.5 | 0.476 | 0.902 | 147 | 12.9 |
| 0 | 1 | 1 | 290.906 | 284.53 | −9.0 | −0.5 | −9.5 | 0.389 | 0.905 | 151 | 12.0 |
| 1 | 0 | 1 | 293.923 | 289.34 | −6.5 | −0.5 | −6.6 | −2.628 | 0.890 | 299 | 38.5 |
| 1 | 1 | 1 | 293.959 | 289.39 | −6.4 | −0.7 | −6.6 | −2.664 | 0.888 | 306 | 39.9 |
| Reference mode | | | 291.295 | 287.78 | −4.9 | | | | | | |

Every month converges below 1e-4 W/m²; no fallback months; energy and
water gates pass in every measured month.

- **Mixing the heat within groups removes the within-group contrast**
  (−30 to −0.1 K) and halves land's deficit against the ocean (−16.6 to
  −8.9 K). The global mean hardly moves (+0.5 K), so **V8's gap falls
  from 0.93 K to 0.37–0.48 K without a refit,** near its 0.3 K limit.
- **The remaining land–ocean contrast is between groups** (−9.5 K);
  the zonal wind's advection takes it to −6.6 K, but warms the planet
  3 K, which would need a refit, and costs three times the Newton
  iterations.
- **The precipitation ratio does not move** (0.89–0.91): it is the
  global evaporation, which the radiation sets, not where the water
  goes.
- **Cost at L4:** 1.7 times the baseline's wall time (two more coupled
  solves and a column pass each month). L5 and L6 are measured below.

### At L5 and L6 over 250 years: cost, equilibrium, determinism (2026-10-23)

V11's performance runs, climate mode, 250 years (247-year spin-up, then
36 measured months), 16 workers, the probe with heat mixing m = 1 (log
`build/inv04/group_mixing_performance.log`). The probe does not measure
V11's column-convergence gates. INV-03's L6 baseline (1 061.1 s) was not
rerun; L5's baseline here (293.4 s) is close to INV-03's 308.3 s.

| Level | Heat mixing | Mean (K) | Land (K) | Ocean (K) | Land − ocean (K) | Air within mixed groups (K) | Air between groups (K) | Precipitation (m/yr) | Newton (36 months) | Fallback months | Wall (s) | Budget (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 0 | 288.381 | 279.85 | 291.87 | −12.0 | −26.6 | −8.3 | 1.564 | 147 | 0 | 293.4 | 607.5 |
| 5 | 1 | 285.213 | 279.07 | 287.72 | −8.7 | +0.1 | −8.0 | 1.446 | 177 | 0 | **804.7** | 607.5 |
| 6 | 1 | 285.254 | 279.38 | 287.65 | −8.3 | +0.1 | −7.0 | 1.442 | 225 | 0 | **2 927.0** | 2 025 |

(Ocean is from the mean and land with a 0.29 land share, as in the
probe's land − ocean.) Energy and water gates pass in every measured
month.

- **Both performance runs fail their budgets:** 2.7 times L5's baseline
  and 2.8 times INV-03's L6 baseline, against 1.7 times over L4's three
  years (whose fixed setup cost dilutes the ratio). The extra cost is in
  the two extra column passes, not the transport solve (7.6 s of 805 s
  at L5's measured months).
- **In equilibrium the mixing cools the planet 3.2 K** (L5: 288.38 to
  285.21 K), almost all of it the ocean (−4.1 K); land cools 0.8 K. At
  L4 after ten years it warmed 0.5 K: the deep ocean had not yet
  responded. Warmer land air radiates more of the ocean's heat to space.
  V3's L4 fit would move by about 3 K, so a refit would be needed, against
  ADR-0027 §5's "without a refit".
- **The within-group contrast is gone at every level** (+0.1 K), and the
  between-group contrast shrinks with the group size (−8.0 K at L5,
  −7.0 K at L6).
- **V5 passes:** at L4 (ten-year spin-up, three years), one and four
  workers give the same final state hash, `755f00c1b18534a6`, and the
  same means (log `build/inv04/group_mixing_determinism.log`; the probe
  now takes `PROBE_WORKERS` and prints the hash).

### One update a month (2026-10-23)

The prototype now does two solves, not three, with δ updated once
between them, and without its own column pass: a converged solve's last
response is at its last iterate, so its cells' θ_c and s_c are reused
(the columns are solved again only after an unconverged solve). Off,
it is bit-identical (L4: 290.361 K, as before). Logs
`build/inv04/group_mixing_one_update_performance.log`.

| Level | Mean (K) | Land − ocean (K) | Air within mixed groups (K) | Air between groups (K) | Precipitation (m/yr) | Fallback months | Wall (s) | Budget (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 (ten years, then three) | 290.835 | −9.0 | −0.4 | −9.6 | 1.609 | 0 | 10.2 | |
| 5 (250 years) | 285.363 | −8.7 | −0.1 | −8.0 | 1.451 | 0 | 528.1 | 607.5 |
| 6 (250 years) | 285.376 | −8.3 | −0.1 | −7.0 | 1.448 | 0 | **2 200.9** | 2 025 |

- One update does what three solves did: the within-group contrast is
  −0.1 to −0.4 K, and V8's L4 gap would be 0.460 K.
- **L5 passes its budget** (1.8 times its baseline); **L6 fails by 9 %**
  (2.07 times INV-03's 1 061.1 s baseline, not rerun). The second solve
  starts again from the floor, as every coupled solve does.
- The equilibrium still cools 3 K (L5: 285.36 K against 288.38 K).

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

## Decision in progress (2026-10-22)

With the user: V8 is measured at L4 over three years (ADR-0023 §16,
accepted 2026-10-22). Its baseline (above) is 0.936 K and a 0.901
precipitation ratio. Next: the land–ocean split at L4 (reference land
9.2 K warmer, ocean 2.4 K colder).

**Update (2026-10-23).** Done: the land–ocean split. Climate mode's land
is cold at every latitude because its air is cold and dry. Its transport
delivers 1.55 W/m² of latent heat to land against reference mode's
23.3 W/m², and the cloud longwave over land is 19.5 against 40.2 W/m².
Land that shares a transport group with ocean (47 % of it) receives no
water. Next, if the user agrees: split climate mode's water delivery to
land into the circulation's mean flow and its eddies, then run cheap
climate-only sensitivity tests (off by default, as INV-03's prototypes
were) before proposing an ADR.

## Open tasks (updated 2026-10-23)

**Done since the first list (2026-10-22):**
- the 20-year spin-up: the gap grows to 4.719 K, as climate mode keeps
  cooling;
- the 30-piece case: an oscillation of the lagged cloud longwave at steps
  of 6 hours to 2 days, not a timestep trend;
- the heat-only comparison: the diffusive heat transport alone gives
  2.85 K of the 3.03 K;
- ADR-0025's options re-measured on the moistening: they stay off;
- V8 moved to L4 over three years (ADR-0023 §16): 0.936 K, with a 0.901
  precipitation ratio.

**Done on 2026-10-23:**
- the land–ocean split at L4, with the land budget and sub-monthly melt
  (commit `c21d957`): climate land is cold at every latitude; three
  quarters of reference land's extra heat comes from its air; sub-monthly
  melt is real but a quarter of the effect;
- the air and transport over land and ocean (commit `1c94d89`): climate
  mode's land air is dry, because its transport brings almost no water
  to land (1.55 against 23.3 W/m²); coastal land in mixed transport
  groups receives none.

**V8 (this investigation), in order:**
1. ~~**Which part of the climate transport fails to bring water to
   land.**~~ Done (above): the mean flow brings almost none, because
   since ADR-0011 §17.6 it is the zonal-mean overturning alone; the
   eddies bring nine tenths of the 1.5 W/m².
2. **Climate-only sensitivity tests** (seconds each at L4; off by
   default): ~~a stronger humidity transport onto land~~ (done above: it
   does not close the land–ocean contrast); ~~heat and water moved
   along latitude circles, and water mixed within a group~~ (done above:
   together −16.6 to −11.1 K of reference mode's −4.9 K); ~~heat mixed
   within a transport group~~ (prototype above, chosen with the user:
   V8's gap 0.37–0.48 K without a refit); ~~its cost at L5 and L6~~
   (done above: 2.7–2.8 times the baselines, over both budgets, and
   −3.2 K in equilibrium); a cheaper form, one update a month (done
   above: L5 within budget, L6 9 % over); next, the second solve started
   from the first's sources. The target is reference
   land's numbers: about 23 W/m² of latent heat delivered, about 12 g/kg
   of equatorial humidity, and the cloud longwave about 40 W/m².
3. **An ADR proposal** for whichever change works, then a refit and V8
   again (about 3 h at L4).
4. **The precipitation ratio, 0.901,** at the edge of its ±10 % limit:
   expected to move with the land's water.
5. **The drift (ADR-0023 §16.3):** both modes are still cooling with the
   deep ocean after ten years. If it decides the gap, the option is a
   shared, spun-up deep ocean as the start, by amending ADR-0023 §15.
6. **L4's convergence is unmeasured:** L5's reference mode costs about
   four times L4's.

**Model, found on the way:**
7. **The cloud longwave oscillates** at steps of 6 hours to 2 days: the
   radiation holds the step's starting cloud water. Neither production
   step is in that window, but a weather or subcycled step would be.
8. **Snow on land grows without bound** (about 4.4e14 kg a year at L3
   after 150 years): nothing removes it. A sink, such as ice-sheet flow
   or a cap, needs a decision; it is probably outside P0.
9. **L6's coupled transport:** a solver that handles the kinks in the
   columns' response (ADR-0024 §9.5) would make the fallback rarer.
10. **The L4 fit's fragility:** it passes because its cold start has no
    fallback month. If a refit brings one back, the option is a fallback
    triggered by the columns failing (INV-03), by amendment.
11. **`PlanetRun` has no resume constructor,** and PRUN replay cannot
    start from a snapshot (INV-03).
12. **M8's known limitations** (ADR-0025 §14): §4.4's zonal cloud
    targets (V5) and V7's longwave difference.

**Decisions waiting:**
13. **ADR-0020** (the planet's read contract) is proposed. It must not
    be implemented until it is accepted.
14. **M9** starts only on explicit request.

**Housekeeping:**
15. `git stash`: both entries can be dropped. `stash@{0}` is the unused
    continuous fallback blend; `stash@{1}` holds only Godot's import
    changes to `godot/project.godot`.
16. Rebuild `build` and `build-godot` (Release) after every pull.
17. **Long runs:** an L4 V8 run takes about 2.5–3 hours, longer than a
    tool's 2-hour background limit, so start it detached (`setsid
    nohup … &`) and write its log under `build/inv04/`.
