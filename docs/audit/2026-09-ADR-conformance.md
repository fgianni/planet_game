# ADR conformance audit — M0/M1 migration and M2

- **Audit date:** 2026-09-29
- **Evidence baseline:** M0/M1 migration at commit `6e91459`, plus the G2-M2
  operator, state/snapshot, terrain and drainage implementation through
  2026-09-29
- **Plan audited:** [M0/M1 ADR migration plan](../M0-M1-ADR-migration-plan.md)
- **Scope:** completed M0/M1 migration and M2 implementation against
  [ADR-0001](../decisions/0001-time-acceleration.md),
  [ADR-0002](../decisions/0002-mesh-and-field-layout.md), and
  [ADR-0003](../decisions/0003-determinism-snapshots-migration.md), plus
  [ADR-0005](../decisions/0005-coastlines-and-drainage.md)

## Conclusion

The repository satisfies the actual M0 and M1 milestone acceptance criteria.
M2 is complete: the finite-volume operator gate, state partition, base
persistent snapshot format, plate-scale geology with sub-cell hypsometry and
sea level, and deterministic static drainage topology are implemented. The
simulation-mode scheduler and pre-M3 seasonal-resolution ADR have not started;
later persistence work remains assigned to M3 through M5.

The migration cleared the three structural problems found by the historical
pre-migration audit:

| Before migration | Current state |
|---|---|
| Accumulated `double` simulation clock | Signed 64-bit `SimulationTick`; physical seconds are derived |
| Primal triangular cells | Vertex-centred dual cells with hexagonal/pentagonal topology |
| No field registry, keyed RNG, aligned/layer-major fields, locality order, or strict FP flags | All are present and covered at the level applicable to M0/M1 |

## Markers

| Marker | Meaning |
|---|---|
| ✅ EVIDENCED | Implemented and supported by a test, type, or direct source evidence |
| ⚠ RUNTIME_ONLY | Partially implemented, behavior-only, or missing a required enforcement mechanism |
| ❌ VIOLATION | The implementation contradicts a current requirement |
| NOT FOUND | Not implemented; the intended milestone is noted where known |

## A. Mesh and geometry

| ID | Status | Evidence and finding |
|---|---|---|
| A1 | ✅ EVIDENCED | Authoritative cells are the vertex-centred dual. L5 has 10,242 cells and every tested level has exactly 12 pentagons (`sim/planet/mesh/icosphere.cpp`, `tests/physics/test_icosphere.cpp:14-65`). |
| A2 | ✅ EVIDENCED | `CellGeometry` stores centre, area, and East/North basis, constructed once with the mesh (`sim/planet/mesh/planet_mesh.hpp:44-55`, `sim/planet/mesh/icosphere.cpp:238-250`). |
| A3 | ✅ EVIDENCED | CSR cell incidences store neighbour, shared edge ID, and local outward normal; shared edges store length and centre distance (`sim/planet/mesh/planet_mesh.hpp:57-69`, `sim/planet/mesh/icosphere.cpp:281-340`). |
| A4 | ✅ EVIDENCED | `CellGeometry::is_pentagon()` is queryable and topology tests require exactly 12 pentagons (`sim/planet/mesh/planet_mesh.hpp:54`, `tests/physics/test_icosphere.cpp:35-65`). |
| A5 | ✅ EVIDENCED | *Updated 2026-09-28 (G2-M2).* Divergence of edge fluxes, least-squares gradient and two-point Laplacian (`sim/planet/operators/finite_volume.hpp`) on the amended centroidal Voronoi mesh. V3 (`tests/physics/test_operator_accuracy.cpp`) and V4 (`tests/conservation/test_operator_conservation.cpp`) pass; the error map is a CI artifact. |
| A6 | ✅ EVIDENCED | Area closure and exact topology are tested from L0 through L6 (`tests/conservation/test_mesh_area.cpp`, `tests/physics/test_icosphere.cpp`). The test prints every measured closure; the L0--L6 table is recorded in the README and ADR-0002 §9, with a maximum of `1.2253352946929809e-16` against the `5e-14` gate. |

