# ADR-0005 — Fractional coastlines and cell-mesh drainage

- **Status:** Accepted
- **Date:** 2026-09-28
- **Accepted:** 2026-09-28
- **Milestone:** P0 / M2 (geological planet, terrain and ocean basins)
- **Context document:** Planetary Civilization Simulator — Design Record v0.4, §3.4, §4.2, §5.2, §5.5, §9.3; `docs/DEVELOPMENT_SPEC_v0_2.md` §13 M2 and M9
- **Related:** ADR-0002 (mesh, field layout; §8 open questions on coastlines and runoff routing), ADR-0003 (determinism, snapshots)

## 1. Context

M2 generates elevation, bathymetry, the land/ocean split and drainage
topology. Two choices made here are inherited by every later surface system
and cannot be changed cheaply once surface energy (M3), hydrology (M9) and
the ocean (M11) are written against them. ADR-0002 §8 left both open:

1. Is a coastline a **binary** per-cell land/ocean mask, or does each cell
   carry a **fractional** land area?
2. Is runoff routed **on the cell mesh**, or on a separate, finer **flow
   network** derived from elevation?

At the L6 reference resolution a cell covers about 12,450 km² with centres
about 113 km apart (ADR-0002 §4.1). Most real coastlines, straits and river
mouths are narrower than one cell.

The design record makes both questions gameplay-relevant: the Ice Age
scenario needs a low sea level (§4.2); coastal flooding, shrinking rivers and
changing sea ice must be visible (§3.4); runoff must follow terrain to rivers
and oceans (§5.5); and hydropower depends on simulated river flow (§9.3).

## 2. Decision drivers

- **D1 — Sea-level change.** Glacial sea levels, sea-level rise and coastal
  flooding must change land area continuously rather than in whole-cell steps.
- **D2 — Conservation.** Land area, water mass and energy must be
  accountable when sea level or coastlines change (ADR-0001 V1).
- **D3 — Determinism.** Generation and routing must be bit-identical for a
  given seed and build, independent of worker count (ADR-0002 §4.6).
- **D4 — One topology.** Every subsystem already shares the cell mesh; a
  second graph must earn its place.
- **D5 — Cost.** Memory and time stay inside ADR-0001 and ADR-0002 budgets.
- **D6 — Readability.** Coasts and rivers must be legible on the rendered
  planet without pretending to a resolution the simulation does not have.

## 3. Options considered

### 3.1 Coastlines

**A. Binary mask.** One byte per cell. Simple, but a coastline moves only in
whole-cell steps of about 12,450 km², sea-level change is a staircase, and
every coastal cell is wholly land or wholly ocean for energy and moisture
exchange. Rejected by D1 and D2.

**B. Fractional land from a stored fraction.** One `float` per cell. Smooth,
but sea-level change needs a rule for how the fraction moves, and without
sub-cell elevation that rule is arbitrary. Rejected by D1: it would be a
scripted modifier, not a physical state.

**C. Fractional land from sub-cell hypsometry — chosen.** Each cell stores
its sub-cell elevation distribution as a small, fixed set of quantiles. The
land fraction at any sea level is read from that distribution, so sea-level
change, flooding and glacial exposure follow from state.

### 3.2 Drainage

**A. Separate flow network.** A finer river graph derived from high-resolution
elevation. More faithful channel geometry, but a second topology to generate,
store, snapshot, keep deterministic and couple conservatively to every cell
field. Rejected for P0 by D4 and D5; it can be added later as a
presentation-level refinement fed by cell discharge.

**B. Multiple-flow-direction routing on cells.** Splits each cell's outflow
among lower neighbours. Smoother catchments, but no unique downstream path
for rivers, basins or infrastructure, and weights must be fixed
deterministically.

**C. Single-downstream routing on cells — chosen.** Each land cell drains to
exactly one neighbour, forming a forest of trees rooted at ocean outlets. Unique paths, exact catchment sums, and it matches the
per-cell `downstream`, `basin_id` and `catchment_area_m2` fields already
sketched in the specification's M2 section.

## 4. Decision

### 4.1 Sub-cell hypsometry and land fraction

- Each cell stores `hypsometry_m`: **nine** elevation quantiles at cumulative
  area fractions 0, 1/8, …, 1 of the cell, in metres relative to the
  planet's reference radius, non-decreasing, as `float`. The cell's mean
  elevation is derived from the same data by trapezoidal integration.
- `below_fraction(z_sea)` is the area fraction of the cell below sea level,
  found by linear interpolation of the quantile curve: 0 below the lowest
  quantile, 1 above the highest, continuous and non-decreasing in sea level.
