# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_8.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v1_4.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
Design v1.4 §46 (systems architecture: interpretation layer, PopSim,
population knowledge, presentation, player levers) is not yet traced into
the specification; where it conflicts with the specification or an
accepted ADR, they win. Its PlanetSim part (§46.3) is reconciled with them
in the proposed ADR-0020 (`docs/decisions/0020-planet-read-contract.md`),
which is not yet accepted and must not be implemented until it is.
Only the current specification and design record are kept in the tree;
earlier versions are in git history. Accepted ADRs and completed task
documents keep the version citations they were written against.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M6 --- Wind and Coriolis**
(ADR-0011, amended §12--§17; ADR-0009 §13): M6-01 (C-grid operators),
M6-02 (shallow-water core), M6-03 (primitive equations, the winds in
reference mode), M6-04 (the climate-mode balanced circulation) and M6-05
(`docs/tasks/M6-05-transport-refit-and-close.md`: the circulation carries
the heat, the balanced p_s in the slow state, τ₀ refitted, the gates).
M0--M5 (mesh, orbit, terrain, drainage, scheduler and calendar, surface
energy columns, replay, snow and sea ice, diffusive transport, snapshot
history, the layered atmosphere) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M8 unless explicitly requested.

Current milestone: **P0 / M7 --- Humidity and evaporation** (ADR-0021,
accepted 2026-10-09: saturation rainout, the Manabe bucket). M7-01 (water
state, saturation, PSNAP schema 6), M7-02 (evaporation and the bucket) and
M7-03 (saturation rainout; ADR-0021 amended §10: the surface air's
humidity) and M7-04 (humidity transport in climate mode, §11), all behind
`SurfaceEnergyParameters::water_cycle` (scenario `water_cycle`), off by
default, are complete; next is M7-05 (vapour radiation). The 42 K equator-to-pole target waits for M7's latent and
M11's ocean transport (ADR-0011 §17.7).

The rendering track (specification §31.10) runs in parallel with the P0
milestones. Its first task is
[`docs/tasks/R1-01-presentation-library-and-channels.md`](docs/tasks/R1-01-presentation-library-and-channels.md),
under ADR-0018 (accepted 2026-10-04). It may proceed alongside the P0 milestones, but
must never change a solver, a field's meaning or the PSNAP format.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
