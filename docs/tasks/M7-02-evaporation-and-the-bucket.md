# Task M7-02 — Evaporation and the bucket

- **Milestone:** P0 / M7 (second task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.3 (accepted 2026-10-09), V2, V3, V5;
  ADR-0007 V2; ADR-0008 §4.3–4.4 and §10

## Done

- **The column's surface equation** (`ColumnSystem`,
  `sim/planet/surface/column_step.{hpp,cpp}`) gains the latent flux
  L E(T_s) of four transfers ρ C_E V_e:
  - open water and leads (L_v);
  - floes (L_s);
  - snow (L_s);
  - the bucket (L_v), with β = min(1, W / 0.75 W_max).

  Each is implicit in T_s through q_sat(T_s), with its slope in the Newton
  solve. Each store's take is its backward-Euler limit, so a month never
  removes more than it holds:
  - snow: κ S / (S + κ Δt);
  - bucket: κ min(1, W / (W_c + κ Δt));
  - dew and frost: κ V / (V − κ Δt), never more than the air's vapour V.

  With no transfer, the system and its solve are ADR-0007's bit for bit.
- **The evaporating solve:** it starts from the dry root and runs a
  bracketed Newton. It stops when the residual is at the rounding level of
  its terms (64 ε). The step-size stop alone stalled one bit above its
  tolerance on large ocean balances.
- **Land** (`prepare_land_tile`, `solve_land_tile`): snow sublimates; the
  bucket, with this step's rain in it, evaporates. The melting-point clamp
  holds only while melt plus sublimation leave snow; otherwise the snow
  melts out and the bucket takes over.
- **Ocean** (`prepare_ocean_tile`, `solve_ocean_tile`):
  - open water and the leads evaporate at T_f;
  - the floes sublimate at their surface temperature, through the floe
    balance's slope;
  - sublimation comes out of the ice;
  - when the ice melts away, the floes' vapour comes from the water, and
    their latent heat stays booked. Dropping it broke the planet's energy
    closure by up to 1e2 gates in months where ice melted out.
- **The surface step** (`step_surface_energy`, behind
  `SurfaceEnergyParameters::water_cycle`, off by default; it needs a layered
  atmosphere):
  - per cell, ρ_s = p_s / (R_d A) at the surface air temperature;
  - V_e = |V_bottom| + 5 m/s, from the circulation's bottom-layer winds
    when they are available;
  - C_E = 1.5e-3 over the ocean and 4e-3 over land (ADR-0011 §4.3);
  - q_0 is the bottom layer's humidity, with m_0 = p_s / (g N).

  The water update:
  - the bucket gains rain and meltwater and loses its evaporation;
  - the bucket overflows above W_max to `runoff_kg_m2_s`;
  - q_0 gains the cell's evaporation, E Δt / m_0;
  - `evaporation_kg_m2_s` records the step's mean.
- **q_0 responds within the step.** The tiles' transfers are divided by
  1 + Δt Σ f τ / m_0, which is backward Euler in q_0 for a common q_sat. A
  month's τ Δt / m_0 is about 15. With q_0 held, the layer overshot
  saturation and dewed back the next month, giving latent fluxes of about
  ±60 W/m² in alternate months. ADR-0021 leaves this numerics open;
  M7-03's rainout and M7-04's transport act on q_0 after it.
- **Diagnostics** (`SurfaceEnergyDiagnostics`): the following, all in kg
  over the step:
  - water evaporation;
  - bucket evaporation;
  - snow sublimation and ice sublimation;
  - `bucket_runoff_kg`;
  - the vapour and bucket changes and stocks.

  The latent heat of evaporation, `evaporation_latent_J`, is in
  `closure_residual_J`. `water_residual_kg` now books sublimation, and
  `water_cycle_residual_kg` and `water_cycle_gate_kg` add the vapour and
  the bucket (V2).

## Results

- **Tiles** (`tests/unit/test_evaporation.cpp`):
  - Open water at 70% humidity: 138 W/m² latent, 1.2 K cooler. The slope
    matches the finite-difference response to 1e-3.
  - Dew warms the surface and never exceeds the air's vapour.
  - Neither the bucket nor the snow ever gives more than it holds.
  - Energy budgets close to 1.5e-15 on land and 4e-14 on the ocean, over
    snow 0–400 kg/m², buckets 0–150 and ice 0–2000, through free, clamped
    and melt-out branches.
  - With no transfer, the result is bit-identical to before.
  - **V5:** 24 monthly steps of a wet dry-soil tile never change the sign
    of ΔT.
- **Planet** (`tests/physics/test_water_cycle.cpp`, L3, earth_like, three
  layers, 0.8 m/yr prescribed precipitation, one year):
  - V3 energy within 9e-5 of its gate.
  - V2 water (snow and ice, with sublimation) within 4e-5 of its gate.
  - The whole cycle (vapour, bucket, snow, ice) within 3e-5 of its gate.
  - The first month, from 60% humidity, evaporates 60 W/m².
  - With no rainout yet, the bottom layer then fills to 1.12 of the
    surface's saturation, and the year averages 2.9 W/m².
  - The bucket stays in [0, W_max] and the humidity stays non-negative.
  - 1 and 4 workers are bit-identical.
- **The default is unchanged:** with `water_cycle` off, all 84 tests pass,
  including the replay, golden and gate tests.

## Open for later tasks

- The bottom layer is supersaturated at its own temperature (4.6× in the
  area mean after a year) until M7-03's rainout caps it.
- Prescribed precipitation still feeds the bucket and the snow. M7-03
  replaces it with the column's, and fills `precipitation_kg_m2_s`.
- The water cycle is off in every preset and run until M7-03. The refit
  (§4.8) is M7-07's.
