# Task M2-02 — Plates, terrain, hypsometry and sea level

- **Milestone:** P0 / M2 (in progress; G2-M2 operators and M2-01 state/snapshots are complete)
- **Requested:** 2026-09-28
- **Scope:** procedural plate-scale geology up to the ocean/land split. Drainage is the next task (M2-03).
- **Prerequisite (complete):** `docs/tasks/M2-01b-foundation-hardening.md` (specification §26).
- **Governing decisions:** `docs/DEVELOPMENT_SPEC_v0_4.md` §13 M2 and §13.2; ADR-0002 (mesh, operators, field layout); ADR-0003 (determinism, snapshots); ADR-0005 §4.1 and §5 V1–V3

## 1. Read first

1. `AGENTS.md`
2. `docs/DEVELOPMENT_SPEC_v0_4.md` §13 M2, §13.1 (reference experiments A–C), §13.2
3. `docs/decisions/0005-coastlines-and-drainage.md` §4.1 (hypsometry, ocean connectivity, sea-level solve), §4.3, §5
4. `docs/decisions/0002-mesh-and-field-layout.md` §4.2 and §9 (operators and their accuracy)
5. `docs/decisions/0003-determinism-snapshots-migration.md` §3.1 (L0 determinism)
6. `sim/planet/planet_state.hpp`, `sim/core/serialization/snapshot_file.hpp`, `sim/planet/operators/finite_volume.hpp`, `sim/core/random/counter_rng.hpp`

Accepted ADRs take precedence over the specification where they conflict.

## 2. Goal

Generate a deterministic planet from a seed:

```text
seed -> plates (regions, rotation vectors) -> boundary classification
     -> continental/oceanic crust and crust age -> structural elevation
     -> roughness and diffusive smoothing -> sub-cell hypsometry (slow state)
     -> sea-level solve for a target land fraction -> land/ocean fractions
```

The output is the slow state of ADR-0005 (`hypsometry_m`, `sea_level_m`),
which the existing persistent snapshots already store, plus an in-memory
`GeologyState` that preserves plate and crust structure for later systems.
The goal is plausible, physically interpretable structure: mountain belts at
continental collisions, trenches and arcs at subduction zones, ridges and
age-deepening ocean basins, shelves at passive margins. It is not a
geological-timescale simulation (specification §13.2) and not tuned for looks.

## 3. Current code (as of commit `54fc067`)

- `PlanetState` has `SlowState { Field3D<float> hypsometry_m (9 layers); double sea_level_m; }`,
  lazy `FastState`, empty `Climatology`, and derived `ForcingState`.
- Persistent snapshots (`write_snapshot`, `read_snapshot`) store exactly the
  registered slow fields and verify mesh identity.
- `sim/planet/operators/finite_volume.hpp`: divergence, least-squares
  gradient, two-point Laplacian, all block-parallel and deterministic.
- `sim/core/random/counter_rng.hpp`: stateless keyed RNG
  (`keyed_random_unit_double(seed, stream, tick, key, sample)`); streams
  `validation`, `weather`, `hydrology`.
- `PlanetMesh` cells carry `center_unit`, `east_unit`, `north_unit`,
  `area_m2`, CSR edges with `neighbor`, `edge`, outward normals; shared edges
  carry `length_m` and `centroid_distance_m`; `cell_corners()` gives the
  counter-clockwise corner indices; `corners_unit()` the corner positions.
- There is no noise function and no geology code yet.

## 4. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Randomness.** Add `RandomStreamId::geology = 0x0003'0001U` (do not change
   existing values). Every random quantity is keyed by
   `(world_seed, geology, tick 0, key, sample)` with documented keys, so
   results do not depend on evaluation order or worker count.
2. **Noise.** Implement a deterministic 3-D gradient noise and fractal sum
   (fBm) in `sim/core/random/noise.{hpp,cpp}`, with lattice gradients hashed
   through `mix_random_key` from the seed, a per-use salt, and the integer
   lattice coordinates. Evaluate it on unit-sphere positions scaled by a
   frequency. No third-party code.
