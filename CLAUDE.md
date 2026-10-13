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

Most recently completed milestone: **P0 / M7 --- Humidity and evaporation**
(ADR-0021, amended §10--§13): M7-01 (water state, saturation, PSNAP schema
6), M7-02 (evaporation and the bucket), M7-03 (saturation rainout, the
surface air's humidity), M7-04 (humidity transport in climate mode), M7-05
(the vapour path), M7-06 (reference mode) and M7-07
(`docs/tasks/M7-07-refit-and-close.md`: the refit, the water cycle on in
the preset, τ_c, the gates; V9 parity recorded failing, deferred to M8).
M0--M6 (mesh, orbit, terrain, drainage, scheduler and calendar, surface
energy columns, replay, snow and sea ice, diffusive transport, snapshot
history, the layered atmosphere, winds and the climate circulation) are
complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M8 unless explicitly requested.

Current milestone: none in progress. M8 (clouds and precipitation) is
next but must not start unless explicitly requested; its first gates
include ADR-0021's V9 parity and the 42 K equator-to-pole target.

The rendering track (specification §31.10) runs in parallel with the P0
milestones. Its first task is
[`docs/tasks/R1-01-presentation-library-and-channels.md`](docs/tasks/R1-01-presentation-library-and-channels.md),
under ADR-0018 (accepted 2026-10-04). It may proceed alongside the P0 milestones, but
must never change a solver, a field's meaning or the PSNAP format. Its next
task is [`docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md`](docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md),
under ADR-0022 (accepted 2026-10-13), which starts with its precision spike
(step 0).

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
