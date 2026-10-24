# CLAUDE.md

Read `docs/DEVELOPMENT_SPEC_v0_8.md` before making changes. It is the
development specification. The design document is
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

The specification defines the architecture, physical design, P0 roadmap,
validation strategy, and current implementation task for the Planetary
Civilization Simulator.

Most recently completed milestone: **P0 / M8 --- Clouds and precipitation**
(ADR-0023, amended §11--§15; ADR-0024; ADR-0025, amended §11--§14):
M8-01 (cloud water state, PSNAP schema 7), M8-02 (Betts--Miller moist
convection), M8-03 (cloud water to precipitation), M8-04 (cloud
radiation), M8-05 (`docs/tasks/M8-05-refit-and-gates.md`: the refit, V9
passing, V11 passing at L5 and L6), M8-06 (clouds on screen, V12), M8-07
(sub-grid condensation) and M8-09 (convective condensate and anvils), the
last two implemented and off in the preset. M8 closed on 2026-10-20 with
the global cloud field (ADR-0025 §14): the zonal cloud pattern, the
modes' cloud longwave difference and V8's +6.4 K temperature gap are
recorded as known limitations. M0--M7 (mesh, orbit, terrain, drainage,
scheduler and calendar, surface energy columns, replay, snow and sea ice,
diffusive transport, snapshot history, the layered atmosphere, winds and
the climate circulation, humidity and evaporation) are complete.

Key constraints: - standalone C++20 PlanetSim core; - no Godot
dependency in simulation code; - headless build/tests first; - SI
units; - deterministic and testable implementation; - physical
state/fluxes rather than scripted climate modifiers; - production code
and tests together; - do not proceed to M9 unless explicitly requested.

Current investigation: [INV-04](docs/tasks/INV-04-v8-transport-and-radiation.md),
the remaining V8 gap. INV-03 completed the post-fallback refit and restart
checks. V8 is now measured at L4 over three years (ADR-0023 §16, accepted
2026-10-22). Its baseline fails: 0.936 K against the 0.3 K limit, and a
0.901 reference/climate precipitation ratio against ±10 %. The gap is
reference land 9.2 K warmer and its ocean 2.4 K colder. The earlier
L3 measurement was 4.148 K. Its cause, found 2026-10-23: climate mode's
land air is cold and dry because its transport brings almost no water to
land (1.55 against reference mode's 23.3 W/m² of latent heat), which
weakens the greenhouse over land. Most of the contrast was inside the
transport's groups; ADR-0027 (accepted 2026-10-23, implemented 2026-10-24)
mixes each group's heat (`heat_group_mixing = 1` in the preset), with the
radiation pair refitted to τ_d = 1.147, κ_v = 0.005735 m²/kg (L4/150
287.985 K). **V8 now passes at L4:** −0.029 K, precipitation ratio 0.908;
but reference land is still 2.7 K warmer and its ocean 1.1 K colder, which
cancel, and climate land's air stays dry. V11: L5/250 499.3 s (1 fallback
month), L6/250 1,812.5 s (3 fallback months); the column gate is now on
the residual, 1e-9 W/m² (ADR-0027 §10). INV-04's open tasks list the rest.
The accepted ADR-0024 §9 fallback is on in the Earth-like preset: a coupled
month over 100 W/m² consistency residual uses the diffusive transport.
Before ADR-0027, with the pair τ_d = 1.111, κ_v = 0.005555 m²/kg, the
L4/150 mean was 287.984 K, the L5/250 run took 308.3 s, and the L6/250
run took 1,061.1 s. Both performance runs pass their budget and column
convergence gates; L6 has 10 fallback months and no failed circulation
months. Two-year L6 runs on one and four workers have the same hash
through eight fallback months. An L4 PSNAP restart before the month-2
fallback, using INV-02's radiation pair, reproduces the fallback and the
uninterrupted six-month hash with four versus one workers. It restores the
climate scheduler directly; `PlanetRun` still has no resume constructor.
The refitted L6 restart also passes (2026-10-22): a PSNAP at tick 175317
before month 4 reproduces all four monthly hashes through months 4–7 with
four versus one workers, including three fallback months and the next
non-fallback month. Energy/water gates and column convergence pass. Changes
to the fallback or nonlinear solve require an architecture decision. M9 must
not start unless explicitly requested.

The rendering track (specification §31.10) runs in parallel with the P0
milestones. Its first task is
[`docs/tasks/R1-01-presentation-library-and-channels.md`](docs/tasks/R1-01-presentation-library-and-channels.md),
under ADR-0018 (accepted 2026-10-04). It may proceed alongside the P0 milestones, but
must never change a solver, a field's meaning or the PSNAP format. Its most
recent task, [`docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md`](docs/tasks/R1-04-continuous-globe-camera-and-terrain-tiles.md)
(ADR-0022: the Earth-viewer camera and streamed terrain tiles), is complete
(2026-10-15).

When starting work, first audit the repository and propose the concrete
implementation plan for the current milestone against the specification. Then implement in
small, reviewable steps and run the relevant tests after each coherent
step.
