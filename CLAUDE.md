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

Most recent investigation: [INV-04](docs/tasks/INV-04-v8-transport-and-radiation.md)
(closed 2026-11-05; its "Outcome" section has the summary and the
known limitations carried forward), V8 (climate–reference parity), measured at L4 over three years (ADR-0023
§16). Its first cause (2026-10-23): climate mode's land air is cold and dry
because its transport brings almost no water to land. ADR-0027 (accepted
2026-10-23) mixes each transport group's heat (`heat_group_mixing = 1`).
ADR-0028 (accepted 2026-10-26) adds the equatorial waves' exchange to the
eddy diffusivity; at c = 50 m/s V8 passed at L4 (−0.146 K), but by
compensation. ADR-0029 (proposed: land's water in climate mode; B or C deferred until
after INV-05) found
(2026-10-30) that at the same sea-surface temperature climate mode's
tropical air is 9–12 K colder than reference mode's, its Hadley overturning
4–10 times weaker, its water held in the bottom layer, and the waves
flattened its equator-to-pole difference (P2 38.9 against 48.8 K).
**ADR-0028 §11 (accepted with the user 2026-10-30): c = 25 m/s,** the
radiation pair refitted to τ_d = 1.1298584, κ_v = 0.0056492919 m²/kg
(L4/150 288.009 K). V8 at L4 then fails as expected, +0.473 K
(precipitation ratio 0.970), with P2 within 0.7 K of reference mode's;
V11 passes on the i9-14900KS (L5/250 412 s, L6/250 1,542 s). What remains
looks structural: climate mode's rain is 99 % Betts–Miller's from its bottom
layer, reference mode's 79 % from resolved ascent aloft. **V8 at L5
(c = 50) fails at −3.195 K: reference mode is not converged between L4 and
L5** (2.8 K colder at L5, P2 48.8 → 60.1 K; climate mode hardly changes),
so V8 at L4 is level-dependent (ADR-0023 §16.5). **V8 at L6 (c = 25) runs
on the RTX 5070 Ti machine** (started 2026-11-02 09:16, about 29 h; log
`~/src/planet_game_runs/a3db687/build/inv04/v8_L6_c25.log` there): it
decides whether reference mode settles by L5. It did not finish: reference
mode stopped in its second year at L6, its winds past the dynamics' 150 m/s
bound (ADR-0023 §16.6). **[INV-05](docs/tasks/INV-05-reference-resolution.md),
reference mode's resolution dependence, closed 2026-11-12:** the polar caps
cool 5–10 K per level and do not settle by L6; several resolution-dependent
schemes each explain 1–3 K and about 8 K is unattributed; reference mode's
upwind vapour advection is a 6–8 K bias of its caps at every level. Its
"Outcome" lists the decisions left (a second-order vapour advection, how
V8 treats an unconverged reference, the small fixes). Nothing is tuned
against V8 meanwhile. ADR-0024 §9's fallback (R_f = 100 W/m², with §9.6's restart) is
on in the preset; the column gate is on the residual, 1e-9 W/m² (ADR-0027
§10); `PlanetRun` has no resume constructor. INV-04's "Outcome" and open
tasks list the rest. Changes to the fallback or nonlinear solve require an architecture
decision. M9 must not start unless explicitly requested.

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
