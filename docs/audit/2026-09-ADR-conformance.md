# ADR conformance audit — M0/M1 migration

- **Audit date:** 2026-09-25
- **Repository state:** commit `8425591`
- **Plan audited:** `docs/M0-M1-ADR-migration-plan.md`
- **Scope:** completed M0/M1 migration against ADR-0001, ADR-0002, and
  ADR-0003

## Conclusion

The repository satisfies the actual M0 and M1 milestone acceptance criteria,
but it does not literally complete the whole migration plan. The plan mixes
M0/M1 requirements with work assigned by the accepted ADRs to M2 through M5.
Its status should therefore be read as "M0/M1 subset complete," not "all
phases complete."

No M2 implementation is required to close M0/M1. The remaining items should
either be retained as an explicitly labelled future backlog or moved to the
milestone where their governing ADR assigns them.

## Markers

| Marker | Meaning |
|---|---|
| ✅ EVIDENCED | Implemented and supported by a test, type, or direct source evidence |
| ⚠ RUNTIME_ONLY | Partially implemented, behavior-only, or missing a required enforcement mechanism |
| ❌ VIOLATION | The implementation or plan contradicts the stated requirement |
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

Additional Phase 2 requirements that are not yet applicable: pentagons are
not excluded from operator convergence statistics and cannot yet be forbidden
as scenario anchors because neither operator convergence infrastructure nor
scenario anchors exist. Those protections must be added with their consuming
systems.

## B. Field layout and parallelism

| ID | Status | Evidence and finding |
|---|---|---|
| B1 | ✅ EVIDENCED | Each field uses one contiguous allocation through a 64-byte-aligned allocator (`sim/core/fields/field.hpp:15-101`). |
| B2 | ✅ EVIDENCED | `Field3D` indexes as `layer * cell_count + cell`, and layer spans are contiguous (`sim/core/fields/field.hpp:105-166`). |
| B3 | ⚠ RUNTIME_ONLY | The current diagnostic insolation field is `float` and solar/global reductions use `double`. No prognostic fields or slow ocean/carbon reservoirs exist yet, so their precision policy cannot yet be enforced. |
| B4 | ✅ EVIDENCED | Cell IDs follow first encounter in the recursively ordered final faces, before fields are allocated (`sim/planet/mesh/icosphere.cpp:173-223`). This achieves the required order without a separate permutation pass. |
| B5 | ✅ EVIDENCED | The mesh stores fixed 256-cell logical blocks independent of worker count (`sim/planet/mesh/planet_mesh.hpp:71-76`, `sim/planet/mesh/icosphere.cpp:103-112`). |
| B6 | ❌ VIOLATION | The plan still requires a fixed reduction tree. The implementation merges block partials sequentially in fixed block-index order (`sim/planet/orbit/solar_diagnostics.cpp:80-98`). This remains deterministic and matches the revised accepted ADR-0002, so the plan is stale rather than the implementation being numerically nondeterministic. |
| B7 | ⚠ RUNTIME_ONLY | `/fp:strict` or `-ffp-contract=off` is configured and fast-math is not enabled (`cmake/CompilerWarnings.cmake:1-22`). There is no CI job that detects removed flags or externally enabled fast-math. |

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
M1 acceptance, but they do prevent Phase 4/G4 of this migration plan from
being marked complete.

## D. Clock, RNG, and identity

| ID | Status | Evidence and finding |
|---|---|---|
| D1 | ✅ EVIDENCED | `SimulationTick` is `std::int64_t`, one tick is 60 seconds, and seconds are derived rather than accumulated (`sim/core/scheduler/simulation_clock.hpp:7-24`). |
| D2 | ✅ EVIDENCED | Random values are stateless and keyed by seed, stream, tick, cell, and sample index; tests compare 1-worker and 16-worker output (`sim/core/random/counter_rng.hpp`, `tests/unit/test_clock_parameters.cpp:45-74`). No global mutable generator was found. |
| D3 | ✅ EVIDENCED | The registry contains a stable numeric field ID and a compile-time uniqueness assertion (`sim/core/fields/field_registry.hpp:10-50`). |
| D4 | ⚠ RUNTIME_ONLY | The presentation snapshot copies cells in mesh order and includes the field ID (`sim/core/serialization/state_snapshot.cpp:23-27`). A generic serializer that orders multiple fields by ID does not exist; ADR-0003 assigns persistent serialization to M2. |

