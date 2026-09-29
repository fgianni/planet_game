# Task M2-03 — Drainage topology

- **Milestone:** P0 / M2 (terrain and sea level complete; drainage is the
  remaining geological-planet deliverable)
- **Requested:** 2026-09-29
- **Scope:** deterministic depression filling, single-downstream routing,
  basins and catchment areas on the existing dual cell mesh
- **Prerequisite (complete):**
  `docs/tasks/M2-02-plates-terrain-and-sea-level.md`
- **Governing decisions:** `docs/DEVELOPMENT_SPEC_v0_4.md` §13 M2;
  ADR-0002 (mesh and deterministic blocks); ADR-0003 (L0 determinism);
  ADR-0005 §4.2, §4.3 and §5 V5–V8

## 1. Read first

1. `AGENTS.md`
2. `docs/DEVELOPMENT_SPEC_v0_4.md` §4–§6, §13 M2 and §14
3. `docs/decisions/0005-coastlines-and-drainage.md` §4.2, §4.3, §5 and §8
4. `docs/decisions/0002-mesh-and-field-layout.md` §4.1 and §4.6
5. `docs/decisions/0003-determinism-snapshots-migration.md` §3.1
6. `sim/planet/terrain/hypsometry.hpp`,
   `sim/planet/terrain/surface_fractions.hpp`,
   `sim/planet/terrain/terrain_generator.hpp`

Accepted ADRs take precedence over the specification where they conflict.

## 2. Goal

Derive a complete drainage graph from the immutable M2 terrain:

```text
hypsometry + sea level
 -> land-part drainage elevation + ocean outlets
 -> deterministic priority-flood and depression records
 -> one downstream neighbour per non-outlet land cell
 -> basin outlet IDs + upstream catchment areas
```

Every land cell must reach an ocean outlet. A no-ocean planet instead has one
terminal sink. The result is the static routing topology that M9 will use for
runoff, rivers and lake/endorheic behaviour; M2-03 does not add water stocks or
discharge.

## 3. Current code (as of commit `3f7573e`)

- `SlowState` persists nine hypsometry quantiles per cell and global sea level.
- `compute_surface_fractions` derives land/ocean fractions and the connected
  world ocean from those fields.
- `generate_terrain` creates deterministic geology, hypsometry and sea level;
  `TerrainGeneration` retains the in-memory geology and sea-level result.
- `Field2D<T>` supports the integer and floating containers needed for derived
  drainage data, but the registered persistent field types remain only
  `float32` and `float64`.
- There is no drainage code, downstream field, basin field, depression record,
  or catchment accumulation yet.

## 4. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Derived state only.** `DrainageState` is regenerated from
   `(mesh, hypsometry_m, sea_level_m)` after generation or load. Its fields are
   not registered and do not change the persistent snapshot schema. Do not
   persist drainage or add a dependency.
2. **Drainage elevation.** For a cell with ocean area, use the conditional
   mean of the quantile curve over its land fraction (the portion at or above
   sea level). For a fully land cell, including an ocean-disconnected
   below-sea-level depression, use the mean of the complete quantile curve.
   Ocean outlets retain this value for diagnostics even though they have no
   downstream neighbour. If a cell is completely submerged, use its highest
   quantile: this is the finite limiting conditional mean as its land fraction
   approaches zero.
3. **Outlets and terminal sink.** Every cell with `ocean_fraction > 0` is an
   ocean outlet. If there are no outlets, the single terminal sink is the cell
   with lowest drainage elevation, ties to lower `CellId`. Outlets and the
   terminal sink use `no_downstream` in the downstream field.
4. **Priority-flood.** Run a deterministic priority-flood seeded by all
   outlets, or by the terminal sink. The queue is ordered by
   `(filled_elevation_m, CellId)`. When visiting an unvisited neighbour, set
   its filled elevation to the maximum of its drainage elevation and the
   current filled elevation. The filled surface and all comparisons use
   `double`; there is no epsilon gradient.
5. **Depression identity.** A depression is an edge-connected component of
   cells for which `filled_elevation_m > drainage_elevation_m` and whose
   filled elevations are exactly equal (the priority-flood propagates the
   same `double` spill value). Components are ordered by their minimum
   `CellId`; this zero-based order is `depression_id`. Cells not raised use
   `no_depression`.
6. **Spill record.** Each depression stores its ID, spill level, canonical
   spill cell and raised area. Candidate spill cells are cells outside the
   depression adjacent to it with filled elevation no higher than its spill
   level and a route to an outlet. Select the candidate by
   `(filled_elevation_m, CellId)`. The depression's flat routing is seeded at
   member cells adjacent to that canonical spill, so every member routes
   through the one recorded spill cell.
7. **Downstream on slopes.** A non-outlet cell with a lower filled-surface
   neighbour uses the steepest descent
   `(filled_here - filled_neighbour) / centroid_distance_m`; exact slope ties
   go to lower `CellId`.
