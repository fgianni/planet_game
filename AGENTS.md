# AGENTS.md

This repository is the Planetary Civilization Simulator.

Before changing code, read `docs/DEVELOPMENT_SPEC.md` in full. It is the
implementation contract and architecture source of truth.

## Current phase

P0 --- Living Planet.

Current milestone: **M0 --- Icosphere and simulation skeleton**.

Do not implement later milestones unless explicitly requested.

## Hard rules

-   C++20.
-   `PlanetSim` is standalone and must not depend on Godot.
-   Godot is presentation/input only and consumes snapshots/submits
    commands.
-   Core code must build and test headlessly.
-   Use SI units internally.
-   Prefer data-oriented structures and explicit physical fields/fluxes.
-   No arbitrary climate/gameplay modifiers when a physical state or
    flux can represent the interaction.
-   Tests and diagnostics are part of every implementation.
-   Do not silently change architecture to make implementation easier.
-   Record significant architecture choices in `docs/decisions/`.
-   Do not weaken failing tests without explaining the
    physical/numerical reason.

## Required workflow

1.  Inspect existing code.
2.  State the implementation plan.
3.  Implement the smallest coherent slice.
4.  Add/update tests.
5.  Build.
6.  Run tests.
7.  Report diagnostics and limitations.

For M0 acceptance criteria and the full roadmap, see
`docs/DEVELOPMENT_SPEC.md`.
