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

Most recently completed milestone: **M8 --- Clouds and precipitation**.

M0 through M8 are complete. The standalone core includes the centroidal
Voronoi mesh, orbit, geological terrain and drainage, scheduler and calendar,
surface energy, replay and persistent snapshots, snow and sea ice, layered
atmosphere, winds and climate circulation, humidity, clouds and precipitation.
M8 closed under ADR-0023, ADR-0024 and ADR-0025 §14; sub-grid condensation
and convective anvils are implemented but off in the Earth-like preset.
Task documents in [`docs/tasks/README.md`](docs/tasks/README.md) state their
scope, accepted decisions, acceptance criteria and current status.

Current investigation: [INV-04](docs/tasks/INV-04-v8-transport-and-radiation.md),
the remaining V8 gap. [INV-03](docs/tasks/INV-03-l6-startup-transport.md)
completed the post-fallback refit and restart checks. ADR-0026 option 1
(convective moistening) and ADR-0024 §9's diffusive fallback are enabled in
the Earth-like preset. The fallback threshold is 100 W/m²; the refitted
radiation pair is τ_d = 1.111 and κ_v = 0.005555 m²/kg. L4 calibration,
L5/L6 budget and performance gates, and worker-count determinism pass.
The L4 snapshot restart through a fallback passes using INV-02's radiation
pair. The refitted L6 restart also passes: all four monthly hashes match
with one worker uninterrupted versus four after restart, through three
fallback months and the following non-fallback month (2026-10-22).
The measured INV-02 temperature-parity gap remains 4.148 K against a 0.3 K
limit, while precipitation passes at a 0.969 reference/climate ratio.
Changes to the fallback or nonlinear solve require an architecture decision.
Do not implement M9 or later milestones unless explicitly requested.

The rendering track (specification §31.10) runs in parallel with the P0
milestones under ADR-0018. It must never change a solver, a field's meaning
or the PSNAP format. R1-04, the continuous globe camera and streamed terrain
tiles under ADR-0022, is complete (2026-10-15); M8-06's cloud rendering is
also complete. See the task index for the completed rendering tasks.

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