3. **Plates by noisy multi-source growth.** Choose `plate_count` seed cells
   (default 12, allowed 2–40) from keyed random points on the sphere, taking
   the nearest cell and skipping duplicates deterministically. *Amended
   2026-09-28 (see §10): seeds closer than 0.5 √(4π/P) rad to an earlier seed
   are also skipped.* Grow all plates
   at once with a multi-source Dijkstra over cell edges, ordered by
   `(cost, CellId)`. The edge cost is `centroid_distance_m × plate_speed ×
   (1 + a × fbm(edge midpoint))`, where each plate has a keyed growth-speed
   factor in [0.6, 1.4] and `a` ≈ 0.5. This guarantees connected plates with
   irregular boundaries and unequal sizes.
4. **Plate motion.** Each plate gets a keyed random rotation pole and angular
   speed such that surface speeds are 1–10 cm/yr; the cell velocity is
   `ω × r` expressed in the cell's East/North basis, in m/s.
5. **Boundary classification.** For every shared edge between different
   plates, take the relative velocity of the two cells and split it into the
   normal component (along the stored edge normal) and the tangential
   component. Classify as convergent, divergent or transform: normal-dominant
   (|normal| ≥ |tangential|) by sign, otherwise transform. Keep the class,
   the normal relative speed and the crust types on both sides per edge.
6. **Continental crust.** Build a continental-propensity field: low-frequency
   fBm plus a keyed per-plate bias, so some plates are mostly oceanic. Mark
   the cells with the highest propensity continental until their area reaches
   `continental_area_fraction` (default 0.40), with ties broken by `CellId`.
   Continental area exceeds the land fraction because shelves are flooded.
7. **Crust age and ocean depth.** Oceanic age is the along-mesh distance to
   the nearest divergent boundary of its own plate divided by that boundary's
   half-spreading rate (cap 180 Myr; plates without a divergent boundary use
   the cap). Oceanic structural depth follows a documented age–depth
   relation, starting from Parsons & Sclater (1977):
   `d = 2500 + 350 √t_Myr` m for t < 70 Myr, flattening towards ~6,400 m
   beyond. Continental age is a keyed value in 0.5–3.5 Gyr per connected
   continental region. Store age in seconds (SI).
8. **Structural elevation** (cell centres, metres) is the sum of:
   - crust base: the age–depth relation for oceanic cells; `+300 m` for
     continental cells (*amended to +800 m, see §10*);
   - passive margins (*continental width amended to 400 km, see §10*):
     continental cells within ~150 km of oceanic crust of
     the same plate ramp down to the shelf edge (−130 m); oceanic cells
     within ~100 km of continental crust ramp from the shelf edge to their
     age depth (continental slope);
   - convergent boundaries, by crust pair: continent–continent: mountain
     belt, +3,500 m peak, ~250 km half-width on both sides;
     ocean–continent: trench on the oceanic side (−2,500 m, ~60 km) and a
     volcanic cordillera on the continental side (+2,500 m, ~200 km);
     ocean–ocean: trench on the older side and island arc (+1,500 m) on the
     younger side;
   - divergent continent–continent: rift valley (−800 m, ~80 km);
   - transform: a narrow fault zone (±200 m roughness, ~50 km).
   Use Gaussian profiles in along-mesh distance from the boundary, computed
   with a deterministic multi-source Dijkstra per boundary class, scaled by
   the boundary's normal relative speed relative to the median. All constants
   live in a `GeologyParameters` struct with units in the names and a comment
   stating that they are starting values, not calibrated physics.
9. **Roughness and smoothing.** Add fBm roughness (larger on continents than
   on the ocean floor), then apply a fixed number of explicit diffusion steps
   with the existing `laplacian` operator and a stable step, as a documented
   "diffusive erosion approximation". Keep the number of steps and the
   diffusivity in `GeologyParameters`.
