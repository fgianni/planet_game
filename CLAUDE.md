# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC.md` before making changes.

That file defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Current milestone: **P0 / M1 --- Orbit, sun, day/night and seasons**.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M2 until M1 acceptance criteria
are satisfied.

When starting work, first audit the repository and propose the concrete
M1 implementation plan against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
