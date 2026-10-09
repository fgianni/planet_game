# Task documents

Each task document states its scope, the decisions already made, acceptance
criteria and what to report. `AGENTS.md` names the current task.

| Task | Subject | Status |
|---|---|---|
| [M2-01](M2-01-state-partition-and-snapshots.md) | State partition and persistent snapshots | Complete |
| [M2-01b](M2-01b-foundation-hardening.md) | Foundation hardening before terrain (specification §26) | Complete |
| [M2-02](M2-02-plates-terrain-and-sea-level.md) | Plates, terrain, hypsometry and sea level | Complete |
| [M2-03](M2-03-drainage.md) | Drainage | Complete |
| [M2-04](M2-04-scheduler-and-calendar.md) | Simulation modes, scheduler skeleton and sub-step calendar (ADR-0001 §8, ADR-0006) | Complete |
| [M3-01](M3-01-substep-mean-insolation.md) | Sub-step mean insolation (ADR-0006 §4.3) | Complete |
| [M3-02](M3-02-surface-energy-columns.md) | Surface energy columns, experiments A and B (ADR-0007) | Complete |
| [M3-03](M3-03-replay-gates-and-close.md) | Ocean mixed layer in `double`, run manifest and replay, performance gate; closes M3 | Complete |
| [M4-01](M4-01-cryosphere-state-and-land-snow.md) | Cryosphere state, land snow, PSNAP schema 4 (ADR-0008) | Complete |
| [M4-02](M4-02-diffusive-heat-transport.md) | Diffusive horizontal heat transport (ADR-0009) | Complete |
| [M4-03](M4-03-sea-ice.md) | Sea ice on the ocean tile (ADR-0008 §4.4) | Complete |
| [M4-04](M4-04-seasonal-experiment-and-refit.md) | Seasonal experiment, climatology, refit with sea ice | Complete |
| [M4-05](M4-05-history-deltas-and-compression.md) | Snapshot history: compression, delta chains, forks (ADR-0003) | Complete |
| [M5-01](M5-01-migration-chain.md) | The ordered snapshot migration chain (ADR-0003 §3.6, ADR-0010 §4.7) | Complete |
| [M5-02](M5-02-atmosphere-state-and-hydrostatics.md) | Atmosphere state, hydrostatic initialisation, PSNAP schema 5 (ADR-0010) | Complete |
| [M5-03](M5-03-column-radiation-and-convection.md) | Column radiation and convection, surface coupling; sea-ice floes and leads (ADR-0010, ADR-0008 §10) | Complete |
| [M5-04](M5-04-calibration-and-close.md) | Refit, preset switch, plateau experiment, performance gates; closes M5 (ADR-0010 §11) | Complete |
| [M6-01](M6-01-c-grid-geometry-and-operators.md) | C-grid geometry and vector operators (ADR-0011 §4.2, V1, V2) | Complete |
| [M6-02](M6-02-shallow-water-core.md) | Shallow-water core, Williamson tests 2 and 5 (ADR-0011 §4.3, V3; decides §12) | Complete |
| [M6-03](M6-03-primitive-equations.md) | Primitive equations, Held–Suarez, the winds in reference mode (ADR-0011 §4.3, §13, V4–V6) | Complete |
| [M6-04](M6-04-balanced-circulation.md) | Climate-mode balanced circulation: zonal model, azonal balance, derived outputs (ADR-0011 §4.4–4.6, §14–§16, V8, V9) | Complete |
| [M6-05](M6-05-transport-refit-and-close.md) | Transport by the circulation, the balanced p_s in the slow state, refit and close (ADR-0011 §4.7–4.8, §17, V7, V10, V12, V13) | Complete |
| [M7-01](M7-01-water-state-and-saturation.md) | Water state and saturation: humidity and the bucket, PSNAP schema 6 (ADR-0021 §4.1–4.2, V1, V10) | Complete |
| [M7-02](M7-02-evaporation-and-the-bucket.md) | Evaporation and the bucket: the implicit latent flux in the tile solves, the bucket, the water and energy budgets (ADR-0021 §4.3, V2, V3, V5) | Complete |
| [R1-01](R1-01-presentation-library-and-channels.md) | Presentation library and semantic channels (ADR-0018; rendering track, parallel to M6) | Complete |

Completed tasks are records: they keep the specification and design versions
they were written against, even after those files have left the tree. Read
cited versions from git history (`git log -- docs/DEVELOPMENT_SPEC_v0_2.md`,
for example).