10. **Sub-cell hypsometry** (ADR-0005 §4.1). For each cell, split it into its
    fan triangles (cell centre, corner k, corner k+1). In each triangle use the
    six barycentric points that are the permutations of (2/3, 1/6, 1/6) and
    (1/6, 5/12, 5/12), normalised onto the sphere, each weighted by one sixth
    of the triangle's spherical area. At each point, elevation is the
    barycentric interpolation of the structural elevation at the cell centre
    and at the two corners (a corner's value is the mean of its three
    cells), plus a high-frequency fBm term. The nine stored values are the
    area-weighted quantiles at 0, 1/8, …, 1 (0 is the minimum, 1 the
    maximum), non-decreasing, as `float`.
11. **Sea level and fractions** exactly as ADR-0005 §4.1 (*ocean rule amended
    2026-09-28, ADR-0005 §9.2: anchored at the deepest cell*): `below_fraction`
    by linear interpolation of the quantile curve; the ocean is the
    largest-area connected component of cells whose lowest quantile is below
    sea level; fixed-iteration bisection for the target land fraction
    (default 0.29); report a connectivity jump that spans the target. Provide
    a pure function that computes land and ocean fractions (and the ocean
    component) from `(mesh, hypsometry, sea_level)`, so the drainage task and
    later systems can reuse it.
