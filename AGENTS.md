# AGENTS.md

This repository is the Planetary Civilization Simulator.

Before changing code, read `docs/DEVELOPMENT_SPEC_v0_4.md` in full. It is
the development specification: the implementation contract and
architecture source of truth. The design document is
`docs/planetary_civilization_simulator_design_v0_9.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
Only the current specification and design record are kept in the tree;
earlier versions are in git history. Accepted ADRs and completed task
documents keep the version citations they were written against.

## Current phase

P0 --- Living Planet.

Most recently completed milestone: **M2 --- Geological planet, terrain and
ocean basins**.

M0 through M2 are complete. M2 includes gate G2-M2 (finite-volume
operators), the state-partition/persistent-snapshot foundation, foundation
hardening, plate-scale terrain with sub-cell hypsometry and sea level, and
static cell-mesh drainage. The simulation-mode scheduler and the pre-M3
seasonal-resolution ADR have not started.

Most recently completed task:
[`docs/tasks/M2-03-drainage.md`](docs/tasks/M2-03-drainage.md).
Task documents in `docs/tasks/` state their scope, the decisions already
made, acceptance criteria and what to report. Do not implement M3 or later
milestones unless explicitly requested.

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
`docs/DEVELOPMENT_SPEC_v0_4.md`.
