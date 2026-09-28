# M0/M1 → ADR-0001/0002/0003 conformance migration

- **Status:** M0/M1 subset complete; overall plan in progress
- **Date:** 2026-09-23
- **Updated:** 2026-09-28
- **Applies to:** the M0 dual mesh, the M1 orbit/sun/day-night/seasons work,
  and the ADR prerequisites assigned to those milestones
- **Governing records:** [ADR-0001](decisions/0001-time-acceleration.md),
  [ADR-0002](decisions/0002-mesh-and-field-layout.md), and
  [ADR-0003](decisions/0003-determinism-snapshots-migration.md)
- **Method:** read-only audit first, then milestone-scoped implementation and
  validation gates. Future-milestone gates do not block completion of M0/M1.

> **Why now:** no player histories exist yet, so cell definition, field identity
> and clock representation can still change for free. Every one of these becomes
> expensive after the first persistent save format ships.

## Execution status (2026-09-28)

- G1-M1 is complete: integer ticks, a stable field registry, and keyed random
  streams are implemented and tested.
- G2-M0 is complete: the dual mesh passes V1/V2 through L6. G2-M2 (finite-
  volume operators and V3/V4) remains future work.
- G3-M1 is complete: aligned field containers, fixed logical blocks, fixed
  block-index solar reductions, compiler flags, 1/2/8/16-worker bit identity,
  memory gates, and the V7 benchmark are in place. Command replay and
  checkpoint hashes remain assigned to M3 by ADR-0003.
- G4 state partitions/mode scheduling and G5 persistent snapshots are later
  milestone work. They were deliberately not pulled into M1.

---

## Phase 0 — Read-only conformance audit (complete)

The current evidence trail is
[docs/audit/2026-09-ADR-conformance.md](audit/2026-09-ADR-conformance.md).
Every finding uses one of these markers:

| Marker | Meaning |
|---|---|
| ✅ EVIDENCED | Requirement met, with file evidence |
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
| B6 | No atomics in reductions; partials merged in fixed block-index order | reduction helpers |
| B7 | Build flags: `-ffp-contract=off`, no fast-math | CMake |

### C. State partition and modes (ADR-0001 §4.1–4.2)

| # | Requirement | Evidence to find |
|---|---|---|
| C1 | Fields grouped into `SlowState` / `FastState` / `Climatology` | type or registry grouping |
| C2 | `FastState` allocated only when a reference run or weather window is active | allocation site |
| C3 | Mode enum exists and the scheduler dispatches on it | scheduler |
| C4 | No wall-clock input to the solver (no frame time in stepping decisions) | timing API search in core |

### D. Clock, RNG, identity (ADR-0003 §3.2–3.4)

| # | Requirement | Evidence to find |
|---|---|---|
| D1 | Simulated time is `int64` ticks (1 tick = 1 minute); no accumulated `double` seconds | clock type |
| D2 | RNG streams keyed by `(seed, stream, tick, cell)`; no global generator | RNG code; search for shared generators |
| D3 | Field **registry** with stable numeric IDs and compile-time uniqueness check | registry file |
| D4 | Canonical iteration order defined (cells in mesh order, fields by ID) | serializer or documented invariant |

## Phase 1 — Identity and time (M1, complete)

These define the meaning of downstream state and therefore precede physics.

1. **Integer clock.** Use signed 64-bit `SimulationTick` values with explicit
   conversion to physical seconds. Do not accumulate simulated time in a
   floating-point seconds counter.
2. **Field registry.** Maintain stable numeric field IDs and enforce uniqueness
   at compile time. Add field kind, dtype, layer count, and migration metadata
   when their consuming systems are introduced.
3. **RNG discipline.** Use a stateless counter-based generator keyed by seed,
   stream, tick, cell, and sample. Do not introduce a shared mutable generator.

**Gate G1-M1:** integer ticks are authoritative; field IDs are unique and
stable; a test draws the same random sequence for a given key regardless of
call order and worker count. **Status: complete.**

## Phase 2 — Mesh conformance (M0 complete; M2 operators deferred)

1. Build the **dual**: cell = vertex of the triangular mesh; corners = triangle
   centroids. Keep the triangular mesh only as construction scaffolding.
2. Precompute and store geometry (A2, A3); forbid recomputation inside kernels.
3. Tag pentagons and expose `is_pentagon(cell)`. Exclude them from convergence
   statistics and scenario anchors when those M2+ consumers are implemented.