8. **Downstream on flats.** Raised depression cells route by breadth-first
   edge distance to their canonical spill. Other equal-elevation flats route
   by breadth-first edge distance to any neighbour that already has a route
   to a lower filled elevation or an outlet. Distance ties go to lower
   `CellId`. No epsilon gradient or random tie break is allowed.
9. **Catchment area.** A cell contributes `cell.area_m2 * land_fraction`.
   This includes the remaining land part of coastal outlet cells in their own
   catchment and makes the outlet-catchment sum equal total land area. Accumulate
   in `double`, in reverse topological order with ties by `CellId`, then store
   `catchment_area_m2` as `float`. Keep double totals in diagnostics so
   ADR-0005 V6 is tested before float conversion.
10. **Basin IDs.** `basin_id` is the terminal outlet's `CellId` value for
    every cell, including the outlet itself. On the no-ocean planet every cell
    has the terminal sink's ID. An aqua planet may have one zero-land-area
    basin per outlet cell; closure remains exact at zero area.
11. **Shape and sentinels.** Use `std::uint32_t` cell/depression values and
    `std::numeric_limits<std::uint32_t>::max()` for `no_downstream` and
    `no_depression`. Validate all field shapes and reject non-finite or
    non-monotone hypsometry rather than producing a partial graph.
12. **Parallelism and determinism.** Per-cell input calculations may use the
    existing deterministic block executor. Priority-flood, component
    labelling and topological accumulation use their fixed total orders.
    Results must be bit-identical for 1, 2, 8 and 16 workers.
13. **Pipeline integration.** `TerrainGeneration` owns the generated
    `DrainageState`. `generate_terrain` derives it after sea level is chosen.
    `planet_cli terrain` reports drainage diagnostics and its CSV appends
    drainage elevation, filled elevation, downstream, basin, depression and
    catchment columns. Existing output names and meanings do not change.
14. **M9 boundary.** Do not add precipitation, runoff, discharge, lakes,
    evaporation, groundwater, erosion, river rendering or an endorheic/spill
    policy. M2 records potential depressions and static paths; M9 decides how
    water occupies and moves through them.

## 5. Implementation steps

Use small commits; configure/build and run the full `ctest` suite after each.

1. Add conditional land-mean integration to `hypsometry` with analytic unit
   tests, including flat quantile stretches and coastal fractions.
2. Add `sim/planet/terrain/drainage.{hpp,cpp}` with `DrainageState`, depression
   records, input validation and deterministic priority-flood. Add unit tests
   on small generated meshes and controlled elevation fields.
3. Add slope/flat downstream routing, basin propagation and catchment
   accumulation. Add physics/conservation tests for V5–V7 and dead-rock/aqua
   edge cases.
4. Integrate drainage into `generate_terrain`; extend the terrain CLI and CSV;
   add V8 regression coverage for 1, 2, 8 and 16 workers and a CLI smoke check.
5. Update README, ADR-0005's implementation record, the conformance audit and
   project status documents. Record Release diagnostics at L5 and L6.

## 6. Tests and acceptance criteria

Use fixed seeds. Functional tests run at L4 and L5; L6 is used only for the
reported reference diagnostic and timing.

| # | Check | Gate |
|---|---|---|
| D1 | Land-part mean integration is finite and matches analytic piecewise-linear quantile cases | absolute error <= 1e-12 in `double` |
| D2 | Priority-flood: filled elevation is finite and never below original drainage elevation | exact |
| D3 | Depression records: every raised cell has one valid ID, non-raised cells have the sentinel, every depression has one valid canonical spill and all members route through it | exact |
| D4 | ADR-0005 V5: downstream references are neighbours or the sentinel; the graph is acyclic; every cell reaches an ocean outlet or the sole no-ocean sink | exact |
| D5 | ADR-0005 V6: double-precision outlet catchments sum to total routed land area | relative error <= 1e-12 |
| D6 | Catchments: every stored value is finite/non-negative and equals the double accumulation within `float` conversion error; basin IDs equal the reached outlet | exact / conversion bound |
| D7 | ADR-0005 V7: filled surface >= original; every depression has one spill cell and one spill level | exact |
| D8 | ADR-0005 V8: drainage fields, depression records and diagnostics from one seed are bit-identical for 1, 2, 8 and 16 workers | bit-identical |
| D9 | Edge cases: `dead_rock` has one terminal sink and one basin; `aqua_planet` has no routed land and exact zero catchment closure | exact |
| D10 | CLI diagnostics: outlet, basin and depression counts; largest catchment; total routed area; closure error; maximum fill depth; invalid-reference and cycle counts | reported and valid |
| D11 | L5/L6 Earth-like drainage generation time and total terrain generation time | reported; flag total L6 time above 10 s |
| D12 | Existing snapshot bytes and all pre-M2-03 output meanings remain unchanged; the full suite, warnings-as-errors, field-registry check and floating-point-policy check pass | exact/pass |