Pentagons are excluded from operator convergence statistics (G2-M2) and their
errors are reported separately. Scenario anchors do not exist yet; add that
protection with the scenario work.

## B. Field layout and parallelism

| ID | Status | Evidence and finding |
|---|---|---|
| B1 | ✅ EVIDENCED | Each field uses one contiguous allocation through a 64-byte-aligned allocator (`sim/core/fields/field.hpp:15-101`). |
| B2 | ✅ EVIDENCED | `Field3D` indexes as `layer * cell_count + cell`, and layer spans are contiguous (`sim/core/fields/field.hpp:105-166`). |
| B3 | ✅ EVIDENCED | Registry dtype and layout select each owned field's container type at compile time through `field_container_t`; `make_field` derives dimensions from the mesh and descriptor (`sim/planet/field_factory.hpp`, `tests/unit/test_field.cpp`). Current insolation and hypsometry fields are `float`, while the slow global sea-level reservoir is `double`. |
| B4 | ✅ EVIDENCED | Cell IDs follow first encounter in the recursively ordered final faces, before fields are allocated (`sim/planet/mesh/icosphere.cpp:173-223`). This achieves the required locality without a separate permutation pass. |
| B5 | ✅ EVIDENCED | The mesh stores fixed 256-cell logical blocks independent of worker count (`sim/planet/mesh/planet_mesh.hpp:71-76`, `sim/planet/mesh/icosphere.cpp:103-112`). |
| B6 | ✅ EVIDENCED | `reduce_deterministic_blocks` computes one partial per fixed logical block and merges partials sequentially in block-index order, with no atomics (`sim/core/scheduler/deterministic_executor.hpp`). Its order-sensitive float test is bit-identical for 1, 2, 8 and 16 workers and to a serial block-order fold; solar diagnostics use the helper without changing output (`tests/unit/test_deterministic_executor.cpp`, `sim/planet/orbit/solar_diagnostics.cpp`). |
| B7 | ✅ EVIDENCED | `/fp:strict` or `-ffp-contract=off` is configured and fast-math is not enabled (`cmake/CompilerWarnings.cmake:1-22`). Every CI build job runs `tools/ci/check_fp_flags.py` against `compile_commands.json`, which fails on a missing effective `-ffp-contract=off` or any fast-math flag, including flags injected through `CMAKE_CXX_FLAGS` (`.github/workflows/ci.yml`). MSVC is not covered because CI does not build with it. |

The V7 benchmark exists in `planet_cli`, verifies ordered and naive kernels
produce identical values, and records timing. ADR-0002 records five optimized
L6 runs with a median naive-to-ordered ratio of 1.028. L5/L6 geometry memory
budgets are enforced by tests.

## C. State partition and modes

| ID | Status | Evidence and finding |
|---|---|---|
| C1 | ✅ EVIDENCED | `PlanetState` exposes `SlowState`, `FastState`, and `Climatology`; the slow partition owns hypsometry and sea level, while forcing is derived outside it (`sim/planet/planet_state.hpp`, `tests/unit/test_planet_state.cpp`). |
| C2 | ✅ EVIDENCED | `FastState` is absent by default, allocated only by `open_fast_state()`, and discarded by `release_fast_state()`; lifecycle behavior is tested (`sim/planet/planet_state.cpp`, `tests/unit/test_planet_state.cpp`). |
| C3 | ✅ EVIDENCED | *Updated 2026-09-29 (task M2-04).* `SimulationMode` and a serial `Scheduler` dispatch registered processes by mode, with reference-mode cadence, on the ADR-0006 sub-step calendar; mode changes are logged and pacing cannot change the step sequence (`sim/core/scheduler/scheduler.{hpp,cpp}`, `tests/unit/test_scheduler.cpp`). |
| C4 | ✅ EVIDENCED | The simulation core has no wall-clock timing dependency. Timing calls are confined to the CLI benchmark/diagnostics and Godot presentation. |

The development specification defines M1 as orbit, sun, day/night, and
seasons—not as the completed mode scheduler. C1, C2 and C3 are now present;
C3 closed M2 with task M2-04.

## D. Clock, RNG, and identity

