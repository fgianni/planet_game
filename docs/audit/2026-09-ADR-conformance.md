# ADR conformance audit — M0/M1 migration

- **Audit date:** 2026-09-28
- **Evidence baseline:** commit `6e91459`, plus the documentation consolidation
  in the current working tree
- **Plan audited:** [M0/M1 ADR migration plan](../M0-M1-ADR-migration-plan.md)
- **Scope:** completed M0/M1 migration against
  [ADR-0001](../decisions/0001-time-acceleration.md),
  [ADR-0002](../decisions/0002-mesh-and-field-layout.md), and
  [ADR-0003](../decisions/0003-determinism-snapshots-migration.md)

## Conclusion

The repository satisfies the actual M0 and M1 milestone acceptance criteria.
The migration plan now distinguishes the completed M0/M1 subset from work that
the accepted ADRs assign to M2 through M5. No M2 implementation is required to
close M0/M1, and none was added by the documentation cleanup.

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
| A5 | NOT FOUND | Divergence and the finite-volume operator library are not implemented. ADR-0002 assigns them and V3/V4 to M2. |
| A6 | ✅ EVIDENCED | Area closure and exact topology are tested from L0 through L6 (`tests/conservation/test_mesh_area.cpp`, `tests/physics/test_icosphere.cpp`). |

Pentagons are not yet excluded from operator convergence statistics or scenario
anchors because neither consuming system exists. Add those protections with
the M2 operator and later scenario work.

## B. Field layout and parallelism

| ID | Status | Evidence and finding |
|---|---|---|
| B1 | ✅ EVIDENCED | Each field uses one contiguous allocation through a 64-byte-aligned allocator (`sim/core/fields/field.hpp:15-101`). |
| B2 | ✅ EVIDENCED | `Field3D` indexes as `layer * cell_count + cell`, and layer spans are contiguous (`sim/core/fields/field.hpp:105-166`). |
| B3 | ⚠ RUNTIME_ONLY | The current diagnostic insolation field is `float` and solar/global reductions use `double`. No prognostic fields or slow ocean/carbon reservoirs exist yet, so their precision policy cannot yet be enforced. |
| B4 | ✅ EVIDENCED | Cell IDs follow first encounter in the recursively ordered final faces, before fields are allocated (`sim/planet/mesh/icosphere.cpp:173-223`). This achieves the required locality without a separate permutation pass. |
| B5 | ✅ EVIDENCED | The mesh stores fixed 256-cell logical blocks independent of worker count (`sim/planet/mesh/planet_mesh.hpp:71-76`, `sim/planet/mesh/icosphere.cpp:103-112`). |
| B6 | ✅ EVIDENCED | Solar reductions compute one partial per fixed logical block and merge partials sequentially in block-index order, with no atomics (`sim/planet/orbit/solar_diagnostics.cpp:80-98`). This matches the accepted ADR-0002. |
| B7 | ✅ EVIDENCED | `/fp:strict` or `-ffp-contract=off` is configured and fast-math is not enabled (`cmake/CompilerWarnings.cmake:1-22`). Every CI build job runs `tools/ci/check_fp_flags.py` against `compile_commands.json`, which fails on a missing effective `-ffp-contract=off` or any fast-math flag, including flags injected through `CMAKE_CXX_FLAGS` (`.github/workflows/ci.yml`). MSVC is not covered because CI does not build with it. |

The V7 benchmark exists in `planet_cli`, verifies ordered and naive kernels
produce identical values, and records timing. ADR-0002 records five optimized
L6 runs with a median naive-to-ordered ratio of 1.028. L5/L6 geometry memory
budgets are enforced by tests.

## C. State partition and modes

| ID | Status | Evidence and finding |
|---|---|---|
| C1 | NOT FOUND | `SlowState`, `FastState`, and `Climatology` partitions do not exist. ADR-0001 permits this work through M2. |
| C2 | NOT FOUND | There is no lazy `FastState` allocation because `FastState` does not exist yet. |
| C3 | NOT FOUND | No simulation-mode enum or multi-rate scheduler dispatch exists. |
| C4 | ✅ EVIDENCED | The simulation core has no wall-clock timing dependency. Timing calls are confined to the CLI benchmark/diagnostics and Godot presentation. |

