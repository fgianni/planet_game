# Task M6-01 — C-grid geometry and vector operators

- **Milestone:** P0 / M6 (first task)
- **Scope:** ADR-0011 §4.2:
  - the C-grid geometry of the Voronoi mesh: edge corners and tangents,
    corner cells and edges, dual-triangle and kite areas, the TRiSK
    weights;
  - the operators: tangential velocity, relative vorticity, kinetic
    energy, Perot reconstruction, edge normal and tangential gradients,
    cell-to-corner interpolation;
  - their validation, V1 and V2.

  No physics uses them yet (M6-02).
- **Governing decisions:** ADR-0011 §3.2, §4.2, V1, V2; ADR-0002 §4.2,
  §4.6, §9

## 1. Decisions made for this task

1. **Where the geometry lives.** `CGridGeometry`
   (`sim/planet/operators/c_grid.{hpp,cpp}`) is built from a `PlanetMesh`
   and is immutable afterwards.
   - `PlanetMesh` and `mesh_generator_version` are unchanged, because cell
     geometry and order are untouched (ADR-0011 §4.2).
   - A model that needs the C-grid builds it once per mesh, as the
     transport builds its graph.
2. **Conventions.**
   - The normal `n_e` points from the edge's first cell to its second, as
     everywhere else (ADR-0002).
   - The tangent is `t_e = k × n_e`, evaluated at the edge's velocity
     point. Its corners are ordered so that `t_e` points from
     `vertex[0]` to `vertex[1]`. That is the counter-clockwise order
     around the first cell, which `check_nondivergent_flux` already uses.
   - The velocity point is the midpoint of the Voronoi edge, the point
     where ADR-0002's divergence validation evaluates its fluxes.
3. **Areas.**
   - The kite `R_iv` is the part of cell i nearest corner v: the two
     spherical triangles (centre, bisector point, corner). Each bisector
     point is the midpoint of the arc between the two cell centres.
   - A corner's area `A_v` is the sum of its three kites. V1 checks it
     against the spherical triangle of the three cell centres, and checks
     the kites of each cell against the cell's area.
4. **Operators** (Ringler et al., 2010; Thuburn et al., 2009):
   - **Tangential velocity:** `u⊥_e = (1/d_e) Σ_e' W_ee' l_e' u_e'` over
     the other edges of both cells. The weight is
     `W_ee' = ±(½ − Σ R̃_iv) n_e,i n_e',i`, with R̃ = R/A_i summed over the
     corners passed going counter-clockwise from e to e'. The overall sign
     is fixed by the discrete identity in V1, not by convention.
   - **Vorticity at corners:** `ζ_v = (1/A_v) Σ_e s_ve d_e u_e`, where
     `s_ve = +1` when `n_e` turns counter-clockwise about v, that is when
     v is `vertex[1]`.
   - **Kinetic energy at cells:** `K_i = (1/A_i) Σ_e (l_e d_e / 4) u_e²`.
   - **Perot reconstruction:**
     `U_i = (1/A_i) Σ_e l_e n_e,i u_e (x_e − x_i)`, projected onto the
     cell's east/north basis.
   - **Normal gradient:** `(φ_2 − φ_1) / d_e`.
   - **Tangential gradient** of a corner field: `(ψ_b − ψ_a) / l_e`.
   - **Cell to corner:** `ψ_v = Σ_i R_iv ψ_i / A_v`.
5. **Determinism.** Edge and corner outputs run over fixed blocks of 256
   edges or corners, and each value depends only on its own inputs, so
   every worker count gives identical results (ADR-0002 §4.6). All
   arithmetic is in `double`.
6. **The `edge_layers` layout** moves to M6-02. The first field that uses
   it, the fast wind, arrives there, and nothing could test the layout
   earlier. M5-01 moved scenario-layered fields for the same reason.

## 2. Acceptance

- **V1, exact to rounding (relative 1e-12):**
  - each corner has three cells and three edges;
  - each tangent points from `vertex[0]` to `vertex[1]`;
  - the kite areas sum to every cell's area and to every dual triangle;
  - `W_ee' = −W_e'e`;
  - the Coriolis term `Σ_e l_e d_e u_e u⊥_e` vanishes for arbitrary u;
  - the curl of a gradient vanishes;
  - for arbitrary u, the vorticity of the tangential flux is minus the
    kite-weighted mean of the cell divergences. This is TRiSK's
    steady-geostrophic-mode condition (Thuburn et al., 2009).
- **V2, L3–L6, analytic field `u = ∇g + r × ∇h`:**
  - Every operator's relative L2 error, pentagons excluded, converges at
    order ≥ 1 from L5 to L6.
  - Maxima and pentagon errors are recorded in `planet_cli operators`.
- **Determinism:** bit-identical on 1, 2 and 8 workers.

## 3. Results

- **V1** (`tests/conservation/test_c_grid_identities.cpp`, L2–L6; the CLI
  also covers L7):
  - weight antisymmetry ≤ 4.4e-16, Coriolis work ≤ 3.5e-15, curl of a
    gradient ≤ 8.5e-17, and the steady-geostrophic-mode identity
    ≤ 4.4e-16.
  - Each cell's TRiSK fractions are taken over its kite sum, so they sum to
    one exactly.
  - The kites tile the stored cell areas to 3.7e-13 at L5 and 1.7e-12 at
    L6, growing ×4 per level. The cause is the mesh's circumcentres. Each
    is the normalised cross product of two differences of length h, so it
    is accurate to ε/h. A cell area recomputed accurately from the same
    corners agrees with the stored one, which rules out the area formula.
    The gate is `1e-15 · 4ᴸ` (ADR-0011 §12).
  - The kite triangles use the triple product of differences, which keeps
    their relative precision for small triangles.
- **Barycentric corner values.** The kite-weighted corner value sits at
  the kites' centroid, not at the corner, so its tangential differences
  did not converge (16 % at every level). `interpolate_to_corner` adds
  barycentric weights in the gnomonic projection about the corner, which
  are exact for linear fields. The conservative `cell_to_corner` stays for
  conserved quantities.
- **V2** (`tests/physics/test_c_grid_accuracy.cpp`, L3–L6; the CLI prints
  L2–L7):
  - The reconstruction and both gradients converge at second order.
  - TRiSK's `u⊥` and kinetic energy stall in maximum at 0.7 % and 1.6 %
    in the first ring around the pentagons. Their L2 orders fall: for `u⊥`
    1.89, 1.70 and 1.40 at L4→L5, L5→L6 and L6→L7.
  - The vorticity is first order (0.93).
  - The seams carry no more error than the interior.
  - The gates are those of ADR-0011 §12. Whether this accuracy
    is acceptable is decided by Williamson test 2 in M6-02.
- **Determinism:** every operator is bit-identical on 1, 2 and 8 workers
  at L5.
- **Cost at L6** (Clang Release):
  - Building the geometry takes 45 ms.
  - It holds 10 TRiSK weights per edge, stored as parallel arrays of edges
    and values, and occupies 32.8 MB: weights 14.7 MB, edges 10.8 MB,
    corners 7.2 MB.
  - That is above ADR-0002 §4.5's rough 12 MB for all geometry and
    topology, and far inside ADR-0001's 4 GB.
  - Only a model that needs the C-grid builds it. The edges' stored
    normal, tangent and midpoint vectors (72 bytes per edge) can be derived
    on the fly if memory ever matters.
- `planet_cli operators` prints the C-grid identities and norms beside the
  ADR-0002 operators.
- **Full suite:** 66 tests pass (GCC Debug). The new tests also pass with
  Clang Release, and the floating-point policy check is clean.