| ID | Status | Evidence and finding |
|---|---|---|
| D1 | ✅ EVIDENCED | `SimulationTick` is `std::int64_t`, one tick is 60 seconds, and seconds are derived rather than accumulated (`sim/core/scheduler/simulation_clock.hpp:7-24`). |
| D2 | ✅ EVIDENCED | Random values are stateless and keyed by seed, stream, tick, cell, and sample index; tests compare 1-worker and 16-worker output (`sim/core/random/counter_rng.hpp`, `tests/unit/test_clock_parameters.cpp:45-74`). Nine exact golden vectors cover every key argument and stream, including geology, negative and positive ticks, and zero/maximum values (`tests/unit/test_counter_rng.cpp`); changing them requires an ADR because it invalidates runs, snapshots, and replay. No global mutable generator was found. |
| D3 | ✅ EVIDENCED | The registry has stable numeric field IDs with compile-time uniqueness, ordering, persistence, and registered/retired disjointness checks (`sim/core/fields/field_registry.hpp`). `planet_cli registry dump` and `tools/ci/check_field_registry.py` compare it with the committed release baseline in CI; tests cover changed attributes, missing IDs, and explicit retirement. |
| D4 | ✅ EVIDENCED | Persistent `PSNAP` files order registered slow fields by stable ID and encode each in layer-major/cell-major order. L0/L4/L6 write-read-write tests are byte-identical, and the strict reader rejects metadata, length, checksum, and trailing-data errors before mutating state (`sim/core/serialization/snapshot_file.cpp`, `tests/unit/test_snapshot_file.cpp`). |

The accepted M1 guarantees do not require `SimulationTick` to be a strong
wrapper type or the small registry to be generated. The registry records
partition, layout, scalar type, and layer count, and release-to-release
append-only enforcement is active. Migration initializers remain necessary as
schemas evolve.

## Gate status

| Gate | Status | Finding |
|---|---|---|
| Phase 0 audit | ✅ EVIDENCED | The audit was committed at `6e91459` and is maintained here as the current evidence trail. |
| G1-M1 | ✅ EVIDENCED | Integer time, keyed RNG, stable field IDs, uniqueness checks, and worker-independent RNG tests are complete. |
| G2-M0 | ✅ EVIDENCED | V1/V2 dual-mesh geometry and topology are green through L6. |
| G2-M2 | ✅ EVIDENCED | *Updated 2026-09-28.* V3/V4 pass L3–L6 with the error map uploaded by CI. Required the ADR-0002 amendment to circumcentre corners and a centroidal Voronoi mesh; see ADR-0002 §9. |
| G3-M1 | ✅ EVIDENCED | Field layout, fixed blocks, fixed block-index reductions, 1/2/8/16-worker forcing equality, memory gates, and V7 are green. |
| G3-M3 | ✅ EVIDENCED | *Updated 2026-09-29 (task M3-03):* run manifest (`PRUNv1`), command log, yearly XXH3 checkpoint state hashes and replay are implemented; ADR-0003 V1 and V2 run in CI (`tests/regression/test_run_replay.cpp`, `planet_cli replay`). Weather-window schedules await weather windows. |
| G4 | ✅ EVIDENCED | The slow/fast/climatology state partition, lazy fast-state lifecycle, simulation modes and the multi-rate scheduler skeleton (task M2-04) are implemented. *Updated 2026-09-29 (task M3-03):* the years-per-minute harness and the ADR-0001 §5 250-year CI gate at L5 and L6 run in CI Release jobs (ADR-0001 §10); the >20 % regression comparison is deferred until cost approaches the budget. |
| G5 | ⚠ RUNTIME_ONLY | The M2 base snapshot writer/reader, canonical ordering, CRC-32C validation, corruption tests, V4 round trips, CLI inspection, and initial golden save are complete. *Updated 2026-09-29 (task M3-02):* schema v2 with a declared v1 → v2 migration hook is implemented. Schema v3 (ADR-0007 §10) adds the first retired field and an exact v2 → v3 step. *Updated (task M3-03):* manifests and replay are implemented, and every golden save steps ten years (V5). Delta chains/compression (M4) and general migration machinery (M5) are not yet implemented. |

### M2-02 terrain (2026-09-28)