For D5, the closure comparison uses the retained double accumulator, not the
rounded `Field2D<float>` values mandated by ADR-0005 §4.3.

## 7. Out of scope

- Dynamic hydrology, runoff, river discharge, lakes and endorheic water
  balance (M9).
- Erosion, sediment transport or terrain mutation.
- Drainage persistence or snapshot schema changes.
- River rendering or presentation smoothing.
- ADR-0005 V4 (M3) and V9 (M11).
- The simulation-mode scheduler and all M3 work.

## 8. Environment and stop conditions

- Configure/build/test:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`,
  `cmake --build build --parallel`,
  `ctest --test-dir build --output-on-failure`.
- Run the repository's floating-point-policy check and ASan+UBSan suite before
  completion; follow the existing Linux `setarch -R` note if needed.
- Stop and ask before adding a dependency, changing an accepted ADR or a §4
  decision, changing snapshot bytes, or weakening an existing test.
- If an acceptance threshold cannot be met with §4, report the measured value
  instead of tuning the test or changing the algorithm silently.

## 9. Report when done

- Files added and changed, per implementation step/commit.
- Result of D1–D12, including measured D5 closure, D10 diagnostics and L5/L6
  D11 timings.
- CLI drainage diagnostics for `earth_like` at L5 and L6 with seed 20260928.
- Anything not met and why.
- Any ambiguity or incorrect assumption found in the specification or ADRs.

## 10. Completion note (2026-09-29)

Complete on `main`. No dependency, registered field or snapshot-schema change
was required, and no existing test was weakened.

| Check | Result |
|---|---|
| D1 | Pass. Piecewise-linear full/partial/zero-measure land means match analytic linear, stepped and flat cases to <= 1e-12. |
| D2 | Pass, exact. Every tested filled elevation is finite and >= its original drainage elevation. |
| D3 | Pass, exact. Raised components have one ID and canonical spill; every member's downstream chain passes through that spill. |
| D4 / ADR-0005 V5 | Pass, exact. All non-terminal references are edge neighbours, the graph is acyclic, and every cell reaches an outlet or the no-ocean sink. |
| D5 / ADR-0005 V6 | Pass. Catchment closure error is 6.34e-16 at L5 and 4.23e-16 at L6. |
| D6 | Pass. Catchments are finite/non-negative; basin IDs equal the reached terminal; double accumulation is rounded once to float storage. |
| D7 / ADR-0005 V7 | Pass, exact. Every depression has one spill cell/level; maximum fill depth is 693.3 m at L5 and 436.7 m at L6. |
| D8 / ADR-0005 V8 | Pass, bit-identical. L4/L5 drainage surfaces, records, routing fields, catchments and diagnostics match for 1, 2, 8 and 16 workers; regeneration after snapshot load also matches. |
| D9 | Pass, exact. `dead_rock` has one sink/basin; `aqua_planet` has zero routed land and zero closure residual. |
| D10 | Pass. CLI reports every requested count, area, closure, fill and validity diagnostic; CSV appends all drainage fields. |
| D11 | Pass. Four-worker Release measurements are below; L6 total terrain generation is 0.595 s, below the 10 s flag threshold. |
| D12 | Pass. Registry and snapshot schema are unchanged; Release and Clang ASan+UBSan are 38/38; floating-point policy is 57 translation units, 0 violations. |

`earth_like`, seed 20260928:

| Diagnostic | L5 | L6 |
|---|---:|---:|
| cells | 10,242 | 40,962 |
| ocean outlets / basins | 7,904 / 7,904 | 30,762 / 30,762 |
| filled drainage depressions | 69 | 470 |
| total routed land area (m²) | 1.47919e14 | 1.47923e14 |
| catchment closure relative error | 6.33794e-16 | 4.22518e-16 |
| largest catchment (m²) | 7.60820e12 | 6.24869e12 |
| maximum fill depth (m) | 693.266 | 436.678 |
| invalid / cyclic / unreachable | 0 / 0 / 0 | 0 / 0 / 0 |
| drainage regeneration time, 4 workers (ms) | 2.788 | 13.552 |
| total terrain generation time, 4 workers (ms) | 154.329 | 594.988 |

Interpretations required where ADR-0005 was not fully explicit:

- A completely submerged outlet has zero land measure, so its drainage
  elevation uses the highest quantile: the limit as land fraction tends to
  zero.
- A depression is a connected set of raised cells at one exact propagated
  spill level. The canonical spill is the selected adjacent cell outside that
  component.
- Coastal outlets contribute their remaining land area to their own
  catchment. Otherwise V6 would omit real land area.
- V6's 1e-12 gate applies to the required double accumulator. It cannot
  sensibly be applied after ADR-0005's required conversion to float storage.

These are deterministic definitions of terms already chosen by ADR-0005, not
changes to its decision. Dynamic lake occupancy and endorheic/spill behaviour
remain deferred to M9.
