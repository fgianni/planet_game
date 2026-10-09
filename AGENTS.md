# AGENTS.md

This repository is the Planetary Civilization Simulator.

Before changing code, read `docs/DEVELOPMENT_SPEC_v0_8.md` in full. It is
the development specification: the implementation contract and
architecture source of truth. The design document is
`docs/planetary_civilization_simulator_design_v1_4.docx`.

Accepted ADRs in `docs/decisions/` take precedence where they conflict
with the specification (notably ADR-0002 on the mesh: a
hexagonal--pentagonal centroidal Voronoi tessellation).
Design v1.4 §46 (systems architecture: interpretation layer, PopSim,
population knowledge, presentation, player levers) is not yet traced into
the specification; where it conflicts with the specification or an
accepted ADR, they win. Its PlanetSim part (§46.3) is reconciled with them
in the proposed ADR-0020 (`docs/decisions/0020-planet-read-contract.md`),
which is not yet accepted and must not be implemented until it is.
Only the current specification and design record are kept in the tree;
earlier versions are in git history. Accepted ADRs and completed task
documents keep the version citations they were written against.

## Current phase

P0 --- Living Planet.

Most recently completed milestone: **M6 --- Wind and Coriolis**.

M0 through M6 are complete. M2 includes gate G2-M2 (finite-volume
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
history: zstd compression, delta chains and forks).
Task documents in `docs/tasks/` state their scope, the decisions already
made, acceptance criteria and what to report.

M5 (atmosphere and pressure) is complete under ADR-0010 (accepted
2026-10-01, amended §11): tasks M5-01 (the ordered snapshot migration
chain), M5-02 (atmosphere state, hydrostatics, PSNAP schema 5), M5-03
(column radiation and convection; ADR-0008 §10 sea-ice floes and leads)
and M5-04 (refit of τ₀ and D, the Earth-like preset on three layers, the
plateau experiment, performance gates). Most recently completed task:
[`docs/tasks/M5-04-calibration-and-close.md`](docs/tasks/M5-04-calibration-and-close.md).
M6 (wind and Coriolis) is complete under ADR-0011 (amended §12–§17) and
ADR-0009 §13: tasks M6-01 (C-grid geometry and vector operators), M6-02
(shallow-water core), M6-03 (primitive equations, Held–Suarez, the winds
in reference mode), M6-04 (the climate-mode balanced circulation) and
M6-05 (the circulation carries the heat, the balanced p_s in the slow
state, τ₀ refitted, the gates). Most recently completed task:
[`docs/tasks/M6-05-transport-refit-and-close.md`](docs/tasks/M6-05-transport-refit-and-close.md).
M7 (humidity and evaporation) is in progress under ADR-0021 (accepted
2026-10-09): M7-01 (water state, saturation, PSNAP schema 6) and M7-02
(evaporation and the bucket) are complete; next is M7-03 (saturation
rainout). Do not implement M8 or later
milestones unless explicitly requested.

The rendering track (specification §31.10) runs in parallel with the P0
milestones. Its first task is
[`docs/tasks/R1-01-presentation-library-and-channels.md`](docs/tasks/R1-01-presentation-library-and-channels.md),
under ADR-0018 (accepted 2026-10-04). It may proceed alongside the P0 milestones, but
must never change a solver, a field's meaning or the PSNAP format.

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
`docs/DEVELOPMENT_SPEC_v0_8.md`.
