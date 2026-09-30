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

Most recently completed milestone: **M3 --- Surface energy and first thermal
planet**.

M0 through M3 are complete. M2 includes gate G2-M2 (finite-volume
operators), the state-partition/persistent-snapshot foundation, foundation
hardening, plate-scale terrain with sub-cell hypsometry and sea level, static
cell-mesh drainage, and the simulation modes, scheduler skeleton and sub-step
calendar (task M2-04). The pre-M3 seasonal decision record is accepted
(ADR-0006).

M3 tasks M3-01 (sub-step mean insolation), M3-02 (surface energy columns,
experiments A and B, ADR-0007) and M3-03 (the ocean mixed layer in `double`,
run manifests, state hashes and replay, the ADR-0001 performance gate) are
complete.

M4 (basic snow, ice and albedo feedback) has started under ADR-0008: task
M4-01 (cryosphere state, land snow, PSNAP schema 4) is complete. Most
recently completed task:
[`docs/tasks/M4-01-cryosphere-state-and-land-snow.md`](docs/tasks/M4-01-cryosphere-state-and-land-snow.md).
Task documents in `docs/tasks/` state their scope, the decisions already
made, acceptance criteria and what to report. M4 was requested on
2026-09-30; do not implement M5 or later milestones unless explicitly
requested.

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
