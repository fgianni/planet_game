# ADR 0001: Primal triangular icosphere cells

- Status: Accepted
- Date: 2026-09-23
- Milestone: P0 / M0

## Context

The development specification requires an icosphere whose surface cells have
one center, area, three neighbors, and three edge lengths. Geometry must remain
immutable after initialization and support the L6 reference resolution. A dual
mesh would change the neighbor-count contract and would require a separate
architecture decision before downstream solvers adopted it.

The geometry also needs unambiguous SI-area and edge-length definitions.
Planar chord triangles are convenient for rendering, but their planar areas do
not partition the physical spherical surface.

## Decision

Use the primal indexed icosphere:

- each subdivided triangular face is one dense `CellId`;
- vertices are normalized unit vectors retained by `PlanetMesh`;
- a cell's edge slot `i` is the undirected edge between vertex slots `i`
  and `(i + 1) % 3`;
- the neighbor in slot `i` is the unique face sharing that edge;
- cell centers are normalized arithmetic centroids of the three unit vertices;
- cell areas use the robust spherical-excess formula and are scaled by
  `radius_m²`;
- edge lengths are great-circle arc lengths scaled by `radius_m`;
- subdivision visits faces and their three canonical edges in fixed order,
  while a sorted undirected-edge midpoint map gives stable vertex IDs;
- `PlanetMesh` owns its arrays privately and exposes only const accessors.

The indexed vertex list is presentation-supporting geometry. Simulation fields
remain cell-centered and separate from the mesh.

## Consequences

- Every M0 cell has exactly three reciprocal neighbors, matching the stated
  solver interface.
- Areas sum to the sphere area within floating-point roundoff, making the
  conservation diagnostic physically meaningful.
- Dense stable IDs make structure-of-arrays fields and deterministic replay
  straightforward.
- Face areas are not perfectly uniform; downstream finite-volume operators
  must use the supplied areas and edge lengths rather than assuming uniform
  cells.
- Rendering a discontinuous cell-centered scalar duplicates triangle vertices
  in the presentation mesh. This is a presentation cost and does not change
  authoritative topology.
- Moving to a dual polygonal mesh later would alter neighbor cardinality and
  field placement and therefore requires a superseding ADR.

## Alternatives considered

### Dual Voronoi cells

This gives mostly hexagonal control volumes and can be attractive for some
geophysical operators, but it conflicts with the specified three-neighbor
triangular-cell contract and adds pentagon handling at M0.

### Planar triangle areas and chord lengths

These are easy to compute but measure the inscribed polyhedron rather than the
spherical surface. They were rejected for authoritative physical geometry;
the renderer may still draw straight triangle chords between the same unit
vertices.

### Mutable public topology arrays

This would simplify ad hoc tooling but could invalidate connectivity after
fields had been sized. Private ownership with const spans keeps geometry stable
while allowing efficient iteration.
