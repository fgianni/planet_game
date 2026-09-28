# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_3.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v0_8.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
`docs/DEVELOPMENT_SPEC_v0_2.md` and `docs/DEVELOPMENT_SPEC.md` are superseded
and not authoritative; design versions v0.3--v0.7 are kept for history.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M1 --- Orbit, sun, day/night and
seasons**. M2 is in progress: gate G2-M2 (finite-volume operators, ADR-0002
V3/V4) is complete, and ADR-0005 (fractional coastlines, cell-mesh
drainage) is accepted. The ADR-0001 state-partition and ADR-0003 persistent
snapshot foundation are complete. Next: the specification §26 hardening
(`docs/tasks/M2-01b-foundation-hardening.md`), then terrain (M2-02). The
simulation-mode scheduler has not started.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M3 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
