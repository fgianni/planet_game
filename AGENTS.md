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
the remaining mode and regional-transport gap. ADR-0027's within-group heat
mixing is accepted and implemented in the Earth-like preset. The current
radiation pair is τ_d = 1.147 and κ_v = 0.005735 m²/kg; ADR-0026 option 1
(convective moistening) and ADR-0024 §9's diffusive fallback remain enabled.
L4 calibration and the L5/L6 budget and performance gates pass. L4 V8 over
three years now passes globally (reference minus climate −0.029 K;
reference/climate precipitation 0.908), but land is still 2.7 K warmer and
ocean 1.1 K colder in reference mode. The L6 restart reproduces four monthly
hashes across one and four workers through two fallback months. INV-04's
current next decision is an ADR proposal for tropical heat exchange: a
stronger off-by-default eddy sensitivity closes the land–ocean split, but
requires a refit and renewed parity checks. Do not treat global V8 agreement
as regional parity.
Changes to the fallback or nonlinear solve require an architecture decision.
Do not implement M9 or later milestones unless explicitly requested.
M8A (wind-driven cloud-water transport) is a proposed post-M8 follow-up in
`docs/tasks/M8A-01-wind-driven-cloud-transport.md`; do not implement it before
an architecture decision resolving ADR-0023 §8 is accepted. INV-04 remains
the current investigation.

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