12. **Reference presets** (specification §13.1): `earth_like` (defaults),
    `aqua_planet` (target land fraction 0: sea level above every cell's
    maximum), `dead_rock` (target land fraction 1: sea level below every
    cell's minimum, no ocean).
13. **Use the M2-01b foundations.** Every global sum or other reduction
    (areas, land fraction, sea-level bisection, diagnostics) goes through
    `reduce_deterministic_blocks`; registered fields are allocated with
    `make_field`; no new registered field is needed.
14. **Geology is not persisted yet.** `GeologyState` (plate id, crust type,
    crust age, boundary distances, plate list) is returned by the generator
    and kept in memory. Do not register it in the field registry or the
    snapshot: integer fields would need new snapshot data types, and the
    persistence choice (store the fields, or store the generation parameters
    and regenerate) is a separate decision. Do not add a cell-centre
    `elevation_m` field either: under ADR-0005 the mean elevation is derived
    from the hypsometry.

## 5. Implementation steps

Small, reviewable commits; build and run `ctest` after each.

1. `RandomStreamId::geology` and the noise module with unit tests
   (determinism, seed sensitivity, bounded range, no axis-aligned lattice
   artefacts in a sampled mean).
2. A deterministic multi-source Dijkstra over the mesh (`(cost, CellId)`
   ordering, custom edge costs), with tests.
3. `sim/planet/geology/`: `GeologyParameters` and presets, plates, motion
   and boundary classification.
4. Continental crust, crust age, structural elevation, roughness and
   diffusive smoothing.
5. Hypsometry sampling into `SlowState`.
6. Surface fractions, ocean connectivity and the sea-level solve.
7. CLI: `planet_cli terrain --subdivision L --seed N [--preset NAME]
   [--land-fraction F] [--plates P] [--map FILE.csv] [--snapshot FILE.psnap]`
   printing the diagnostics of §6 A11; the CSV has one row per cell (id,
   latitude, longitude, plate, crust type, crust age in Myr, nearest boundary
   class and distance in km, mean elevation, lowest and highest quantile,
   land fraction). Add a ctest smoke test.
8. Documentation: README section, ADR-0005 implementation record (§9, new),
   the audit, `AGENTS.md`/`CLAUDE.md` status, and this task's completion note.

## 6. Tests and acceptance criteria

Run the suite at L4 and L5; use L6 only where stated. Seeds are fixed in the
tests.

| # | Check | Gate |
|---|---|---|
| A1 | Determinism: same seed gives bit-identical hypsometry, sea level and `GeologyState`; any parallel stage gives identical results for 1, 2, 8, 16 workers | bit-identical |
| A2 | Different seeds give different planets | hypsometry differs |
| A3 | Plates: requested count, every plate one connected region, each at least 0.5 % of the sphere, every cell assigned | exact |
| A4 | Boundaries: every cross-plate edge classified; relative velocities antisymmetric between the two sides | exact / 1e-12 relative |
| A5 | Structures correlate with boundaries (L5, `earth_like`): mean structural elevation within 300 km of continent–continent convergent boundaries exceeds the continental mean by ≥ 1,000 m; the minimum within 100 km of ocean–continent convergent boundaries is ≥ 1,000 m below the abyssal mean; oceanic cells within 200 km of divergent boundaries are ≥ 1,000 m shallower than oceanic cells more than 1,000 km away | as stated |
| A6 | Crust: continental area fraction within one cell area of the target; oceanic age is zero at divergent boundaries and non-decreasing along each Dijkstra path away from them | exact / as stated |
| A7 | Hypsometry (ADR-0005 V1): finite and non-decreasing in every cell | exact |
| A8 | Fractions (ADR-0005 V2): in [0, 1]; non-increasing land fraction as sea level rises (checked at several levels); cells outside the ocean component have no ocean area | exact |
| A9 | Sea-level solve (ADR-0005 V3): targets 0.10, 0.29, 0.50 met to 1e-4 or a spanning connectivity jump reported; `aqua_planet` gives land fraction 0 and `dead_rock` gives 1 with no ocean component | as stated |
| A10 | Non-flat bathymetry (`earth_like`, L5): some ocean area shallower than 200 m (shelves) and some deeper than 4,000 m (abyssal); ocean elevation standard deviation ≥ 500 m | as stated |
| A11 | Diagnostics reported by the CLI: plate count and areas, boundary lengths by class, continental fraction, crust-age range, elevation percentiles, achieved land fraction and sea level, generation time | reported |
| A12 | Snapshot: a generated planet written with `write_snapshot` and read back gives bit-identical slow state | bit-identical |
| A13 | L6 `earth_like` generation time in a Release build | reported; flag it if above 10 s |

The suite must stay green in CI (GCC and Clang, Debug and Release,
ASan+UBSan) with warnings as errors, and the floating-point policy check
must pass. Follow the surrounding style: `snake_case`, strong ID types,
`[[nodiscard]]`, SI units in names, `double` accumulation for global sums,
`float` storage for fields.

## 7. Out of scope

- Drainage: depression filling, downstream routing, basins, catchments
  (M2-03, ADR-0005 §4.2, V5–V8).
- Persisting `GeologyState` (see §4.14).
- Rendering terrain in the Godot preview.
- Sediment, volcanism and tectonic stress fields, isostasy, erosion beyond the
  diffusive approximation, time-evolving tectonics.
- The simulation-mode scheduler (separate task).
- Resources and any civilization system.

## 8. Environment notes

- Configure and test: `cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON`,
  `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- On Linux 6.x kernels with 32-bit mmap randomisation, sanitizer binaries
  built by Clang 14–17 hang at start-up; run them as
  `setarch -R ctest --test-dir build-sanitize`. CI lowers `vm.mmap_rnd_bits`.
- Stop and ask before adding a dependency, changing an accepted ADR's
  decision, changing a §4 decision, or weakening an existing test. If an A5
  or A10 threshold cannot be met with the §4 recipe, report the measured
  values instead of tuning constants until it passes.

## 9. Report when done

- Files added and changed, per step.
- Result of each acceptance item A1–A13, with the measured values for A5,
  A6, A9, A10, A11 and A13.
- The CLI diagnostics for `earth_like` at L5 and L6 with one fixed seed.
- Anything not met, and why; anything in the specification or ADRs that
  turned out to be ambiguous or wrong.

## 10. Completion note (2026-09-28)

Complete on branch `m2-02-terrain`. All of A1–A13 pass; measured values are
in ADR-0005 §9.4 and the audit.

**Amendments approved by the project owner.**

1. §4.3: plate seeds keep a minimum angular spacing of
   `plate_seed_min_spacing_factor` × √(4π/P) rad (default 0.5). Without it,
   A3 failed for some seeds (smallest plate 0.44 % at L4, 0.28 % with 40
   plates); with it the smallest plate is 2.9–4.2 % (12 plates) and 0.85 %
   (40 plates).
2. §4.11 / ADR-0005 §4.1: the world ocean is the below-sea component that
   contains the deepest cell. The largest-area rule is not monotone in sea
   level (ADR-0005 §9.2).
3. §4.8–4.10 starting values: continental base +800 m (was +300), continental
   roughness 100 m RMS and sub-cell roughness 120 m RMS (were 250 and 300),
   passive-margin width 400 km (was 150). The Godot terrain view showed
   continental interiors speckled with sea. With 40 % continental crust and a
   29 % land target, about 28 % of continental crust is flooded whatever its
   height (the sea-level solve absorbs any uniform uplift); the old values
   made interiors as low and rough as margins, so that flooding scattered
   inland. Measured at L6 over three seeds (land 29 %), share of continental
   crust more than 400 km from oceanic crust that is majority ocean:

   | Variant | Inland flooded | Within 400 km flooded |
   |---|---|---|
   | Old values | 16.6 % | 43.0 % |
   | Base +800 m only | 12.0 % | 50.1 % |
   | Median-quantile connectivity instead of lowest (ADR change) | 22.2 % | 55.0 % |
   | Roughness / 2.5 only | 11.0 % | 52.5 % |
   | Margin 400 km only | 14.3 % | 47.5 % |
   | **Adopted: all three value changes** | **3.8 %** | 70.0 % |

   The connectivity rule of ADR-0005 is unchanged: the stricter variant made
   inland flooding worse. A5, A9 and A10 still pass (ADR-0005 §9.4).

**Interpretations where §4 was silent or ambiguous.**

- §4.4: the drawn 1–10 cm/yr speed is the Euler rotation's equatorial surface
  speed ωR; cells nearer a plate's pole move more slowly.
- §4.5: relative motion is evaluated for both plates at the edge midpoint,
  with the geometric normal there. Convergence and the sense of shear are the
  same from either side; the relative velocity is exactly antisymmetric.
- §4.7: the age–depth curve joins Parsons & Sclater's √t branch at 70 Myr to
  an exponential towards 6,400 m that matches its value and slope (τ ≈ 46 Myr),
  since their two published branches differ by about 78 m there.
- §4.8: cells adjacent to a boundary (or to a crust change within a plate, for
  passive margins) are at distance zero, so structures narrower than an L5
  cell (about 240 km) are carried by the first row of cells. "Half-width" is
  the half-width at half-maximum. Scaling uses the closing speed
  (convergent), opening speed (divergent) or shear speed (transform) divided
  by the class median, clamped to at most 2. The island-arc half-width
  (100 km) was not specified.
- §4.8–4.10: roughness amplitudes are RMS values of unit-RMS normalised fBm
  (`normalized_fbm`). The plate-growth term and the continental propensity use
  raw fBm as written.
- §4.9: each erosion step is split into equal explicit sub-steps where the
  step would exceed half the mesh's stability limit, so the diffusion length
  √(2κNΔt) (100 km by default) does not depend on resolution.
- `below_fraction` counts area strictly below the level; quantiles that
  coincide flood at once (ADR-0005 §9.3).

**Observations, not tuned.**

- Raw fBm has a standard deviation of about 0.15, so `a = 0.5` varies the
  growth cost by only about ±7 %. Plate boundaries are therefore nearly
  straight (Voronoi-like). Using `normalized_fbm` there would change §4.3.
- With random Euler poles, about half of the boundary length classifies as
  transform (L5, seed 20260928: 90,500 km transform, 52,100 km divergent,
  42,000 km convergent).
- Along-mesh path lengths exceed the geodesic by up to about 17 % (measured
  1.174 at L4), so structure widths are slightly narrower than their nominal
  values in some directions.
- The §4.10 barycentric recipe gives the cell centre about 35 % of the
  sub-cell weight, so a cell's hypsometric mean blends with its neighbours
  through the corner means (up to 3.6 km from the structural elevation at L3,
  next to trenches and mountain belts).
- Inland below-sea-level depressions (after amendment 3): 12 at L5 and 159
  at L6 for seed 20260928. They are dry land until drainage (M2-03) and
  hydrology (M9).
- With amendment 3 the ocean shallower than 200 m falls to 4.1–4.5 % of the
  ocean (from 6.7–8.1 %): A10 still passes, but shelves are narrower than
  Earth's (about 7 %).

