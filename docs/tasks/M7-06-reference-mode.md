# Task M7-06 — Reference mode

- **Milestone:** P0 / M7 (sixth task)
- **Status:** complete (2026-10-09)
- **Governing decisions:** ADR-0021 §4.5 (reference mode), V2, V3, V6;
  ADR-0011 §4.3; ADR-0002 §4.2

## Done

- **The vapour in the primitive-equation core**
  (`PrimitiveEquationState::mass_humidity`, Q_k = μ_k q_k; empty means no
  tracer):
  - flux form, with the same edge mass fluxes F_k and vertical fluxes W as
    Θ;
  - upwind q on the edges and interfaces, the limited, positive form of
    ADR-0002 §4.2;
  - in each RK3 stage, with no flux through the surface or the model top.
- **The reference-mode driver** (`AtmosphereDynamics`,
  `AtmosphereDynamicsParameters::advect_humidity`, set from the
  scenario's `water_cycle`):
  - hands μq to the core and writes q = Q/μ back with the new p_s;
  - clips the RK3's negative undershoots and reports the water that adds
    (`AtmosphereDynamicsDiagnostics::clipped_kg`), with the vapour before
    and after;
  - writes the resolved winds reconstructed at the cells (Perot) into the
    derived `atmosphere_eastward_wind_m_s` and
    `atmosphere_northward_wind_m_s`;
  - the evaporation's bulk wind now uses those fields whenever they are
    sized, so in reference mode it takes the resolved bottom-layer wind.
- **The column physics** already ran every reference step: the surface
  step evaporates and rains out, with the vapour path. With no circulation
  it moves no humidity itself.

## Results

- **V6 in the core** (`tests/physics/test_primitive_equations.cpp`;
  random states, 3 km terrain, 40 m/s random winds, a day of steps;
  N = 1, 3, 5):
  - a uniform q stays uniform to 1e-15;
  - a varying q keeps its total to 1e-15;
  - no value goes negative.
- **Reference mode** (`tests/physics/test_reference_water.cpp`, L3, ten
  days):
  - every step's V3 energy within 0.22 of its gate, and the water cycle
    within 8e-6;
  - the advection conserves the vapour to 7e-15, with nothing clipped;
  - 1 and 4 workers are bit-identical;
  - from the initial 60 % humidity, the ten days evaporate 5.5 m/yr and
    rain 3.2 m/yr while the air fills to 155 kg/m² of vapour. These are
    spin-up rates; parity with climate mode (V9) is M7-07's.
- All 89 tests pass.

## Open for later tasks

- V9 parity (climate against reference: mean temperature ± 0.3 K,
  precipitation ± 10 %) after the refit (M7-07).
- A reference period that starts from the balanced circulation
  redistributes p_s, which changes the vapour mass at fixed q. That is a
  one-off at the mode switch, to be measured with V9.
