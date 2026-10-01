# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_4.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v0_9.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
Only the current specification and design record are kept in the tree;
earlier versions are in git history. Accepted ADRs and completed task
documents keep the version citations they were written against.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M4 --- Basic snow, ice and
albedo feedback** (ADR-0008, ADR-0009): M4-01 (cryosphere state, land snow,
PSNAP schema 4), M4-02 (diffusive heat transport), M4-03 (sea ice), M4-04
(seasonal experiment, climatology, refit) and M4-05
(`docs/tasks/M4-05-history-deltas-and-compression.md`: ADR-0003 snapshot
history with zstd compression, delta chains and forks, V6--V7).
M0--M3 (mesh, orbit, terrain, drainage, scheduler and calendar, sub-step
insolation, surface energy columns, replay) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M5 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
