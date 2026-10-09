# Task M7-03 — Saturation rainout

- **Milestone:** P0 / M7 (third task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.3–4.4, V2–V4, amended in §10
  (2026-10-09, with the user); ADR-0008 §4.3; ADR-0010 §3.3 B, §4.4

## Done

- **Rainout in the column solve** (`solve_atmosphere_column`,
  `sim/planet/atmosphere/atmosphere_column.hpp`, with a `ColumnMoisture`):
  - each layer condenses C_k = max(0, q*_k − q_sat(T_k, p_k)) at its new
    temperature and gains L m C_k / Δt, implicitly in the Newton solve;
  - the bottom layer's q* includes the surface's evaporation E Δt / m;
  - the Jacobian has the condensation's dependence on T_k and on the
    surface, which now returns E with its derivatives in the downward
    longwave and the air temperature (`SurfaceExchange`);
  - the result has every layer's humidity, its condensate, the
    precipitation and the latent heat;
  - without moisture the solve is the dry one bit for bit.
- **Saturation after convection** (ADR-0021 §10.3): after the convective
  adjustment, each supersaturated layer condenses isobarically, alternating
  with the adjustment until nothing more condenses.
- **The surface air's humidity** (ADR-0021 §10.1):
  - the tiles evaporate against q_a = q₀ q_sat(A, p_s) / q_sat(T₀, p₀),
    updated in every solve with the column's surface air temperature A;
  - where the layer rains, q_a is q_cap = q_sat(A, p_s), with the undamped
    transfer;
  - each tile takes the larger of the two demands, which is exact for tiles
    sharing q_sat (`SurfaceAir`, `ColumnSystem::cap_humidity`, `cap_ratio`);
  - the tiles return the flux's slopes in their source, q_a and q_cap for
    the column's Jacobian. They match finite differences to 1e-5.
- **Precipitation** (`step_surface_energy` with `water_cycle`) replaces the
  prescribed field and reaches the surface at the end of the step:
  - snow on a land tile that starts frozen, whose condensate also releases
    L_f in the column (§10.2);
  - otherwise rain into the bucket;
  - on the ocean, it is the ocean's;
  - `precipitation_kg_m2_s` records it.
- **Floes that sublimate more than the ice left:** the excess vapour comes
  from the water, with its latent heat kept booked. One cell's energy had
  failed by 2e3 gates.
- **Budgets:**
  - `condensation_latent_J` is in `closure_residual_J`;
  - `water_cycle_residual_kg` books Δ(vapour + bucket + snow + ice)
    against the ocean's evaporation, the precipitation on it, runoff and
    the ice's growth and melt;
  - `precipitation_kg` and `ocean_precipitation_kg` are reported.

## Results

- **V4** (`tests/unit/test_rainout.cpp`):
  - condensing layers end at saturation to 2e-16;
  - the column's water closes to 3e-16 and its energy to 1e-13;
  - solves take at most 5 Newton iterations;
  - a dry column over an evaporating surface saturates in its first month,
    and after 20 years rains out exactly what evaporates (P/E = 1 to 1e-6),
    never above saturation;
  - with no condensate and no evaporation, the solve is bit-identical to
    the dry one.
- **Tiles** (`tests/unit/test_evaporation.cpp` §7): the raining branch
  never evaporates less than the unsaturated one, and the vapour slopes
  match finite differences.
- **Planet** (`tests/physics/test_water_cycle.cpp`, L3, earth_like, three
  layers, one year from the M6 climate):
  - V3 energy within 0.29 of its gate. That is one unconverged column in
    month 1, finished from its fluxes with a 2e-7 K correction; every other
    month is within 1e-3.
  - V2 water within 9e-5 and the whole cycle within 2e-4 of their gates.
  - Latent flux 127 W/m², evaporation 1.44 m/yr. The climate runs 5 K warm
    before the refit; Earth is about 85 W/m².
  - Months 2–12 rain 1.04 of what they evaporate.
  - Bottom layer at 0.998 mean relative humidity, none above 1.
  - 1 and 4 workers are bit-identical.
- **The default is unchanged:** with `water_cycle` off, all 85 tests pass.

## Open for later tasks

- Vapour reaches the upper layers only through the convective adjustment's
  heat, not as vapour. Humidity transport is M7-04, moist convection M8.
- The water cycle stays off in the presets until the refit (M7-07), which
  also sets the gustiness.
- The reference mode's column heating (`compute_atmosphere_heating`) is
  still dry (M7-06).