| Requirement | Status | Finding |
|---|---|---|
| Keyed randomness only (ADR-0003) | ✅ EVIDENCED | Every geology draw uses `RandomStreamId::geology` with documented keys (`sim/planet/geology/geology_random.hpp`); noise lattices hash through `mix_random_key`. No stateful generator. |
| Worker-count independence (ADR-0002 §4.6, ADR-0003 L0) | ✅ EVIDENCED | `test_terrain_determinism` compares hypsometry, sea level and every `GeologyState` field bit for bit at 1, 2, 8 and 16 workers on L4 and L5. Global sums, minima and component areas use `reduce_deterministic_blocks`; Dijkstra queues order by `(cost, CellId)`. |
| ADR-0005 V1--V3 | ✅ EVIDENCED | `test_hypsometry`, `test_sea_level`; measured values in ADR-0005 §9.4. |
| ADR-0005 §4.1 ocean rule | ⚠ RUNTIME_ONLY | Implemented with the approved deepest-cell anchor (ADR-0005 §9.2) instead of the largest-area component, which is non-monotone; §4.1 carries an amendment note pending formal revision. |
| No new registered fields | ✅ EVIDENCED | The registry baseline check passes unchanged; `GeologyState` stays in memory (task M2-02 §4.14). |
| Snapshot of a generated planet | ✅ EVIDENCED | Bit-identical round trip at L4 and L5. |

### M2-03 drainage (2026-09-29)

| Requirement | Status | Finding |
|---|---|---|
| Conditional land elevation | ✅ EVIDENCED | Exact piecewise-linear integration covers full, partial and zero-measure land portions (`sim/planet/terrain/hypsometry.cpp`, `tests/unit/test_hypsometry.cpp`). |
| ADR-0005 V5 | ✅ EVIDENCED | Every downstream reference is an edge neighbour or sentinel; controlled and generated graphs are acyclic and terminate at an outlet or the one no-ocean sink (`tests/physics/test_drainage.cpp`). |
| ADR-0005 V6 | ✅ EVIDENCED | Fixed-order double accumulation closes outlet catchments to routed land area: relative error `6.33794e-16` at L5 and `4.22518e-16` at L6. |
| ADR-0005 V7 | ✅ EVIDENCED | Filled elevations never decrease; every connected raised component has one deterministic ID, spill level and canonical spill cell. |
| ADR-0005 V8 | ✅ EVIDENCED | L4/L5 drainage fields, records and diagnostics are bit-identical at 1, 2, 8 and 16 workers, including regeneration after snapshot load (`tests/regression/test_terrain_determinism.cpp`). |
| Edge presets | ✅ EVIDENCED | `dead_rock` has one sink/basin; `aqua_planet` has zero routed land and exact zero closure residual. |
| Derived-state boundary | ✅ EVIDENCED | `DrainageState` is unregistered and regenerated from mesh, hypsometry and sea level; persistent snapshot bytes and registry remain unchanged. |
| Diagnostics | ✅ EVIDENCED | The terrain CLI and CSV expose the required graph, basin, depression, catchment, closure, fill and timing values. |

### M3-02 surface energy columns (2026-09-29)

| Requirement | Status | Finding |
|---|---|---|
| ADR-0007 V1–V4 | ✅ EVIDENCED | Backward-Euler column stable to 1e12 s steps; closure within 1e-9 of the flux scale plus the rounding floor; signs; closed-form equilibrium within 1e-6 K (`tests/unit/test_surface_column.cpp`). |
| ADR-0007 V2 globally, V5–V8 | ✅ EVIDENCED | `tests/physics/test_surface_energy.cpp`; measured values in ADR-0007 §9. Bit-identical at 1, 2, 8 and 16 workers and for chunked scheduler runs across a mode switch. |
| ADR-0007 V9, PSNAP schema v2 | ✅ EVIDENCED | Four new slow fields in the registry baseline; v1 golden loads through the declared migration; v2 golden round trip exact (`tests/regression/test_golden_snapshot.cpp`). |
| ADR-0007 V10 | ✅ EVIDENCED | 1.1 ms (L5) and 4.0 ms (L6) per climate sub-step at 8 workers, against 250 ms and 1 s (`planet_cli thermal`). |
| Calibration constants recorded (specification §24) | ✅ EVIDENCED | `earth_like_grey_emissivity = 0.4964` carries its fit target, run and sensitivity (`sim/planet/surface/surface_energy.hpp`, ADR-0007 §9). |
| Reference-mode precision (ADR-0002 §4.4) | ✅ EVIDENCED | *Updated 2026-09-29:* the ocean mixed layer is `double` (ADR-0007 §10; retired field `0x0003'0003`, new `0x0003'0005`, PSNAP schema 3 with an exact v2 → v3 widening). Thirty days of ten-minute ocean steps equal an all-`double` column bit for bit (`tests/physics/test_surface_energy.cpp`); v1, v2 and v3 golden files load (`tests/regression/test_golden_snapshot.cpp`). |