- **Ocean connectivity.** Below-sea-level area is ocean only where it is
  connected to the world ocean. The ocean is the connected component, by area
  the largest, of cells whose lowest quantile is below sea level, linked
  through edge neighbours with the same property. *Amended 2026-09-28
  (§9.2): the ocean is the component containing the deepest cell, which keeps
  V2's monotonicity; at realistic sea levels it is the same component.* Other such components are
  inland depressions below sea level (Caspian, Dead Sea): their low ground is
  dry land in M2 and becomes lake area only through hydrology (M9).
- `ocean_fraction = below_fraction(z_sea)` for ocean-connected cells and 0
  otherwise; `land_fraction = 1 - ocean_fraction`. Raising sea level can
  connect an inland depression and flood it at once — the physically
  expected discontinuity (a sill being overtopped).
- Sea level `z_sea` is one global scalar in M2. The generator chooses it so
  that the area-weighted land fraction matches the scenario's target, by a
  fixed-iteration bisection that is deterministic. If the target falls inside
  a connectivity jump, the solve returns the sea level just below the jump
  and reports the achieved land fraction.
- The hypsometry comes from the M2 generator sampling its procedural
  elevation at a fixed, deterministic set of sub-cell points per cell. It is
  immutable after generation until geological processes (isostasy, erosion)
  are introduced.
- Later surface schemes treat a coastal cell as area-weighted land and ocean
  **tiles** sharing one atmospheric column. Tile fluxes are combined by area
  fraction, so a coastal cell's energy and water budgets remain exact. This
  is a consequence for M3, recorded here, not an M2 deliverable.

### 4.2 Drainage on the cell mesh

- **Outlets.** A cell with `ocean_fraction > 0` is an ocean outlet: its
  runoff enters the ocean in that cell. Drainage is defined over the
  remaining, fully land cells. A planet without ocean (Experiment A, dead
  rock) has one terminal sink: the cell with the lowest drainage elevation,
  ties broken by `CellId`.
- **Depressions.** A deterministic priority-flood over the drainage
  elevation (the cell mean on the land part) fills closed depressions to
  their spill level, with ties broken by `CellId`, seeded from the outlets.
  Every land cell therefore drains to an outlet, through a depression's spill
  cell where necessary. Each filled depression records a `depression_id`,
  its spill level and its spill cell.
  Whether a depression holds a lake, spills, or is endorheic depends on
  evaporation and inflow, so M2 records it and M9 decides.
- **Downstream.** Every non-outlet land cell has exactly one `downstream`
  neighbour: the steepest descent on the filled surface. On flats created by
  filling, cells route towards the spill cell by breadth-first distance,
  again with ties broken by `CellId`. No epsilon gradients are imposed.
- **Derived fields.** `basin_id` (the outlet each cell drains to) and `catchment_area_m2` (upstream land area including the cell itself)
  are accumulated in a fixed topological order with `double` sums, then
  stored as `float`.

### 4.3 Fields and precision

| Field | Type | Kind |
|---|---|---|
| `hypsometry_m` | 9 × `float` per cell (layer-major `Field3D`) | slow, snapshotted |
| `sea_level_m` | `double` scalar | slow, snapshotted |
| `land_fraction` | `Field2D<float>` | derived from the two above |
| `downstream` | `Field2D<std::uint32_t>` cell index, sentinel for outlets | derived |
| `basin_id` | `Field2D<std::uint32_t>` | derived |
| `depression_id` | `Field2D<std::uint32_t>`, sentinel when none | derived |
| `catchment_area_m2` | `Field2D<float>` | derived |

Derived fields are regenerated after load rather than snapshotted, unless
profiling shows regeneration is too slow. At L6 the new state adds about
1.5 MB of slow state and about 0.8 MB of derived fields, within the ADR-0002
§4.5 budget.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Hypsometry is finite and non-decreasing in every cell | exact |
| V2 | `land_fraction` is in [0, 1] and non-increasing in sea level; cells outside the ocean component have no ocean area | exact |
| V3 | Global land fraction matches the scenario target after the sea-level solve, or the solve reports a connectivity jump spanning the target | absolute error ≤ 1e-4 |
| V4 | Sea-level change without a connectivity change: land area lost equals the hypsometric integral over the change | relative error ≤ 1e-6 |
| V5 | Drainage graph is acyclic; every land cell reaches an outlet (or the terminal sink on a planet without ocean) | exact |
| V6 | Catchment closure: outlet catchments sum to the total routed land area | relative error ≤ 1e-12 |
| V7 | Filled surface ≥ original elevation; every depression has one spill cell | exact |
| V8 | Determinism: same seed gives bit-identical fields for 1, 2, 8 and 16 workers | bit-identical |
| V9 | Resolution consistency, L5 vs L6: global land fraction and the areas of the largest basins | land fraction ± 0.01; largest-basin areas ± 10 % |

## 6. Consequences

