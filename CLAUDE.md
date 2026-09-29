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

Most recently completed milestone: **P0 / M2 --- Geological planet, terrain
and ocean basins**: gate G2-M2, state partitioning, persistent snapshots,
foundation hardening, plate-scale terrain, sub-cell hypsometry, sea level,
static cell-mesh drainage, and the simulation modes, scheduler skeleton and
sub-step calendar (`docs/tasks/M2-04-scheduler-and-calendar.md`). The pre-M3
seasonal decision record is accepted (ADR-0006); M3 has not started.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M3 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