The development specification defines M1 as orbit, sun, day/night, and
seasons—not as the completed mode scheduler. Consequently C1–C3 do not block
M1 acceptance.

## D. Clock, RNG, and identity

| ID | Status | Evidence and finding |
|---|---|---|
| D1 | ✅ EVIDENCED | `SimulationTick` is `std::int64_t`, one tick is 60 seconds, and seconds are derived rather than accumulated (`sim/core/scheduler/simulation_clock.hpp:7-24`). |
| D2 | ✅ EVIDENCED | Random values are stateless and keyed by seed, stream, tick, cell, and sample index; tests compare 1-worker and 16-worker output (`sim/core/random/counter_rng.hpp`, `tests/unit/test_clock_parameters.cpp:45-74`). No global mutable generator was found. |
| D3 | ✅ EVIDENCED | The registry contains a stable numeric field ID and a compile-time uniqueness assertion (`sim/core/fields/field_registry.hpp:10-50`). |
| D4 | ⚠ RUNTIME_ONLY | The presentation snapshot copies cells in mesh order and includes the field ID (`sim/core/serialization/state_snapshot.cpp:23-27`). A generic persistent serializer that orders multiple fields by ID does not exist; ADR-0003 assigns it to M2. |

The accepted M1 guarantees do not require `SimulationTick` to be a strong
wrapper type or the small registry to be generated. Field kind, layer count,
migration initializers, and release-to-release append-only enforcement become
necessary with the persistent schema and additional field groups.

## Gate status

| Gate | Status | Finding |
|---|---|---|
| Phase 0 audit | ✅ EVIDENCED | The audit was committed at `6e91459` and is maintained here as the current evidence trail. |
| G1-M1 | ✅ EVIDENCED | Integer time, keyed RNG, stable field IDs, uniqueness checks, and worker-independent RNG tests are complete. |
| G2-M0 | ✅ EVIDENCED | V1/V2 dual-mesh geometry and topology are green through L6. |
| G2-M2 | NOT FOUND | V3/V4 finite-volume operators and the error-map artifact are correctly deferred to M2. |
| G3-M1 | ✅ EVIDENCED | Field layout, fixed blocks, fixed block-index reductions, 1/2/8/16-worker forcing equality, memory gates, and V7 are green. |
| G3-M3 | NOT FOUND | Full command replay and checkpoint state hashes are correctly deferred to M3. |
| G4 | NOT FOUND | State partitions, simulation modes, multi-rate scheduling, years-per-minute harness, and state hashes are later work. |
| G5 | NOT FOUND | Persistent snapshots, manifests, replay, delta chains, migrations, corruption tests, and golden saves remain assigned to M2–M5. |

## Risk-register audit

| Risk mitigation | Status | Finding |
|---|---|---|
| Migrate before scenarios exist | ✅ EVIDENCED | No scenario data or scenario anchors exist to invalidate. |
| Avoid hard-coded semantic cell IDs | ✅ EVIDENCED | No scenario or geographic tests depend on fixed cell IDs. Literal IDs remain only in generic field-container indexing tests. |
| Treat determinism flakes as blockers | ⚠ RUNTIME_ONLY | Exact worker-count equality is tested, but no CI policy or retry detector enforces the process rule. |
| Prevent field-ID reuse | ⚠ RUNTIME_ONLY | Compile-time uniqueness exists within the current registry, but no release-to-release CI comparison exists. |
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

## Validation observed during the migration

- Normal headless suite: 12/12 tests passed.
- AddressSanitizer/UndefinedBehaviorSanitizer suite: 12/12 tests passed.
- Godot 4.7.2 extension build and five-frame headless runtime smoke test:
  passed without errors.
- L6 mesh: 40,962 cells, 12 pentagons, valid topology, zero reported relative
  area error, and 13,764,040 geometry/topology bytes.
- L5 solstice forcing: zero night-side leakage and relative global-mean
  quadrature error approximately `1.81e-5`.
- `PlanetState` retains a shared immutable mesh handle and owns evolving
  fields.
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
