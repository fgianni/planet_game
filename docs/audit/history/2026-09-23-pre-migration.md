# Phase 0 audit — `fgianni/planet_game` @ main (7 commits, P0/M1) against ADR-0001/0002/0003

> **Historical record.** This audit describes the repository before the
> dual-mesh and deterministic-clock migration, approximately at commit
> `b616efe`. Its findings and ready-to-paste tasks are preserved as rationale,
> not as current implementation guidance. See
> [`../2026-09-ADR-conformance.md`](../2026-09-ADR-conformance.md) for the
> current audit.

Evidence read directly from the repository: `sim/core/scheduler/simulation_clock.hpp`,
`sim/core/fields/field.hpp`, `sim/planet/mesh/cell_id.hpp`,
`sim/core/serialization/state_snapshot.hpp`, directory listings of `sim/`, `tests/`, and the former root ADR directory,
`docs/decisions/`, and `README.md`.

Markers: ✅ EVIDENCED · ⚠ RUNTIME_ONLY (right behaviour, not enforced) · ❌ VIOLATION · ∅ NOT FOUND

---

## 1. Findings

### A. Mesh and geometry (ADR-0002 §4.1–4.2)

| # | Requirement | Status | Evidence |
|---|---|---|---|
| A1 | Cells are the hexagonal/pentagonal **dual** | ❌ | `docs/decisions/0002` (now marked Superseded) chose primal triangles; README: "current executable still uses the triangular-face column as its legacy `CellGeometry` count; migration to the accepted dual topology is pending" |
| A2 | Per-cell geometry precomputed (area, centroid, local E/N/U basis) | ✅ | `PlanetMesh` owns immutable geometry; README M1 architecture; local bases derived from surface normals |
| A3 | Per-edge geometry (neighbour, length, centroid distance, outward normal) | ⚠ | neighbours and great-circle edge lengths exist for the 3-neighbour contract; centroid distance and outward normal not evidenced |
| A4 | Pentagons tagged | ∅ | not applicable yet (no dual); required by the dual migration |
| A5 | Divergence as edge-flux sum | ∅ | no operator library yet (M2+) |
| A6 | Area closure + topology tests | ✅ | `tests/conservation/test_mesh_area.cpp`; CLI reports area error ≤ 5e-14 |

