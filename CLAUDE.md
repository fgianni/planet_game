# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC.md` before making changes.

That file defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Current milestone: **P0 / M0 --- Icosphere and simulation skeleton**.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M1 until M0 acceptance criteria
are satisfied.

When starting work, first audit the repository and propose the concrete
M0 implementation plan against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
