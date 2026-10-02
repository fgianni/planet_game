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

Most recently completed milestone: **M4 --- Basic snow, ice and albedo
feedback**.

M0 through M4 are complete. M2 includes gate G2-M2 (finite-volume
operators), the state-partition/persistent-snapshot foundation, foundation
hardening, plate-scale terrain with sub-cell hypsometry and sea level, static
cell-mesh drainage, and the simulation modes, scheduler skeleton and sub-step
calendar (task M2-04). The pre-M3 seasonal decision record is accepted
(ADR-0006).

M3 tasks M3-01 (sub-step mean insolation), M3-02 (surface energy columns,
experiments A and B, ADR-0007) and M3-03 (the ocean mixed layer in `double`,
run manifests, state hashes and replay, the ADR-0001 performance gate) are
complete.

M4 (basic snow, ice and albedo feedback) is complete under ADR-0008 and
ADR-0009: tasks M4-01 (cryosphere state, land snow, PSNAP schema 4), M4-02
(diffusive heat transport with shared cell air), M4-03 (sea ice), M4-04
(seasonal experiment, climatology, refit) and M4-05 (ADR-0003 snapshot
history: zstd compression, delta chains and forks). Most recently
completed task:
[`docs/tasks/M4-05-history-deltas-and-compression.md`](docs/tasks/M4-05-history-deltas-and-compression.md).
Task documents in `docs/tasks/` state their scope, the decisions already
made, acceptance criteria and what to report.

M5 (atmosphere and pressure) was requested on 2026-10-01 and is in progress
under ADR-0010 (accepted 2026-10-01): M5-01 (the ordered snapshot migration
chain), M5-02 (atmosphere state, hydrostatics, PSNAP schema 5) and M5-03
(column radiation and convection; ADR-0008 §10 sea-ice floes and leads)
are complete; M5-04 (calibration, preset switch, gates and close)
remains. Do not implement M6 or later milestones unless explicitly
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
