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

Most recently completed milestone: **P0 / M3 --- Surface energy and first
thermal planet**: sub-step mean insolation (M3-01, ADR-0006), surface energy
columns with experiments A and B (M3-02, ADR-0007), and M3-03
(`docs/tasks/M3-03-replay-gates-and-close.md`): the ocean mixed layer in
`double` (ADR-0007 §10, PSNAP schema 3), run manifests, state hashes and
replay (ADR-0003 V1, V2, V5), and the ADR-0001 performance gate in CI.
M0--M2 (mesh, orbit, terrain, drainage, scheduler and calendar) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M4 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
