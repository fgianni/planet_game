# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_8.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v1_3.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
Only the current specification and design record are kept in the tree;
earlier versions are in git history. Accepted ADRs and completed task
documents keep the version citations they were written against.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M5 --- Atmosphere and pressure**
(ADR-0010, amended §11; ADR-0008 §10): M5-01 (the ordered snapshot
migration chain), M5-02 (atmosphere state, hydrostatics, PSNAP schema 5),
M5-03 (column radiation and convection, sea-ice floes and leads) and M5-04
(`docs/tasks/M5-04-calibration-and-close.md`: refit, the Earth-like preset
on three layers, plateau experiment, performance gates).
M0--M4 (mesh, orbit, terrain, drainage, scheduler and calendar, surface
energy columns, replay, snow and sea ice, diffusive transport, snapshot
history) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M7 unless explicitly requested.

Current milestone: **P0 / M6 --- Wind and Coriolis** (ADR-0011, accepted
2026-10-02; amended §12, §13). M6-01 (C-grid operators), M6-02
(shallow-water core) and M6-03 (primitive equations and the winds in
reference mode, `docs/tasks/M6-03-primitive-equations.md`) are complete;
next is M6-04 (climate-mode balanced circulation).

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