### M3-03 replay, performance gate and M3 close (2026-09-29)

| Requirement | Status | Finding |
|---|---|---|
| ADR-0003 §3.3 run manifest and state hash | ✅ EVIDENCED | `sim/core/serialization/run_manifest.{hpp,cpp}`, `slow_state_hash`; XXH3-64 pinned by reference vectors (`tests/unit/test_xxh3.cpp`); manifest round trip and rejection tests (`tests/unit/test_run_manifest.cpp`). |
| ADR-0003 V1 | ✅ EVIDENCED | Identical manifests for 1/2/8/16 workers, chunked pacing across a reference window and late command submission (`tests/regression/test_run_replay.cpp`). |
| ADR-0003 V2 | ✅ EVIDENCED | Replay from the manifest file matches; altered inputs and records are located at the right tick; CI replays the 250-year L5 run. |
| ADR-0003 V5 | ✅ EVIDENCED | Golden saves of schemas 1–3 load and step ten years with every step's budget closing (`tests/regression/test_golden_snapshot.cpp`). |
| ADR-0001 §5/§8 M3 gate | ✅ EVIDENCED | CI Release: 250 years at L5 (≥ 20 years/min, ≤ 240 s) and L6 (≥ 5 years/min, ≤ 600 s); measured 2,981 and 774 years/min on 4 workers. |
| ADR-0001 V4 regression gate | ⚠ RUNTIME_ONLY | Absolute budget gated; the >20 % regression comparison against a baseline is not yet automated (ADR-0001 §10). |

### M4-01 cryosphere state and land snow (2026-09-30)

| Requirement | Status | Finding |
|---|---|---|
| ADR-0008 V1, V2 (land) | ✅ EVIDENCED | Energy with the latent term and water close per tile and globally in both modes (`tests/unit/test_land_snow.cpp`, `tests/physics/test_land_snow_planet.cpp`); measured values in ADR-0008 §9. |
| ADR-0008 V4–V6, V8 (land) | ✅ EVIDENCED | Invariants, albedo-feedback sign, bit-identity with ADR-0007 without snow, 1/2/8/16 workers. |
| ADR-0008 V9, PSNAP schema 4 | ✅ EVIDENCED | Two float64 reservoirs in the registry baseline; v1–v3 load through the required schema 3 → 4 initialiser; v4 golden exact (`tests/regression/test_golden_snapshot.cpp`). |
| Stable seasonal land snow (specification §13 M4) | ⚠ RUNTIME_ONLY | *Updated 2026-09-30 (M4-02):* with the ADR-0009 transport, snow clears seasonally from about 15 % of land cells; high latitudes still accumulate it. The seasonal experiment is task M4-04. |

### M4-02 diffusive heat transport (2026-09-30)

| Requirement | Status | Finding |
|---|---|---|
| ADR-0009 V1–V3, V7 | ✅ EVIDENCED | Conservation ≤ 9e-17, closure, down-gradient sign, consistency ≤ 5e-7 W/m² (`tests/physics/test_heat_transport_planet.cpp`, `tests/unit/test_heat_transport.cpp`). |
| ADR-0009 V4–V6, V8 | ✅ EVIDENCED | P2 response within 0.15 % at L5 and converging; stable at ten times `D`; off switch bit-identical; worker counts and replay bit-identical. |
| ADR-0009 V9 calibration (specification §24) | ✅ EVIDENCED | `D` and `g` fitted to 288 K and a 42 K P2 equator-to-pole difference with their record (`sim/planet/surface/surface_energy.hpp`, ADR-0009 §9–10). |
| ADR-0001 performance gates with transport | ✅ EVIDENCED | 250 years at L5 in 107 s and L6 in 461 s on 4 workers; L6 margin about 23 %. |
| Deterministic executor (ADR-0002 §4.6) | ✅ EVIDENCED | Persistent worker pool with unchanged block assignment; sanitizer build green. |

