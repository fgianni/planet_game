# ADR-0002 — Mesh topology, resolution policy and field layout

- **Status:** Accepted
- **Date:** 2026-09-23
- **Accepted:** 2026-09-25
- **Context document:** Planetary Civilization Simulator — Design Record v0.4, §15.1, §21
- **Related:** ADR-0001 (time acceleration, simulation modes), ADR-0003 (determinism, snapshot schema, migration)
- **Supersedes:** [`docs/decisions/0002-primal-triangular-icosphere.md`](../docs/decisions/0002-primal-triangular-icosphere.md)

## 1. Context

Every subsystem in PlanetSim — radiation, atmosphere, ocean, land, ice, carbon, civilization fluxes — reads and writes fields on the same planet. The mesh and the memory layout are therefore the one decision that every later decision inherits. They fix the accuracy of the discrete operators, the achievable speed (the performance budget of ADR-0001 is mostly a memory-bandwidth budget), the size of snapshots and forks, and whether results are deterministic.

The design record specifies an icosphere and "approximately 81,920 surface cells" at subdivision level 6. That number is the count of **triangles**, not of cells in the sense used below; the choice between triangle-centred and vertex-centred cells is the first thing this ADR has to settle, and it changes the cell count by a factor of two.

## 2. Decision drivers

- **D1 — Operator quality.** Conservation must be structural, not corrected afterwards. Gradients, divergences and Laplacians must behave uniformly over the sphere, with no preferred axis and no polar singularity.
- **D2 — Memory bandwidth.** At the sizes involved, the solver is bandwidth-bound; layout matters more than instruction count.
- **D3 — Determinism.** Identical results for a given build, platform and seed, independently of thread count (ADR-0001 §4.5).
- **D4 — Resolution as a parameter.** Development at level 5, shipping at level 6, with scenarios portable between levels.
- **D5 — Snapshot size.** History forks and the counterfactual planet multiply stored states.
- **D6 — Simplicity of the consumer API.** Civilization systems and the observation layer should address a cell, not a mesh element type.

## 3. Options considered

**A. Latitude–longitude grid.** Rejected: converging meridians force tiny timesteps near the poles, and the polar singularity distorts exactly the region where polar amplification, sea ice and the overturning circulation matter most.

**B. Cubed sphere.** Good cell uniformity and simple per-face indexing, but the eight corner points and the face-boundary discontinuities complicate operators and halo exchange.

**C. Icosahedral triangles as cells (triangle-centred).** Twice as many cells for the same subdivision level, a natural cell ordering, but each triangle has only three neighbours, which yields poorer gradient reconstruction and a stiffer CFL for the same nominal resolution.

**D. Icosahedral hexagonal dual (vertex-centred) — chosen.** Cells are hexagons, except twelve pentagons at the original icosahedron vertices. Near-uniform area, six neighbours, and the standard choice in modern geodesic climate models (MPAS, ICON) for exactly these reasons. Its finite-volume operators conserve mass by construction.

**E. Adaptive or variable resolution.** Rejected for P0. The gain is real for coastlines and storms, but variable resolution complicates operators, load balancing and determinism, and the remap machinery decided below leaves the door open for a later regional refinement.

## 4. Decision

### 4.1 Mesh

The simulation mesh is the **hexagonal–pentagonal dual of a subdivided icosahedron** (Goldberg polyhedron). Cells are centred on the vertices of the triangular mesh; triangle centroids become cell corners.

| Level | Cells (hex + 12 pent) | Mean cell area | Mean spacing | Role |
|---|---|---|---|---|
| 4 | 2,562 | ~199,000 km² | ~450 km | unit tests, fast experiments |
| 5 | 10,242 | ~49,800 km² | ~226 km | **development default** |
| 6 | 40,962 | ~12,450 km² | ~113 km | **shipped reference** |
| 7 | 163,842 | ~3,100 km² | ~57 km | research only; out of P0 budget |

Note for the design record: level 6 gives **40,962 cells**, not 81,920 — that figure counted triangles. The cell count is what drives memory and speed, so §15.1 should be read with this table.

Each cell stores precomputed geometry, built once at load: area, centroid unit vector, local east/north tangent basis, and per edge the neighbour index, edge length, distance between centroids, and outward normal in the local basis.

### 4.2 Operators

Finite volume on the dual mesh:

- **Divergence** from edge fluxes: `div(F)_i = (1/A_i) Σ_e F_e · n_e · l_e`. Mass, energy and tracer conservation is then exact up to rounding, which is what makes the budget checks of ADR-0001 V1 meaningful.
- **Gradient** by Green–Gauss reconstruction over the cell edges, with a least-squares variant available for cells adjacent to pentagons.
- **Laplacian** as divergence of gradient, using the two-point edge difference form — cheap, positive-definite, and the form already validated in the prototype.
- **Tracer advection**: flux-form upwind with a slope limiter in reference mode; semi-Lagrangian with a conservative fixer in climate mode, where the long timestep makes flux-form unusable.

The twelve pentagons are a known, bounded defect: operators there have a slightly different truncation error. They are tagged at build time, excluded from convergence statistics, and never used as scenario anchors (a civilization start site or a validation probe must not sit on one).

### 4.3 Vertical structure

Terrain-following (sigma) layers, count as a parameter: three in development, five shipped. Ocean uses fixed-depth layers: two in development (mixed layer, deep), four shipped.

### 4.4 Field layout

Structure of arrays, one contiguous allocation per field, 64-byte aligned:

```
Field2D<T>   : [cell]                      // surface, soil, ocean surface, civilization fluxes
Field3D<T>   : [layer][cell]               // layer-major: horizontal loops stay contiguous
Edge<T>      : [edge]                      // fluxes, precomputed geometry
```

