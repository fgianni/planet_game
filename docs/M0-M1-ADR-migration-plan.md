# M0/M1 → ADR-0001/0002/0003 conformance migration

- **Status:** M0/M1 scope implemented and validated; later-milestone items deferred
- **Date:** 2026-09-23
- **Applies to:** PlanetSim core after milestones M0 (mesh) and M1 (field containers / scheduler skeleton)
- **Governing records:** ADR-0001 (time acceleration, modes, budget), ADR-0002 (mesh, resolution, field layout), ADR-0003 (determinism, snapshots, migration)
- **Method:** read-only audit first, then phased migration with hard gates between phases. No phase starts until the previous gate is green.

> **Why now:** no player histories exist yet, so cell definition, field identity and clock representation can still change for free. Every one of these becomes expensive after the first save format ships.

## Execution status (2026-09-25)

- G1 is complete: integer ticks, a stable field registry, and keyed random
  streams are implemented and tested.
- The M0 portion of G2 is complete: the dual mesh passes V1/V2 through L6.
  Operator implementation and V3 belong to M2 under ADR-0002 and were not
  pulled forward.
- G3 is complete for the M1 state: aligned field containers, fixed logical
  blocks, fixed-order solar reductions, compiler flags, 1/2/8/16-worker
  bit-identity, memory gates, and the V7 benchmark are in place.
- G4 state partitions/mode scheduling and G5 persistent snapshots are later
  milestone work. They were deliberately not started because the active task
  stops before M2.

---

## Phase 0 — Read-only conformance audit (no code changes)

Produce `docs/audit/2026-09-ADR-conformance.md`. Every line gets one marker:

| Marker | Meaning |
|---|---|
| ✅ EVIDENCED | Requirement met, with file:line evidence |
| ⚠ RUNTIME_ONLY | Behaviour looks right but is not enforced by a test or type |
| ❌ VIOLATION | Requirement contradicted by current code |
| NOT FOUND | Requirement not implemented at all |

### A. Mesh and geometry (ADR-0002 §4.1–4.2)

| # | Requirement | Evidence to find |
|---|---|---|
| A1 | Cells are the **hexagonal/pentagonal dual** (vertex-centred), not triangles | cell count at level 5 == 10,242; exactly 12 cells with 5 neighbours |
| A2 | Per-cell geometry precomputed: area, centroid, local east/north basis | struct definition + build site |
| A3 | Per-edge geometry: neighbour index, edge length, centroid distance, outward normal | CSR arrays, built once |
| A4 | Pentagons tagged and queryable | flag array or predicate |
| A5 | Divergence implemented as edge-flux sum (conservative by construction) | operator source |
| A6 | Geometry closure test Σ area = 4πR² (V1), topology test (V2) | test files |

### B. Field layout and parallelism (ADR-0002 §4.4–4.6)

| # | Requirement | Evidence to find |
|---|---|---|
| B1 | Structure of arrays; one contiguous 64-byte-aligned allocation per field | allocator / container |
| B2 | 3-D fields are **layer-major** `[layer][cell]` | container indexing |
| B3 | `float32` for prognostic fields, `float64` for accumulators and slow reservoirs | field declarations |
| B4 | Cells ordered by a space-filling order derived from parent triangles, not construction order | ordering pass |
| B5 | Fixed block partitioning independent of thread count | partition code |
| B6 | No atomics in reductions; fixed-order reduction tree | reduction helpers |
| B7 | Build flags: `-ffp-contract=off`, no fast-math | CMake |

### C. State partition and modes (ADR-0001 §4.1–4.2)

| # | Requirement | Evidence to find |
|---|---|---|
| C1 | Fields grouped into `SlowState` / `FastState` / `Climatology` | type or registry grouping |
| C2 | `FastState` allocated only when a reference run or weather window is active | allocation site |
| C3 | Mode enum exists and the scheduler dispatches on it | scheduler |
| C4 | No wall-clock input to the solver (no frame time in stepping decisions) | grep for timing APIs in core |

### D. Clock, RNG, identity (ADR-0003 §3.2–3.4)

| # | Requirement | Evidence to find |
|---|---|---|
| D1 | Simulated time is `int64` ticks (1 tick = 1 minute); no accumulated `double` seconds | clock type |
| D2 | RNG streams keyed by `(seed, stream, tick, cell)`; no global generator | RNG code; grep for `rand`, `mt19937` singletons |
| D3 | Field **registry** with stable numeric ids, compile-time uniqueness check | registry file |
| D4 | Canonical iteration order defined (cells in mesh order, fields by id) | serializer or documented invariant |

---

## Phase 1 — Identity and time (blocking, do first)

These change the meaning of everything downstream, so they come before any physics work.

