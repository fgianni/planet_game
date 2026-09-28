# ADR conformance audit — M0/M1 migration and M2 infrastructure

- **Audit date:** 2026-09-28
- **Evidence baseline:** M0/M1 migration at commit `6e91459`, plus the G2-M2
  operator and M2 state/snapshot implementation through 2026-09-28
- **Plan audited:** [M0/M1 ADR migration plan](../M0-M1-ADR-migration-plan.md)
- **Scope:** completed M0/M1 migration and implemented M2 infrastructure against
  [ADR-0001](../decisions/0001-time-acceleration.md),
  [ADR-0002](../decisions/0002-mesh-and-field-layout.md), and
  [ADR-0003](../decisions/0003-determinism-snapshots-migration.md)

## Conclusion

The repository satisfies the actual M0 and M1 milestone acceptance criteria.
M2 remains in progress: the finite-volume operator gate, state partition, and
base persistent snapshot format are implemented. Geology, terrain generation,
and the simulation-mode scheduler have not started; later persistence work
remains assigned to M3 through M5.

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
| C3 | NOT FOUND | No simulation-mode enum or multi-rate scheduler dispatch exists. |
| C4 | ✅ EVIDENCED | The simulation core has no wall-clock timing dependency. Timing calls are confined to the CLI benchmark/diagnostics and Godot presentation. |

The development specification defines M1 as orbit, sun, day/night, and
seasons—not as the completed mode scheduler. C1 and C2 are now present for M2;
C3 remains a separate future M2 task and does not block M1 acceptance.

## D. Clock, RNG, and identity

| ID | Status | Evidence and finding |
|---|---|---|
| D1 | ✅ EVIDENCED | `SimulationTick` is `std::int64_t`, one tick is 60 seconds, and seconds are derived rather than accumulated (`sim/core/scheduler/simulation_clock.hpp:7-24`). |
| D2 | ✅ EVIDENCED | Random values are stateless and keyed by seed, stream, tick, cell, and sample index; tests compare 1-worker and 16-worker output (`sim/core/random/counter_rng.hpp`, `tests/unit/test_clock_parameters.cpp:45-74`). Eight exact golden vectors cover every key argument, all streams, negative and positive ticks, and zero/maximum values (`tests/unit/test_counter_rng.cpp`); changing them requires an ADR because it invalidates runs, snapshots, and replay. No global mutable generator was found. |
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
| G3-M3 | NOT FOUND | Full command replay and checkpoint state hashes are correctly deferred to M3. |
| G4 | ⚠ RUNTIME_ONLY | The slow/fast/climatology state partition and lazy fast-state lifecycle are implemented. Simulation modes, multi-rate scheduling, the years-per-minute harness, and state hashes remain future work. |
| G5 | ⚠ RUNTIME_ONLY | The M2 base snapshot writer/reader, canonical ordering, CRC-32C validation, corruption tests, V4 round trips, CLI inspection, and initial golden save are complete. Manifests/replay (M3), delta chains/compression (M4), and migration machinery (M5) are not yet implemented. |

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

Versioned specifications and design documents were deliberately not renamed or
deduplicated in this pass; they are source/history artifacts rather than
competing current audits or decisions.

## Validation observed during the migration and M2 infrastructure work

- Normal headless suite: 26/26 tests passed.
- AddressSanitizer/UndefinedBehaviorSanitizer suite: 22/22 tests passed
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