- **Precision:** `float32` for prognostic and diagnostic fields; `float64` for global accumulators, budget diagnostics and the slow ocean and carbon reservoirs, where a century of accumulation would otherwise lose the signal in rounding.
- **Layer-major, not cell-major**, because every operator sweeps horizontally within a layer; vertical coupling is a second, shorter pass.
- **Cell ordering:** recursive ordering by parent triangle (a space-filling order on the sphere), so neighbours are near in memory. Expect roughly 2× on bandwidth-bound kernels versus naive construction order; measure it rather than assume it.
- **Neighbour topology** in compressed sparse row form (`offsets[cell]`, `neighbour[]`, `edge[]`), fixed at load, shared by every subsystem.

The three partitions of ADR-0001 map onto distinct field groups: `SlowState` (snapshotted, authoritative), `FastState` (allocated only when a reference run or weather window is active), `Climatology` (derived, regenerated).

### 4.5 Memory budget

Rough figures per instance, with ~28 surface fields, 8 atmospheric fields per layer, 6 ocean fields per layer:

| | Level 5, 3 layers | Level 6, 5 layers |
|---|---|---|
| Slow state | ~4 MB | ~17 MB |
| Fast state (when active) | ~2 MB | ~13 MB |
| Climatology (12 months × 8 statistics) | ~4 MB | ~16 MB |
| Geometry and topology | ~3 MB | ~12 MB |
| **Working set** | **~13 MB** | **~58 MB** |

Comfortably inside the ADR-0001 budget even with the counterfactual planet running, which is what makes the shadow planet affordable at full resolution.

### 4.6 Parallelism and determinism

- Cells are partitioned into contiguous **blocks** of the space-filling order, one block per worker; block boundaries are fixed at load, not by thread count.
- **No atomics in reductions.** Each block reduces locally in index order; block results combine in a fixed tree. The result is identical for any thread count.
- Compiled with `-ffp-contract=off` and without fast-math; FMA and reassociation are opt-in per kernel and only where a test shows the result is unchanged.
- Iteration order over cells and edges is fixed by the topology arrays, never by a hash map or pointer address.

### 4.7 Resolution policy and remapping

Resolution is a scenario parameter. Scenarios are authored at a reference level and remapped on load by **conservative, area-weighted remapping** between levels, which preserves global mass, energy and carbon to rounding. This covers three needs: developing at level 5 and shipping at level 6, loading an old save into a newer build, and comparing runs at different resolutions during validation.

On the ADR-0001 open question — whether the counterfactual planet runs at reduced resolution — the memory and cost figures above say it does not need to. Run it at the same level; a differently resolved shadow would put resolution error into the very number the player is asked to trust.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Geometry closure: Σ cell areas = 4πR² | relative error < 1e-12 |
| V2 | Topology: neighbour symmetry, edge counts, exactly 12 pentagons, every edge shared by two cells | exact |
| V3 | Operator accuracy on analytic fields (spherical harmonics): gradient, divergence, Laplacian | error decreases with refinement at ≥ 1.5 order; no axis-aligned pattern in the error map |
| V4 | Divergence of a non-divergent field | ≤ rounding |
| V5 | Conservative remap level 5 ↔ 6, round trip | mass and energy conserved to 1e-12; field RMS change reported |
| V6 | Determinism: 1, 2, 8, 16 threads, same seed | bit-identical state |
| V7 | Bandwidth micro-benchmark of a representative kernel, ordered vs naive cell order | ordering gain measured and recorded |
| V8 | Memory budget (§4.5) | within 20 % of the table |

V3 is the one that decides whether the mesh is good: an error map that shows the icosahedron's edges or the twelve pentagons as visible structure means the operators need work before any physics is built on them.

## 6. Consequences

**Positive.** Uniform cells with six neighbours, no polar singularity, structurally conservative operators, and a layout that is deterministic by construction rather than by discipline. Memory is small enough that forks, history and the shadow planet are all affordable. Resolution becomes a parameter, so development speed no longer depends on the shipping target.

**Negative.** The dual mesh is more work to build than triangles, and the twelve pentagons are a permanent irregularity that every new operator must be checked against. Layer-major layout makes vertical-column physics (radiation, convection) strided; those kernels will need a transposed working buffer.

**Risks and mitigations.**

- *Pentagon artefacts appear as visible structure in results* → V3 error maps in CI; pentagons excluded from statistics; scenario anchors forbidden on them.
- *Semi-Lagrangian advection in climate mode loses conservation* → mandatory conservative fixer, checked by V1 of ADR-0001.
- *Cell ordering optimisation turns out not to matter* → V7 measures it; if the gain is small, keep the simpler order.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M0 | Mesh generator, dual construction, geometry and topology arrays, V1–V2 |
| M1 | Field containers, ordering, block partitioning, V6–V8 |
| M2 | Operator library and V3–V4 before any physics is written on top |
| M3 | Conservative remap and resolution switching, V5 |

## 8. Open questions

- Does the ocean use the same horizontal mesh as the atmosphere, or a coarser one? Same mesh is simpler and keeps coupling trivial; a coarser ocean would save little at these sizes. Current position: same mesh.
- Are rivers and runoff routed on the cell mesh, or on a separate flow network derived from elevation? A flow network is more faithful but introduces a second topology.
- Should coastlines be represented sub-cell (fractional land area per cell) rather than binary land/sea? Fractional coverage would improve coastal realism, sea-level rise and the civilization's coastal exposure at no mesh cost, and is probably worth doing from the start.
