# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_2.md` before making changes. It is the
development specification. The design document is
`docs/planetary_civilization_simulator_design_v0_4.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002: the mesh is the
hexagonal--pentagonal dual, not the triangular cells described in the
spec's section 5). `docs/DEVELOPMENT_SPEC.md` is a superseded
reconciliation draft and is not authoritative.

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M1 --- Orbit, sun, day/night and
seasons**.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M2 unless explicitly requested.

When starting work, first audit the repository and propose the concrete
M1 implementation plan against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
