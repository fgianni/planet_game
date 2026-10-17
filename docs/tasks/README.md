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
| [M7-03](M7-03-saturation-rainout.md) | Saturation rainout: implicit condensation in the column solve, model precipitation, the surface air's humidity (ADR-0021 §4.4, §10, V4) | Complete |
| [M7-04](M7-04-humidity-transport.md) | Humidity transport in climate mode: the coupled implicit tracer solve, convergence with water (ADR-0021 §4.5, §11, V6, V7 recorded) | Complete |
| [M7-05](M7-05-vapour-radiation.md) | Vapour radiation: τ = τ_d p/p₀ + κ_v W with the water cycle, provisional constants and the feedback's bistability (ADR-0021 §4.6, §12) | Complete |
| [M7-06](M7-06-reference-mode.md) | Reference mode: the vapour advected by the primitive-equation core, the resolved winds for evaporation (ADR-0021 §4.5, V6) | Complete |
| [M7-07](M7-07-refit-and-close.md) | Refit and close: τ_d and κ_v fitted to 288 K, the water cycle on by default, τ_c, the gates; V9 recorded, deferred to M8 (ADR-0021 §13) | Complete |
| [M8-01](M8-01-cloud-water-state.md) | Cloud water state: the layers' cloud water, PSNAP schema 7, the cloud diagnostics' fields (ADR-0023 §4.1) | Complete |
| [M8-02](M8-02-moist-convection.md) | Moist convection: the dry adjustment and Betts–Miller (ADR-0023 §4.2); reference mode stable over ten years, V7 as read in ADR-0023 §11.3 passes | Complete |
| [M8-03](M8-03-cloud-water-to-precipitation.md) | Cloud water to precipitation: condensate as cloud water, autoconversion, falling and re-evaporation (ADR-0023 §4.3, V1, V2, V5) | Complete |
| [R1-01](R1-01-presentation-library-and-channels.md) | Presentation library and semantic channels (ADR-0018; rendering track, parallel to M6) | Complete |
| [R1-02](R1-02-style-packs-and-switching.md) | Godot style packs, recorded playback and instant switching (ADR-0018) | Complete |
| [R1-03](R1-03-readability-harness.md) | Offscreen style readability gate and colour-vision checks (ADR-0018) | Complete |
| [R2-01](R2-01-temperature-overlay-and-readout.md) | Shared temperature-anomaly overlay and semantic cell readout (ADR-0018) | Complete |
| [R2-02](R2-02-live-climate-bridge.md) | Asynchronous live climate stepping through the snapshot boundary (ADR-0018) | Complete |
| [R3-01](R3-01-atmosphere-and-wind-channels.md) | Snapshot-only atmospheric density and surface-wind channels (ADR-0018) | Complete |
| [R3-02](R3-02-atmosphere-scattering-and-wind.md) | Godot atmosphere scattering and animated surface-wind overlay (ADR-0018) | Complete |
| [R4-01](R4-01-precipitation-channel.md) | Snapshot-only normalized model-precipitation channel (ADR-0018) | Complete |
| [R4-02](R4-02-precipitation-overlay.md) | Shared animated precipitation overlay and cell readout (ADR-0018) | Complete |
| [R4-03](R4-03-precipitation-readability.md) | Offscreen precipitation readability gate (ADR-0018) | Complete |
| [R4-04](R4-04-live-water-preview.md) | Opt-in authoritative live-water preview for precipitation rendering (ADR-0018) | Complete |
| [R4-05](R4-05-precipitation-particles.md) | Bounded GPU-animated precipitation shafts (ADR-0018) | Complete |
| [R4-06](R4-06-render-capture.md) | Deterministic live-frame screenshot capture and CI artifact (ADR-0018) | Complete |
| [R1-04](R1-04-continuous-globe-camera-and-terrain-tiles.md) | Continuous Earth-viewer camera, altitude bands and streamed terrain tiles (ADR-0022) | Complete |
| [G1-01](G1-01-industrial-dawn-scaffold.md) | Vertical slice: Industrial Dawn scaffold, 1880–1980 in the GDScript prototype (`docs/VERTICAL_SLICE.md`) | Proposed |
| [G1-02](G1-02-routes-as-data.md) | Vertical slice: capabilities and routes as data — coal, algae, dams, with counterweights | Proposed |
| [G1-03](G1-03-cohorts-and-grievance.md) | Vertical slice: three cohorts, grievance, trust and protest | Proposed |
| [G1-04](G1-04-levers-promises-and-knowledge.md) | Vertical slice: levers, promise ledger, local records and climatology | Proposed |
| [G1-05](G1-05-turn-flow-end-report-and-playtests.md) | Vertical slice: turn flow, end report, logging, balance runner and playtest kit | Proposed |
| [G1-06](G1-06-local-scale-consequence-view.md) | Vertical slice (stretch): watch-only local scale for consequences (ADR-0022) | Proposed |

Completed tasks are records: they keep the specification and design versions
they were written against, even after those files have left the tree. Read
cited versions from git history (`git log -- docs/DEVELOPMENT_SPEC_v0_2.md`,
for example).
