# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_4.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v0_9.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
`docs/DEVELOPMENT_SPEC.md`, v0.2 and v0.3 are superseded and not
authoritative. Design versions v0.7 and v0.8 are retained for history.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M1 --- Orbit, sun, day/night and
seasons**. M2 is in progress: gate G2-M2 (finite-volume operators, ADR-0002
V3/V4) is complete, and ADR-0005 (fractional coastlines, cell-mesh
drainage) is accepted. The state-partition, persistent-snapshot, and
pre-terrain foundation hardening are complete, and so is task
`docs/tasks/M2-02-plates-terrain-and-sea-level.md` (plates, terrain,
hypsometry and sea level). The next task is M2-03, drainage (ADR-0005 §4.2,
V5--V8); its task document has not been written yet. The simulation-mode
scheduler has not started.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M3 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