4. Establish space-filling locality before fields are allocated. The accepted
   implementation derives cell order from recursively ordered final faces;
   it does not require a separate permutation pass.

**Gate G2-M0:** V1 (area closure below `1e-12` relative) and V2 (neighbour
symmetry, exactly 12 pentagons, every edge shared by two cells). **Status:
complete through L6.**

**Gate G2-M2:** V3/V4 operator accuracy and conservation, including the
spherical-harmonic error map as a CI artifact. **Status: future work; do not
start as part of this migration.**

## Phase 3 — Field containers and M1 determinism (complete)

1. Use SoA, aligned, layer-major containers (B1–B3).
2. Use fixed logical blocks (B5) and merge block partials sequentially in fixed
   block-index order (B6), independent of worker count.
3. Pin build flags (B7). Add CI enforcement when the CI configuration is
   introduced; until then, keep direct build/test evidence in the audit.
4. Test M1 forcing, diagnostics, and keyed RNG under 1, 2, 8, and 16 workers
   for bit-identical results. Full command replay and checkpoint state hashes
   remain M3 work under ADR-0003.

**Gate G3-M1:** worker-count identity is green for implemented M1 operations;
the V7 ordered-versus-naive benchmark and memory gates are recorded in
ADR-0002. **Status: complete.**

## Phase 4 — State partition and scheduler skeleton (M2/M3, deferred)

1. Group fields per C1 using the registry's `kind`; make `FastState`
   allocation lazy (C2).
2. Add the mode enum, multi-rate scheduler, and snapshot cadence decoupled from
   solver cadence. All stepping decisions must be functions of state and ticks.
3. At M3, add the performance harness: simulated years per minute, printed per
   run and asserted loosely in CI (ADR-0001 §5).

**Gate G4:** a headless run advances N simulated years with no physics, reports
years/minute, and produces identical hashes across worker counts.

## Phase 5 — Persistence, replay, and migration (M2–M5, deferred)

1. **M2:** persistent snapshot writer/reader per ADR-0003 §3.4: JSON metadata
   plus zstd chunks, IDs rather than names, per-chunk checksums, and canonical
   order. Its schema version is independent of the in-process presentation
   snapshot version.
2. **M3:** run manifest, command log, checkpoint hashes, replay V1/V2.
3. **M4:** delta chains and fork storage; measure compression and timing.
4. **M5:** migration framework and first persistent-format golden saves.

**Gate G5:** apply the ADR-0003 validation assigned to each milestone: V4 and
V8 at M2, V1/V2 at M3, V6/V7 at M4, and V5 at M5.

---

## What NOT to do in this migration

- No physics changes.
- No delta chains or forks before M4.
- No promotion of L6 to the normal simulation resolution. L5 remains the
  development default; L6 remains available for validation and reference
  measurements.
- No ML surrogate, variable resolution, or cross-platform determinism.

## Suggested task framing for the next applicable phase

Give Codex one milestone-scoped gate at a time. For example, when M2 is
explicitly authorized:

```text
Read docs/decisions/0002-mesh-and-field-layout.md §4.2 and
docs/audit/2026-09-ADR-conformance.md section A.
Task: G2-M2 finite-volume operators only. Do not implement terrain or later physics.
Acceptance: V3 and V4 pass; V3 publishes an error-map artifact.
Report: files touched, tests added, any requirement you could not meet and why.
```

Keep the current audit updated as each finding is cleared, with the commit that
cleared it. It remains the evidence trail for the P0 review.

## Risk register for this migration

| Risk | Mitigation |
|---|---|
| Dual-mesh switch invalidates any early scenario data | Do it before scenarios are authored; regenerate from source elevation data |
| Space-filling reorder breaks hard-coded cell indices in tests | Forbid literal semantic cell IDs in tests; address geographic cells by coordinates and resolve at runtime |
| Determinism test is flaky rather than failing | Treat any flake as a blocker, not a retry: it means an unkeyed RNG or unordered reduction remains |
| Registry IDs get renumbered during review | IDs are append-only from the first registry commit; add release-to-release CI enforcement with the persistent schema |
| Performance harness gives noisy numbers on a laptop | Assert only large regressions (> 20%); record absolute numbers on one reference machine |
