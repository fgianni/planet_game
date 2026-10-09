# Task M7-04 — Humidity transport in climate mode

- **Milestone:** P0 / M7 (fourth task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.5, V2, V3, V6, V7 (recorded),
  amended in §11 (2026-10-09); ADR-0011 §17.1, §17.4

## Done

- **The tracer solve** (`solve_implicit_tracer`,
  `sim/planet/surface/heat_transport.{hpp,cpp}`):
  - backward Euler for the layers' mixing ratios on a transport graph,
    using the climate circulation's fixed overturning fluxes, upwind;
  - the eddies' exchange G / (c_p N) per layer, with c_p now carried in
    `AdvectionDiffusion`;
  - the vertical mass fluxes that close each layer's divergence;
  - one stacked system, node-major, solved by BiCGSTAB with a block ILU(0)
    on whole columns;
  - the change applied is the flux form of the solution;
  - bit-identical for any worker count.
- **The climate step** (`step_surface_energy` with `water_cycle` and an
  active circulation) moves the humidity first, on the coarse groups
  (mass-weighted). Each cell's humidity then scales with its group's
  (split B, ADR-0021 §3.2). The month's `HumidityTransportDiagnostics`
  are in `SurfaceEnergyDiagnostics::humidity_transport`.
- **The scenario** gains `water_cycle`. The manifest records it only when
  on, so earlier manifests read as off.
- **Convergence with water.** At L5 the coupled heat transport hit its
  30-iteration cap every month, 30–160 W/m² from consistency, with
  unconverged columns corrected by up to 50 K. Four causes, all fixed:
  - **The column's linearisation** missed how the tiles' humidities q_a
    and q_cap follow the surface air temperature A. The tiles now return
    their fixed-temperature partials, and the cell maps a change of q onto
    each tile's surface equation as a source −∂(L E)/∂q δq, for the
    longwave, the sensible heat and the evaporation.
  - **The tile's Newton** crept from the steep side of the demand's kinks
    (the raining branch's R ≈ 15, dew), leaving residuals of 90 W/m².
    Inside a finite bracket, a step that does not halve the residual now
    bisects.
  - **Melt-out** stopped the sublimation, which made the response jump
    (ADR-0021 §11.3).
  - **L_s ≠ L_v + L_f** (§11.2) left the melt-out's booking 2e-3 W/m²
    off.
- After these, an L5 water month costs 0.57 s at 8 workers (a dry month
  0.05 s): 13 transport Newton iterations and about 45 tracer iterations.

## Results

- **V6** (`tests/unit/test_tracer_transport.cpp`, L3 agglomerated graph,
  random fluxes):
  - a uniform field stays uniform to 6e-16 under column-balanced fluxes;
  - mass is conserved to 3e-16 with or without divergence;
  - no value goes negative, and the solve takes 5 iterations;
  - 1 and 4 workers are bit-identical;
  - Earth-like eddies smooth a field.
- **Melt-out** (`test_evaporation.cpp` §8): across the boundary between
  held and melted out, the water flux moves by at most 7e-4 of its scale
  per 0.05 W/m² step.
- **Coupled climate** (`tests/physics/test_water_transport.cpp`, L3,
  three layers, second year):
  - every month moves its humidity, conserving it to 3e-15;
  - V3 energy within 4e-5 and the whole cycle within 1.4e-4 of their
    gates;
  - the heat transport converges (worst 9.8e-5 W/m²) and every column
    converges;
  - P/E = 1.012 over the year;
  - 1 and 4 workers are bit-identical.
- **V7, recorded before the refit:**
  - 1.62 m/yr (Earth 1.0), latent 127 W/m², mean 284.6 K;
  - zonal precipitation (m/yr, 10° bands from the South Pole): 0.22 0.18
    0.34 0.96 1.32 1.55 1.69 1.81 2.04 2.33 2.40 2.28 1.98 1.58 0.85 0.33
    0.31 0.18;
  - the maximum is at 10–20° N, within 15° of the equator;
  - no subtropical minima yet.
- **The default is unchanged:** with `water_cycle` off, all 88 tests pass.

## Open for later tasks

- The subtropical minima need the overturning's subsidence to dry the
  layers. Its coarse monthly upwind diffuses strongly; V7 is gated after
  the refit (M7-07).
- The water cycle's cost at L6 (V12) is M7-07's.