**Note:** area accuracy here is excellent (5e-14 against ADR-0002's 1e-12 gate), and the spherical-excess area work carries over to the dual unchanged.

### B. Field layout and parallelism (ADR-0002 §4.4–4.6)

| # | Requirement | Status | Evidence |
|---|---|---|---|
| B1 | SoA, one contiguous 64-byte-aligned allocation per field | ⚠ | `Field<T>` is SoA over `std::vector<T>`, so contiguity yes, **alignment not guaranteed** (`field.hpp`) |
| B2 | 3-D fields layer-major `[layer][cell]` | ∅ | `Field<T>` is 2-D only, indexed by `CellId` |
| B3 | float32 prognostic / float64 accumulators policy | ⚠ | snapshot uses `std::vector<float>`; orbital state is `double`; no declared policy |
| B4 | Space-filling cell ordering | ∅ | subdivision order, stable but not locality-optimised |
| B5 | Fixed block partitioning independent of thread count | ∅ | no parallel decomposition yet |
| B6 | No atomics in reductions; fixed-order reduction tree | ∅ | no reductions yet |
| B7 | `-ffp-contract=off`, no fast-math | ⚠ | warnings-as-errors and sanitiser build exist; FP flags not evidenced in `CMakeLists.txt` |

### C. State partition and modes (ADR-0001 §4.1–4.2)

| # | Requirement | Status | Evidence |
|---|---|---|---|
| C1 | Fields grouped Slow / Fast / Climatology | ∅ | `PlanetState` owns evolving fields undifferentiated |
| C2 | Fast state allocated only when needed | ∅ | — |
| C3 | Mode enum + scheduler dispatch | ∅ | `sim/core/scheduler/` contains only `SimulationClock` |
| C4 | No wall-clock input to the solver | ✅ | clock advances by explicit timestep; Godot adapter maps wall-clock to sim time on its side |

### D. Clock, RNG, identity (ADR-0003 §3.2–3.4)

| # | Requirement | Status | Evidence |
|---|---|---|---|
| D1 | `int64` tick clock, no accumulated `double` seconds | ❌ | `simulation_clock.hpp`: `double time_s_`, `void advance(double timestep_s)` — accumulation drift by construction |
| D2 | RNG keyed by `(seed, stream, tick, cell)` | ∅ | `sim/core/math/` contains only `vec3d.hpp` |
| D3 | Field registry with stable numeric ids | ∅ | snapshot fields are struct members by name (`state_snapshot.hpp`) |
| D4 | Canonical order defined | ⚠ | dense `CellId` gives a natural order; not stated as an invariant, no `state_hash` |

### E. Process and documentation

| # | Observation | Status |
|---|---|---|
| E1 | Two decision sets: `docs/decisions/0001–0004` and the former root ADR directory | ⚠ — numbering collides (`0002` means different things in each); `docs/decisions/0002` correctly marked Superseded |
| E2 | README links the active ADRs across both decision directories | ⚠ — inconsistent source of truth |
| E3 | `tests/physics/` and `tests/regression/` exist | ✅ — ready for the §30 acceptance targets |

---

## 2. What this means

Three real violations, in priority order:

1. **The clock (D1).** A `double` seconds accumulator is the single most invasive defect: every future schedule, RNG key and snapshot label depends on it, and it silently breaks fork comparison. Fix before anything else.
2. **The mesh (A1).** Known and already accepted as pending. It gets cheaper the sooner it happens — M2 terrain and ocean masks have not started, so nothing downstream has to be rewritten.
3. **Identity and layout (B1–B4, D2–D3).** Missing rather than wrong: the registry, RNG discipline, alignment, layer-major fields and cell ordering are all additive.

Everything else is either present or genuinely belongs to a later milestone.

**Do the clock and the mesh in that order, before M2.** Both are cheap now and expensive after terrain, fields and saves exist.

---

## 3. Ready-to-paste Codex tasks

### Task 1 — Integer simulation clock (ADR-0003 §3.2)

```
Read docs/decisions/0003-determinism-snapshots-migration.md §3.2 and docs/audit Phase 0 finding D1.

Replace the accumulating double clock with an integer tick clock.
- Add sim/core/scheduler/tick.hpp: struct Tick { std::int64_t value; } with explicit
  conversions; 1 tick = 1 minute of simulated time; constexpr helpers ticks_from_days,
  ticks_from_years, seconds_of(Tick) for physics that needs SI seconds.
- SimulationClock stores Tick only. advance(Tick). time_s() becomes a derived accessor
  computed from ticks, never accumulated.
- Propagate to PlanetState, orbit, solar code, StateSnapshot (add tick; keep
  simulation_time_s as derived, bump state_snapshot_schema_version to 2).
- Update tests/unit/test_clock_parameters.cpp; add a test that advancing 1 tick
  N times equals advancing N ticks once, exactly.
Mark every changed site // [ADR3-CLOCK].
Do not touch the mesh or fields in this task.
Acceptance: full ctest green; no double accumulator remains in sim/ (grep);
solar diagnostics unchanged within 1e-12 relative.
```

### Task 2 — Dual mesh migration (ADR-0002 §4.1)

```
Read docs/decisions/0002-mesh-and-field-layout.md §4.1-4.2, docs/decisions/0002 (superseded)
and Phase 0 finding A1.

Switch authoritative cells from triangular faces to the hexagonal/pentagonal dual.
- Keep the triangular icosphere as construction scaffolding only.
- Cell = vertex of the subdivided triangular mesh; cell corners = triangle centroids.
- Build per-cell: spherical polygon area (reuse the existing spherical-excess work),
  centroid unit vector, local East/North/Up basis.
- Build per-edge CSR arrays: neighbour CellId, great-circle edge length, centroid
  distance, outward normal in the cell's local basis.
- Neighbour count is 6 except for exactly 12 pentagons; add is_pentagon(CellId).
- Keep the primal triangle list available for presentation only.
Mark // [ADR2-MESH].
Acceptance:
- L5 reports 10,242 cells, L6 reports 40,962 cells; exactly 12 pentagons at every level;
- area closure test still passes (gate 1e-12 relative, current primal result is 5e-14);
- new topology test: every edge shared by exactly two cells, neighbour relation symmetric;
- planet_cli mesh output updated; README mesh table updated to cells, not faces.
```

### Task 3 — Field identity, layout and RNG (ADR-0002 §4.4, ADR-0003 §3.3)

```
Read docs/decisions/0002-mesh-and-field-layout.md §4.4-4.6 and ADR-0003 §3.3-3.4; Phase 0 findings B1-B4, D2-D3.

1. Field registry: single generated header listing field_id (append-only, stable),
   name, kind (slow|fast|derived), dtype, layer count, and a migration initialiser.
   Compile-time uniqueness check. Snapshot fields addressed by id, not by struct name.
2. Field<T>: 64-byte aligned allocator; add Field3D<T> laid out [layer][cell].
   Declare and enforce the precision policy: float32 prognostic, float64 accumulators.
3. Cell ordering: add a space-filling permutation pass after mesh construction, applied
   before any field allocation; record the permutation for presentation mapping.
4. RNG: counter-based generator keyed by (seed, stream_id, tick, cell). No global state.
Mark // [ADR3-REGISTRY], // [ADR2-LAYOUT], // [ADR3-RNG].
Acceptance: registry duplicate-id test; RNG reproducibility test (same key → same value,
order- and thread-independent); micro-benchmark of a representative kernel before/after
the ordering pass, result recorded in the ADR-0002 file.
```

### Task 4 — Documentation hygiene (do with Task 1)

```
Consolidate decision records: keep one directory (recommend docs/decisions/), move
the then-root ADR-0001..0003 files there with non-colliding numbers (0005, 0006, 0007) or renumber the
earlier four; add docs/decisions/README.md indexing all records with status and
supersession links; fix README links so every ADR reference points to the same place.
```

---

## 4. Suggested gate before M2 starts

- Tasks 1–3 merged, `ctest` green including sanitiser build.
- L5 = 10,242 cells, 12 pentagons, area closure ≤ 1e-12.
- Tick clock: no `double` accumulator in `sim/`.
- Registry and RNG reproducibility tests in CI.
- Audit file updated: every ❌/∅ above either cleared with its commit, or explicitly deferred with a milestone.