### M4-03 sea ice (2026-09-30)

| Requirement | Status | Finding |
|---|---|---|
| ADR-0008 V1–V6, V8 (ocean tile) | ✅ EVIDENCED | Per-tile and global energy and water closure, Stefan growth, invariants, ice–albedo sign, bit-identity above `T_f`, worker counts (`tests/unit/test_sea_ice.cpp`, `tests/physics/test_sea_ice_planet.cpp`); values in ADR-0008 §9.1. |
| Seawater never below freezing | ✅ EVIDENCED | `initialise_cryosphere` and the schema 3 → 4 migration raise sub-freezing ocean layers to `T_f`; the golden tests expect it. |
| Plausible sea-ice extent (specification §23) | ⚠ RUNTIME_ONLY | *Updated 2026-10-01 (M4-04):* northern extent near Earth's after the refit; the southern cap is an open polar ocean on this seed. |
| ADR-0001 performance gates with sea ice | ✅ EVIDENCED | 250 years at L5 in 176 s and L6 in 572 s on 4 workers, after ADR-0009 §11–12; the L6 margin is about 5 %. |

### M4-04 seasonal experiment, climatology and refit (2026-10-01)

| Requirement | Status | Finding |
|---|---|---|
| Monthly climatology (ADR-0006 §7, ADR-0008 §4.6) | ✅ EVIDENCED | Welford mean and variance per cell and month, never persisted (`tests/unit/test_climatology.cpp`). |
| Refit with sea ice (specification §24) | ✅ EVIDENCED | `g = 0.4965`, `D = 0.64` with their record; northern sea ice 4.8–10.2 million km² at L5. ADR-0001 gates: 250 years at L5 in 148 s, L6 in 508 s on 4 workers. |
| Stable seasonal snow/ice experiment (specification §13 M4, ADR-0008 V7 as amended) | ✅ EVIDENCED | Stationary to 0.5–0.9 % between decades (`tests/physics/test_seasonal_experiment.cpp`); interannual variability of the summer minimum and the drift of perennial ice and land snow recorded (ADR-0008 §9.2). |

## Risk-register audit

| Risk mitigation | Status | Finding |
|---|---|---|
| Migrate before scenarios exist | ✅ EVIDENCED | No scenario data or scenario anchors exist to invalidate. |
| Avoid hard-coded semantic cell IDs | ✅ EVIDENCED | No scenario or geographic tests depend on fixed cell IDs. Literal IDs remain only in generic field-container indexing tests. |
| Treat determinism flakes as blockers | ⚠ RUNTIME_ONLY | Exact worker-count equality is tested, but no CI policy or retry detector enforces the process rule. |
| Prevent field-ID reuse | ✅ EVIDENCED | Compile-time uniqueness and registered/retired disjointness protect the current registry; the committed baseline plus CLI dump and CI checker reject release-to-release ID removal or attribute changes unless an ID is explicitly retired. |
| Bound benchmark noise/regressions | ⚠ RUNTIME_ONLY | Absolute measurements are recorded, but no greater-than-20-percent regression assertion exists. |

## Documentation consolidation

The cleanup resolves the documentation ambiguity found by both historical
audits:

- `docs/decisions/` is the only active ADR directory and has an index.
- Superseded, colliding 0002/0003 records live under
  `docs/decisions/archive/`.
- `docs/audit/2026-09-ADR-conformance.md` is the only current audit.
- The pre-migration audit lives under `docs/audit/history/` with a historical
  warning.
- The stale post-migration re-audit was removed after its useful before/after
  summary was incorporated here.