1. **Integer clock.** `struct Tick { int64_t value; }` with explicit conversions; simulated durations in ticks. Remove every `double seconds` accumulator from the core. Mark call sites `// [ADR3-CLOCK]`.
2. **Field registry.** Single generated header: `field_id`, name, kind (slow/fast/derived), dtype, layers, initialiser for migration. Compile-time uniqueness assertion. Mark `// [ADR3-REGISTRY]`.
3. **RNG discipline.** One counter-based generator (Philox or similar), keyed as above, one stream id per subsystem. Delete any shared mutable generator. Mark `// [ADR3-RNG]`.

**Gate G1:** `tick` type used throughout the core; registry generates without duplicate ids; a test draws the same random sequence for a given `(seed, stream, tick, cell)` regardless of call order and thread count.

## Phase 2 — Mesh conformance

1. If A1 is ❌ (cells are triangles), build the **dual** and switch cell identity: cell = vertex of the triangular mesh; corners = triangle centroids. Keep the triangular mesh only as construction scaffolding.
2. Precompute and store geometry (A2, A3); forbid recomputation inside kernels.
3. Tag pentagons; add `is_pentagon(cell)`; exclude them from convergence statistics and forbid scenario anchors on them.
4. Apply the space-filling cell ordering (B4) as a permutation pass after construction, before any field is allocated.

**Gate G2:** V1 (area closure < 1e-12 relative), V2 (neighbour symmetry, exactly 12 pentagons, every edge shared by two cells), V3 operator accuracy on spherical harmonics with no visible icosahedral structure in the error map. Publish the error map as a CI artefact — this is the test that decides whether the mesh is good.

## Phase 3 — Field containers and determinism

1. Convert containers to SoA, aligned, layer-major (B1–B3).
2. Introduce block partitioning fixed at load (B5) and the fixed-order reduction tree (B6).
3. Pin build flags (B7); add a CI job that fails if fast-math or FP contraction is enabled.
4. Add the **determinism test** (ADR-0003 V1): same seed and command list under 1, 2, 8, 16 threads → bit-identical state hash at every checkpoint.

**Gate G3:** determinism test green; V7 bandwidth micro-benchmark recorded (ordered vs naive cell order), with the measured gain written into the ADR-0002 record.

## Phase 4 — State partition and scheduler skeleton

1. Group fields per C1 using the registry's `kind`; make `FastState` allocation lazy (C2).
2. Scheduler: mode enum, multi-rate stepping, snapshot cadence decoupled from solver cadence, all stepping decisions a function of state and ticks only (C4).
3. Add the performance harness now, even with placeholder physics: simulated years per minute, printed per run and asserted loosely in CI (ADR-0001 §5). An early loose gate catches regressions before they compound.

**Gate G4:** a headless run advances N simulated years with no physics, reports years/minute, and produces identical hashes across thread counts.

## Phase 5 — Snapshot and manifest skeleton

1. Snapshot writer/reader per ADR-0003 §3.4: JSON manifest + zstd chunks, ids not names, per-chunk checksums, canonical order.
2. Run manifest with command log and per-year `state_hash`.
3. `schema_version = 1`; migration framework present but empty; golden-save corpus started with the first v1 save.

**Gate G5:** V4 (snapshot → load → snapshot byte-identical), V2 of ADR-0003 (replay from manifest matches hashes), V8 (bit-flip detected per chunk, no crash).

---

## What NOT to do in this migration

- No physics changes. Radiation, moisture and ocean work resumes after G4.
- No delta chains or forks yet (ADR-0003 §3.5) — they belong at M4, once snapshots are stable.
- No resolution promotion to level 6. Development stays at level 5 until the performance harness exists.
- No ML surrogate, no variable resolution, no cross-platform determinism.

## Suggested task framing for Codex

Give it one phase at a time, with the gate as the acceptance criterion:

```
Read docs/adr/ADR-0002 §4.1–4.2 and docs/audit/2026-09-ADR-conformance.md section A.
Task: Phase 2, steps 1–3 only. Do not modify field containers or physics.
Mark every changed or added site with // [ADR2-MESH].
Acceptance: tests V1, V2, V3 pass; V3 publishes an error map artefact;
level 5 reports exactly 10,242 cells and 12 pentagons.
Report: files touched, tests added, any requirement you could not meet and why.
```

Keep the audit file updated as each ❌ or NOT FOUND is cleared, with the commit that cleared it. It becomes the evidence trail for the P0 review.

## Risk register for this migration

| Risk | Mitigation |
|---|---|
| Dual-mesh switch invalidates any early scenario data | Do it now, before scenarios are authored; regenerate from source elevation data |
| Space-filling reorder breaks hard-coded cell indices in tests | Forbid literal cell indices in tests; address cells by coordinates and resolve at runtime |
| Determinism test is flaky rather than failing | Treat any flake as a blocker, not a retry: it means an unkeyed RNG or an unordered reduction remains |
| Registry ids get renumbered during review | Ids are append-only from the first commit of the registry; a CI test diffs against the previous release |
| Performance harness gives noisy numbers on a laptop | Assert only large regressions (> 20 %); record absolute numbers on one reference machine |