**Positive.** Sea-level change, coastal flooding and glacial land bridges
follow from state, and their area and water bookkeeping is exact. Coastal
cells exchange energy and moisture in proportion to their land and water
areas. Rivers have unique paths, so discharge, hydropower, flood exposure and
infrastructure can refer to them. There is one topology to snapshot and keep
deterministic.

**Negative.** Ocean extent depends on a connectivity search at every sea-level
change, not only on local state. Surface physics from M3 onward must handle mixed land/ocean
cells with tiles, which is more work than a mask. Rivers follow cell centres,
so at 113 km spacing their paths are coarse and their deltas and straits are
not resolved. Single-downstream routing concentrates flow along one path and
can produce parallel channels on smooth slopes.

**Risks and mitigations.**

- *Nine quantiles are too coarse for low-relief coasts* → V4 measures the
  error; the count is a constant and can be raised with a schema version bump
  (ADR-0003).
- *Parallel channels look artificial* → presentation may smooth river
  drawing; the authoritative graph stays single-downstream.
- *Priority-flood order leaks into results* → ties are broken by `CellId`
  only, and V8 tests worker-count independence.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M2 | Hypsometry generation, sea-level solve, land fraction, depression filling, downstream routing, basins and catchments; V1–V3 and V5–V8 |
| M3 | Land and ocean tiles with area-weighted fluxes; V4 on a sea-level perturbation |
| M4 | Snow and ice on the land tile only; ocean sea ice on the ocean tile |
| M9 | Runoff routing along `downstream`; lake and endorheic behaviour of depressions |
| M11 | Ocean dynamics restricted to cells with ocean fraction; V9 revisited with remapping (ADR-0002 V5) |

## 8. Open questions

- Connectivity through a shared edge is judged from each cell's lowest
  quantile, which can connect basins that a narrow sub-cell ridge would
  separate, and cannot see sub-cell straits. Should a strait or sill be
  forced open or closed? Current position: record ambiguous links in M2
  diagnostics and decide with the ocean in M11.
- Does sea level vary regionally (glacial isostatic adjustment, ice-sheet
  gravity) in P0? Current position: no; a global scalar only.
- The drainage elevation is the mean of the land part of the hypsometry,
  since water flows only on land. Is that right for coastal and depression
  cells? Revisit if routing near coasts looks wrong.

## 9. Implementation record (M2-02, 2026-09-28)

Task `docs/tasks/M2-02-plates-terrain-and-sea-level.md` implemented §4.1:
hypsometry generation, the sea-level solve and the land/ocean fractions.
Drainage (§4.2, V5--V8) is task M2-03.

### 9.1 Where it lives

| Concern | Code |
|---|---|
| Sub-cell sampling, quantiles, `below_fraction`, mean elevation | `sim/planet/terrain/hypsometry.{hpp,cpp}` |
| Ocean component, fractions, sea-level bisection | `sim/planet/terrain/surface_fractions.{hpp,cpp}` |
| Generator pipeline writing `hypsometry_m`, `sea_level_m` | `sim/planet/terrain/terrain_generator.{hpp,cpp}` |
| Diagnostics (A10, A11) | `sim/planet/terrain/terrain_diagnostics.{hpp,cpp}` |
| Plate-scale geology producing the structural elevation | `sim/planet/geology/` |

Nine quantiles come from 36 sub-cell points per hexagon (30 per pentagon):
six barycentric points in each fan triangle, weighted by a sixth of its
spherical area, so the weights close on the cell area to rounding. Quantiles
place each sample at the midpoint of its cumulative weight and interpolate
linearly; fraction 0 is the minimum and 1 the maximum.

### 9.2 Amendment: the ocean is anchored at the deepest cell

§4.1 chose the largest-area below-sea-level component. That rule is not
monotone: when two unconnected basins swap rank as the sea rises, the former
ocean reverts to land and the land fraction increases. Observed at L4, seed
20260928, sea level -6,500 m (land 0.99313 -> 0.99371, 15 components). It
breaks V2 and the premise of the bisection.

The implemented rule: the ocean is the component that contains the deepest
cell (lowest bottom quantile, ties to the lower `CellId`). Components only
merge as the sea rises, so the anchored ocean only grows and V2 holds by
construction. On six seeds at L4 and L5 and targets 0.10, 0.29 and 0.50 (36
cases), the deepest cell was always in the largest component, so realistic
planets are unaffected. Approved by the project owner on 2026-09-28; this
section records it pending a formal revision of §4.1.

### 9.3 Clarification: flat quantile stretches

`below_fraction` counts area strictly below the level. Where quantiles
coincide, that area sits at one elevation and floods at once when the level
passes it, so "continuous" in §4.1 holds except at such atoms. Generated
planets have none in practice (sub-cell noise separates the samples), but the
behaviour is defined and tested.

### 9.4 Validation (Release build, GCC 11, 2026-09-28)

