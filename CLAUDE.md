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

Most recently completed milestone: **P0 / M8 --- Clouds and precipitation**
(ADR-0023, amended §11--§15; ADR-0024; ADR-0025, amended §11--§14):
M8-01 (cloud water state, PSNAP schema 7), M8-02 (Betts--Miller moist
convection), M8-03 (cloud water to precipitation), M8-04 (cloud
radiation), M8-05 (`docs/tasks/M8-05-refit-and-gates.md`: the refit, V9
passing, V11 passing at L5 and L6), M8-06 (clouds on screen, V12), M8-07
(sub-grid condensation) and M8-09 (convective condensate and anvils), the
last two implemented and off in the preset. M8 closed on 2026-10-20 with
the global cloud field (ADR-0025 §14): the zonal cloud pattern, the
modes' cloud longwave difference and V8's +6.4 K temperature gap are
recorded as known limitations. M0--M7 (mesh, orbit, terrain, drainage,
scheduler and calendar, surface energy columns, replay, snow and sea ice,
diffusive transport, snapshot history, the layered atmosphere, winds and
the climate circulation, humidity and evaporation) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M9 unless explicitly requested.

Current milestone: none in progress. Investigation INV-01
(`docs/tasks/INV-01-reference-humidity-and-parity.md`) has its findings:
V8's gap was mostly climate mode carrying no water up, and ADR-0026's
option 1 (Betts--Miller moistening, accepted 2026-10-20; options 2 and 3
not taken for P0) halves it (+6.4 to +3.1 K before a refit). Next, not
started: the refit with `convective_moistening` on in the earth_like
preset, then V8 measured again, then a decision on M9. M9 must not start
unless explicitly requested.

The rendering track (specification §31.10) runs in parallel with the P0
milestones. Its first task is
[`docs/tasks/R1-01-presentation-library-and-channels.md`](docs/tasks/R1-01-presentation-library-and-channels.md),
under ADR-0018 (accepted 2026-10-04). It may proceed alongside the P0 milestones, but
must never change a solver, a field's meaning or the PSNAP format. Its most
recent task, [`docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md`](docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md)
(ADR-0022: the Earth-viewer camera and streamed terrain tiles), is complete
(2026-10-15).

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
