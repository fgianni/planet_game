# Architecture decision records

This directory is the single source of truth for PlanetSim architecture
decisions. New decisions use the next unused number and must record their
status, date, consequences, validation plan, and supersession relationships.

## Active decisions

| ID | Decision | Status | Accepted |
|---|---|---|---|
| 0001 | [Time acceleration, simulation modes, and performance budget](0001-time-acceleration.md) | Accepted (amended 2026-09-28, 2026-09-29) | 2026-09-24 |
| 0002 | [Mesh topology, resolution policy, and field layout](0002-mesh-and-field-layout.md) | Accepted (amended 2026-09-28) | 2026-09-25 |
| 0003 | [Determinism, snapshots, history, and migration](0003-determinism-snapshots-migration.md) | Accepted | 2026-09-25 |
| 0004 | [Keplerian orbit and coordinate frames](0004-keplerian-orbit-and-coordinate-frames.md) | Accepted | 2026-09-24 |
| 0005 | [Fractional coastlines and cell-mesh drainage](0005-coastlines-and-drainage.md) | Accepted (amended 2026-09-28, §9.2) | 2026-09-28 |
| 0006 | [Seasonal climate-mode steps on the integer clock](0006-seasonal-climate-steps.md) | Accepted | 2026-09-29 |
| 0007 | [Surface energy columns for the first thermal planet](0007-surface-energy-columns.md) | Accepted (amended 2026-09-29) | 2026-09-29 |
| 0008 | [Snow, sea ice and the ice–albedo feedback](0008-snow-and-sea-ice.md) | Accepted (amended 2026-10-01, V7; implementation record §9–9.2) | 2026-09-30 |
| 0009 | [Diffusive horizontal heat transport before the atmosphere](0009-diffusive-heat-transport.md) | Accepted (amended 2026-09-30, §10–12) | 2026-09-30 |

Records cite the design record by version and section (for example "Design
Record v0.4, §28"). The current design record is
`docs/planetary_civilization_simulator_design_v0_9.docx`, the only version
kept in the tree; earlier versions are in git history. Accepted records
retain the design-version citations against which their decisions were made,
and every section they cite has the same number and title in v0.9.

## Historical records

Superseded records remain available under [`archive/`](archive/) to preserve
the reasoning and implementation history. The archived 0002 and 0003 records
retain their original identifiers, but links to those identifiers without an
explicit archive path always mean the active decisions above.

| Historical decision | Status | Superseded by |
|---|---|---|
| [Primal triangular icosphere cells](archive/0002-primal-triangular-icosphere.md) | Superseded | [0002](0002-mesh-and-field-layout.md) |
| [Initial determinism, snapshots, and replay scope](archive/0003-determinism-snapshots-and-replay.md) | Superseded | [0003](0003-determinism-snapshots-migration.md) |