| Check | Result |
|---|---|
| V1 hypsometry finite, non-decreasing (L3--L5, several seeds) | pass, exact |
| V2 fractions in [0, 1]; land non-increasing from -8,000 to +6,000 m in 250 m steps; no ocean outside the component (L4, L5) | pass, exact |
| V3 targets 0.10, 0.29, 0.50 (seed 20260928) | L4 and L5: error <= 1.2e-16 on all three. Before the task M2-02 amendment 3 values, L4's 0.50 fell in a connectivity jump that the solve reported (land 0.50234 below, 0.49115 above; the ocean component changed) |
| `aqua_planet` / `dead_rock` | land 0 with every cell in the ocean / land 1 with no below-sea component |
| V8-style determinism of the generated slow state, 1/2/8/16 workers (L4, L5) | bit-identical |
| Snapshot round trip of a generated planet | bit-identical |

`earth_like`, seed 20260928, with the starting values of task M2-02
amendment 3:

| | L5 | L6 |
|---|---|---|
| sea level (m) | 346.7 | 451.5 |
| ocean shallower than 200 m / deeper than 4,000 m | 4.5 % / 57.5 % | 4.1 % / 65.1 % |
| ocean elevation mean / standard deviation (m) | -3,540 / 2,243 | -3,818 / 2,127 |
| inland below-sea-level depressions | 12 | 159 |

V4 (a sea-level perturbation) belongs to M3 and V9 to M11, as §7 states. The
number of inland depressions grows with resolution because finer cells
resolve more closed lows; they are dry land in M2 and become lakes or
endorheic basins through M2-03 and M9.

## 10. Implementation record (M2-03, 2026-09-29)

Task `docs/tasks/M2-03-drainage.md` implemented §4.2 and validation gates
V5--V8. M2 now supplies the static topology that M9 will use; it does not
simulate runoff, discharge, lakes or evaporation.

### 10.1 Where it lives

| Concern | Code |
|---|---|
| Conditional land-part hypsometric mean | `sim/planet/terrain/hypsometry.{hpp,cpp}` |
| Priority-flood, depression records, flat/slope routing, basins and catchments | `sim/planet/terrain/drainage.{hpp,cpp}` |
| Pipeline and CLI/CSV diagnostics | `sim/planet/terrain/terrain_generator.{hpp,cpp}`, `apps/planet_cli/main.cpp` |
| Unit, physics/conservation and worker-count tests | `tests/unit/test_drainage_fill.cpp`, `tests/physics/test_drainage.cpp`, `tests/regression/test_terrain_determinism.cpp` |

`DrainageState` is derived and unregistered. It is rebuilt from mesh,
`hypsometry_m` and `sea_level_m` after generation or snapshot load, so PSNAP
bytes and the append-only registry are unchanged.

### 10.2 Deterministic definitions

- Every cell with positive ocean fraction is a terminal outlet. On a no-ocean
  planet the lowest drainage elevation, ties by `CellId`, is the one sink.
- Priority-flood queue order is `(filled elevation, CellId)`. A depression is
  an edge-connected set of cells raised to one exactly propagated spill level.
  IDs follow minimum `CellId`; one adjacent external spill is chosen
  deterministically.
- Raised flats use breadth-first edge distance to that spill. Other flats use
  breadth-first distance to a strict descent or terminal. Slopes select the
  greatest filled-surface drop per centroid metre; exact ties use `CellId`.
- Coastal outlet land area remains physical land and initializes that outlet's
  catchment. Catchments accumulate in double in a fixed topological order and
  convert once to the required float field. V6 is evaluated on the double
  totals.
- A wholly submerged cell's land-part mean has zero measure. Its highest
  quantile is the finite limiting drainage elevation and is used only for
  outlet diagnostics.

### 10.3 Validation

Release, `earth_like`, seed 20260928, four workers:

| Check | L5 | L6 |
|---|---:|---:|
| outlets / basins | 7,904 / 7,904 | 30,762 / 30,762 |
| filled depressions | 69 | 470 |
| routed land area (m²) | 1.47919e14 | 1.47923e14 |
| V6 relative closure error | 6.33794e-16 | 4.22518e-16 |
| largest catchment (m²) | 7.60820e12 | 6.24869e12 |
| maximum fill depth (m) | 693.266 | 436.678 |
| invalid / cyclic / unreachable | 0 / 0 / 0 | 0 / 0 / 0 |
| drainage regeneration time (ms) | 2.788 | 13.552 |
| total terrain generation time (ms) | 154.329 | 594.988 |

V5 and V7 pass exactly on generated and controlled terrains. V8 compares
every derived field, depression record and diagnostic bit for bit at 1, 2, 8
and 16 workers on L4 and L5. `dead_rock` has one sink/basin;
`aqua_planet` has zero routed land. Release and Clang 14 ASan+UBSan suites
pass 38/38; the strict floating-point policy checks 57 translation units with
zero violations.