Development specification v0.4 and design record v0.9 are authoritative and
are the only versions kept in the tree; earlier specifications and design
records are in git history. Accepted ADRs retain their original design-version citations and
take precedence where a specification conflicts.

## Validation observed during the migration and M2 work

- M3-03 (2026-09-29): GCC 11 Debug and Release, Clang 14 Debug and
  Release, ASan+UBSan on Clang 14 and GCC 12 (under `setarch -R`) 51/51;
  floating-point policy 0 violations in 75 translation units; registry check
  8 current, 1 retired.
- M2-03 drainage (2026-09-29): Release 38/38; Clang 14
  ASan+UBSan RelWithDebInfo 38/38 under `setarch -R`; floating-point policy
  0 violations in 57 translation units.
- M2-03 Release `earth_like`, seed 20260928, four workers: L5 drainage
  regeneration 2.788 ms and total terrain generation 154.329 ms; L6 drainage
  regeneration 13.552 ms and total terrain generation 594.988 ms. Both graphs
  report zero invalid, cyclic or unreachable cells.
- M2-02 terrain (2026-09-28, GCC 11): Release and Debug suites 35/35; GCC
  AddressSanitizer 35/35 and UndefinedBehaviorSanitizer (pointer checks
  excluded) 35/35; floating-point policy check 0 violations in 52 translation
  units. GCC 10--12 could not compile the full `-fsanitize=address,undefined`
  build: the `null`, `nonnull-attribute` and `returns-nonnull-attribute`
  checks (like `-fno-delete-null-pointer-checks` alone) stop GCC treating an
  object's address as non-null in constant evaluation, so
  `static_assert(find_field(Id) != nullptr)` in `field_factory.hpp` was not a
  constant expression. Fixed by `is_field_registered()`, a by-value check;
  full ASan+UBSan now passes 35/35 on GCC 11, GCC 12 and Clang 14.
- M2-02 terrain, Clang 14: Debug and Release 35/35 with warnings as errors,
  floating-point policy 0 violations; full ASan+UBSan (RelWithDebInfo, as in
  CI, run under `setarch -R`) 35/35.
- L6 `earth_like` terrain generation, Release: 220 ms with 24 workers,
  1.35 s with one worker (task M2-02 A13 threshold 10 s).
- M2-04 scheduler and calendar (2026-09-29): GCC Debug and Release, Clang
  Release and Clang ASan+UBSan (under `setarch -R`) 41/41; floating-point
  policy 0 violations in 62 translation units; registry check passed.
- Normal headless suite before M2-02: 26/26 tests passed.
- AddressSanitizer/UndefinedBehaviorSanitizer suite: 26/26 tests passed
  (Clang, 2026-09-28).
- Godot 4.7.2 extension build and five-frame headless runtime smoke test:
  passed without errors.
- L6 mesh: 40,962 cells, 12 pentagons, valid topology, zero reported relative
  area error, and 13,764,040 geometry/topology bytes.
- L5 solstice forcing: zero night-side leakage and relative global-mean
  quadrature error approximately `1.81e-5`.
- Release L6 persistent snapshot: 1,475,228 bytes and about 6 ms to write on
  the development machine, including the mesh checksum; the timing is
  reported, not gated.
- `PlanetState` retains a shared immutable mesh handle and owns the slow,
  fast (lazy) and climatology partitions plus derived forcing.
- The mesh is body-fixed with geographic north on `+Z`; physical rotation and
  a fixed Keplerian orbit are derived from simulation time and planet/star
  parameters.
- Latitude, longitude, and stable right-handed local East/North/Up bases are
  derived from authoritative 3D surface normals.
- `ForcingState` owns cell-centered top-of-atmosphere insolation in `W/m²`.
- The authoritative clock is a signed 64-bit count of one-minute ticks;
  physical seconds are derived, never accumulated.
- Fixed logical blocks, fixed block-index reductions, keyed random streams, and
  disabled floating-point contraction support same-build worker-count
  determinism.
- `StateSnapshot` has an explicit schema version and stable field ID, and
  copies read-oriented orbital and forcing data across the client boundary.
- The Godot target depends on `PlanetSim`; the dependency never points in the
  other direction.