Literal Phase 1 differences remain: `SimulationTick` is an alias rather than
the requested strong `Tick` struct; the registry is handwritten rather than
generated and has no layer count or migration initializer; and the requested
`// [ADR...]` source markers were not added. These do not violate the accepted
M1 ADR guarantees, but they mean Phase 1 was not followed word-for-word.

## Gate status

| Gate | Status | Finding |
|---|---|---|
| Phase 0 audit | ❌ VIOLATION | This file was missing when the migration was marked complete. This audit supplies it, but it still needs to be committed. |
| G1 | ⚠ RUNTIME_ONLY | Functional integer time, keyed RNG, and stable IDs are complete; the strong tick type and full generated registry contract are not. |
| G2 | ⚠ RUNTIME_ONLY | The M0 portion—V1/V2 dual-mesh geometry—is green. V3 operators and an error-map artifact are M2 work and are absent. |
| G3 | ⚠ RUNTIME_ONLY | Field layout, fixed blocks, forcing/reduction equality at 1/2/8/16 workers, memory gates, and V7 are green. Full command replay with checkpoint state hashes and the CI compiler-flags gate are absent. |
| G4 | NOT FOUND | State partitions, simulation modes, multi-rate scheduling, years-per-minute harness, and state hashes are deferred. |
| G5 | NOT FOUND | Persistent JSON/zstd snapshots, checksums, manifests, replay, migrations, corruption tests, and golden saves are deferred to M2–M5. |

The plan says no phase begins until the previous gate is green, but Phase 3
was completed while G2's M2-only V3 requirement remained open. The execution
was consistent with the ADR milestone mapping; the plan's gate sequencing was
not.

## Risk-register audit

| Risk mitigation | Status | Finding |
|---|---|---|
| Migrate before scenarios exist | ✅ EVIDENCED | No scenario data or scenario anchors exist to invalidate. |
| Avoid hard-coded semantic cell IDs | ✅ EVIDENCED | No scenario or geographic tests depend on fixed cell IDs. Literal IDs remain only in generic field-container indexing tests. |
| Treat determinism flakes as blockers | ⚠ RUNTIME_ONLY | Exact worker-count equality is tested, but no CI policy or retry detector enforces the stated process rule. |
| Prevent field-ID reuse | ⚠ RUNTIME_ONLY | Compile-time uniqueness exists within the current registry, but no CI comparison against the previous release exists. |
| Bound benchmark noise/regressions | ⚠ RUNTIME_ONLY | Absolute measurements are recorded, but no greater-than-20-percent regression assertion exists. |

## Documentation corrections recommended

1. Change the plan status to **"M0/M1 subset complete; overall plan in
   progress."**
2. Replace "M1 field containers / scheduler skeleton" with the actual M1
   scope: orbit, sun, day/night, and seasons.
3. Split G2 into an M0 V1/V2 gate and an M2 V3/V4 operator gate.
4. Split G3's current field/forcing determinism tests from M3 command replay
   and checkpoint hashing.
5. Update B6 from "fixed reduction tree" to the accepted fixed block-index
   merge order.
6. Keep G4 and G5 visibly deferred instead of treating them as part of the
   completed migration.

## Validation observed during the migration

- Normal headless suite: 12/12 tests passed.
- AddressSanitizer/UndefinedBehaviorSanitizer suite: 12/12 tests passed.
- Godot 4.7.2 extension build and five-frame headless runtime smoke test:
  passed without errors.
- L6 mesh: 40,962 cells, 12 pentagons, valid topology, zero reported relative
  area error, and 13,764,040 geometry/topology bytes.
- L5 solstice forcing: zero night-side leakage and relative global-mean
  quadrature error approximately `1.81e-5`.
