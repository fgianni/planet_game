# R1-05 — Coastline-stable terrain LOD

- **Track:** rendering, following R1-04 (ADR-0022 §4.8–4.9)
- **Status:** architecture accepted 2026-11-02 (ADR-0030); implementation
  and acceptance tests pending. R1-05 is not complete.
- **Implementation gate:** ADR-0030 selects a shared coastline. Its
  pre-acceptance feasibility prerequisite was explicitly waived, **not** its
  rendered handoff, contour fidelity, raised seam, zoom, style and cost gates.
  Do not change the production tile contract until those gates pass.
  No PlanetSim solver, terrain field meaning or PSNAP change is permitted.

## Measured problem

`tools/render/coast_lod_probe.cpp` compares the existing filtered mean-height
surface with the detailed rank/hypsometry surface in coastal L4 cells, seed
20260928. The first detailed cube tile is level 2. Of 532 sampled points,
100 (**18.8 %**) change land/water classification at the handoff; the mean
height difference is **365 m**, maximum **2467 m**. This is a change in the
presentation surface, not in the authoritative land fraction or sea level.
It is too large to hide with a thinner shader edge or a colour adjustment.
There is also a separate planet–region handoff: both styles' cell-mesh
water colour comes from interpolated `surface_class` plus a coast noise
field, while tile water colour comes from the tile's interpolated height.
The cell mesh geometry uses the cell land fraction to choose its height.
The same shoreline must be reconciled across this cross-fade too; a shared
tile contour alone cannot solve it.

Build and run the diagnostic from the repository root:

```bash
c++ -std=c++20 -O3 -ffp-contract=off -I . \
  tools/render/coast_lod_probe.cpp build-godot/libPlanetSimPresentation.a \
  build-godot/libPlanetSim.a -lzstd -pthread -o build-godot/coast_lod_probe
./build-godot/coast_lod_probe
./build-godot/coast_lod_probe 6
```

## Decision history

The dated probe notes below preserve their historical proposal-stage
language. ADR-0030's later acceptance waived only the *timing* of its
feasibility prerequisite; none of the failed or unmeasured gates became a
pass, and no production candidate was accepted.

Two credible routes affect the already accepted R1-04 tile contract:

1. A level-independent, spatially referenced coast mask and compatible
   geometry at all visible tile resolutions. This could keep the shore fixed
   but costs additional memory, generation and a rule for where the mask and
   hypsometric rank meet.
2. A band-limited hierarchy of the *same* relief with geomorphing at LOD
   transitions. This avoids a sudden mean-to-detail jump but must preserve
   neighbouring tile seams, the V4 hypsometry bound, deterministic tile
   generation and the existing tile budget.

Do not choose one by changing `tile_filtered_cells` alone: moving the abrupt
switch does not remove it, and sampling all high-frequency relief on coarse
tiles aliases the coast. The ADR must specify the coastline error metric,
transition, cost ceiling and whether the rendered coast is defined by the
full-rank field or by a separately filtered representation.

ADR-0030 chooses route 1: the existing detailed field defines the visible
coast. The shared constraint, georeferenced handoff and 64 MiB extra cache
ceiling still need validation before implementation. The architecture was
accepted 2026-11-02 with that prerequisite explicitly waived, not passed.

### First feasibility spike (2026-10-29; no tile-code change)

The read-only probe now takes L4 or L6 and samples east–west transects across
coastal cells. On each 0.6-cell transect, it compares linearly reconstructed
crossings at 8, 16, 32 and 64 intervals against 256 full-rank intervals.
The L4 baseline remains 100/532 land/water flips at the old handoff.

| Level, coastal transects | Samples per transect | Wrong-side fixed samples | Missed reference crossings | Largest matched crossing shift |
|---|---:|---:|---:|---:|
| L4, 76 | 8 | 364/19426 | 31/78 | 19.7 km |
| L4, 76 | 32 | 44/19426 | 15/78 | 2.61 km |
| L4, 76 | 64 | 19/19426 | 8/78 | 2.25 km |
| L6, 69 | 8 | 202/17600 | 25/60 | 6.03 km |
| L6, 69 | 32 | 41/17600 | 13/60 | 773 m |
| L6, 69 | 64 | 14/17600 | 6/60 | 335 m |

The 256-interval reference is only a denser *sample*, not an exact contour.
Transects miss 2D loops, branches, cube-face seams and the actual tile
geometry; no pixel displacement or memory/flight cost was measured. A fixed
64-sample-per-transect raster cannot meet ADR-0030's no-flip goal here.
Investigate adaptive contour refinement with shared georeferenced edge
coordinates before accepting the decision. The 64 MiB ceiling remains
proposed, not validated.

### Second feasibility spike (2026-10-29; no tile-code change)

`tools/render/coast_contour_spike.cpp` samples one coastal cube-face tile
each at L4 and L6 with an adaptive quadtree. It splits on mixed corner
classification, a mismatched midpoint classification, or height interpolation
error over 200 m or 50 m. Leaf corners approximate the full-rank relief;
independent half-grid interior samples check the resulting classification.
Neighbouring same-level tiles also query the canonical height along their
shared dyadic edge. Compile like the first probe, substituting
`coast_contour_spike.cpp`; use `-DCOAST_GRID=256` for the finer run.

| Level, tile | Grid | Error trigger | Leaves | Wrong-side independent samples | Of those, in finest leaves | Shared-edge height disagreement |
|---|---:|---:|---:|---:|---:|---:|
| L4, face 0/4/5/0 | 128 | 200 m | 2155 | 89/16342 | 80 | 0 m |
| L4, face 0/4/5/0 | 256 | 50 m | 6268 | 114/65370 | 82 | 0 m |
| L6, face 0/6/15/0 | 128 | 200 m | 649 | 8/16372 | 8 | 0 m |
| L6, face 0/6/15/0 | 256 | 50 m | 1555 | 19/65485 | 17 | 0 m |

The raw mismatch count rises with four times as many independent evaluation
points, though the *rate* falls. Samples at 1 m or less from sea level are
excluded. Equal edge heights prove only that the canonical field is sampled
consistently on one same-face, same-level edge; this prototype has **not**
built clipped coastal triangles, measured a rendered displacement or tested
mixed-level/cube-face seams. Nor have these leaves been costed as a cache.
At this resolution the proposed no-flip gate still fails. The next spike
must resolve fine-leaf mismatches and build a real shared contour/geometry
contract, including the independent planet-band shader mask, before ADR-0030
can be accepted.

### Planet–region shader handoff audit (2026-10-29)

Both styles' cell-mesh shader colours water from the interpolated
`surface_class` fraction and `detail_seed_coast`; tile water is instead
coloured from interpolated tile height (`detail_geometry_water`). The cell
mesh geometry is separately raised only where the cell land fraction is at
least 0.5. Thus the planet–region cross-fade is another independent coast
definition, not covered by the 1D or 2D terrain probes above.

`godot/tests/probe_coast_handoff.gd` holds a coastal L4 focus, seed 20260928,
at 1.45 radii and forces the map style's cell mesh and then settled tiles at
the **same camera**. A blue-colour heuristic changes water/land classification
at 4,852 of 123,904 compared central pixels (3.9%). Images are saved as
`/tmp/opencode/coast-handoff-{cell,tile}.png`. This is an illustrative
offscreen comparison, **not** an acceptance measure: forcing full tiles at
this altitude bypasses the normal dithered band transition; the colour
heuristic is not a georeferenced contour metric. It does confirm a visible
second handoff, so a tile-only repair would be incomplete.

### Clipped-triangle geometry experiment (2026-10-29)

`tools/render/coast_geometry_spike.cpp` generates full-rank, fixed
cube-face samples on one coastal L4 tile and one coastal L6 tile. It splits
mixed-sign grid triangles at sea level, constructs wet and dry polygon
triangles and their 3D vertex positions, then **independently** generates
four children on the same georeferenced sampling lattice. Their shoreline
edge sets are compared after mapping back into parent coordinates. Compile
against the same two libraries as the other probes; `-DCOAST_GRID=256`
selects the finer run. This is a standalone geometry experiment, not a
replacement for the renderer's tiles.

| Level, parent tile | Grid | Coastal squares | Shore segments | Parent vs children | Wrong-side independent midpoints | Extra coastal-vertex estimate at 72 B each | Total probe time* |
|---|---:|---:|---:|---|---:|---:|---:|
| L4, 0/4/7/0 | 128 | 524 | 881 | identical | 66/16356 | 0.61 MB | 0.19 s |
| L4, 0/4/7/0 | 256 | 1129 | 1887 | identical | 73/65465 | 1.30 MB | 0.69 s |
| L6, 0/6/16/0 | 128 | 720 | 1107 | identical | 65/16346 | 0.79 MB | 0.51 s |
| L6, 0/6/16/0 | 256 | 1515 | 2351 | identical | 68/65367 | 1.67 MB | 2.11 s |

\* One parent, four independent children, midpoint validation and a shared
same-face edge; measured on this machine, not an L6 flight benchmark. The
sampled shared edge agrees to 0 m, and generated sea-level vertices' radius
error is at most 2.3e-16 planet radii. The byte estimate uses the existing
72-byte tile-vertex layout; it is **not** a measured Godot cache allocation
or a global coastline budget. This candidate still misses fine features
between grid vertices and cannot meet the proposed no-flip gate. At grid 256,
the largest signed height of a wrong-side midpoint is 10.7 m at L4 and
6.1 m at L6; converting that into horizontal shoreline displacement needs
the local slope and a rendered test. Its local
per-patch cost cannot be extrapolated safely to the 64 MiB cache ceiling.
No mixed-level neighbours, cube-face seams, planet-band mesh, skirts,
GPU frame times or actual water-colour rasterisation were tested. ADR-0030
remains proposed; do not merge this uniform clipped mesh into R1-04.

### Screen-space check on the failed candidate (2026-10-29)

For each wrongly classified independent midpoint, the geometry probe now
computes its great-circle distance to the nearest segment of the generated
shore. At the 256² grid, that distance is p95/max **631 m / 27.8 km** at L4
and **176 m / 4.09 km** at L6. With an illustrative 650 px focal length and
30 km nadir distance, p95 is about **14 px / 3.8 px** respectively. These
are *distances from wrong-side samples to this candidate coast*, **not**
measured displacement between its contour and the exact full-rank contour;
the large maxima can be missed isolated shore features. A real camera may
project them differently. Even this lower-level diagnostic is much larger
than the proposed 1 px handoff target. The uniform-grid clipping candidate
is rejected; a new representation and actual rendered zoom test are needed.

### Sparse adaptive coastal-strip candidate (2026-10-29)

The 2D probe now clips only mixed-sign leaves into coastal triangles and
estimates their vertex cost in the existing 72-byte tile format. It also
measures wrong-side points against the nearest generated segment (a rough
distance estimate, **not** exact-contour displacement). At the 50 m height
trigger:

| Level, tile | Max grid | Leaves | Coastal leaves | Wrong-side independent points | Clipped coast vertices | Vertex estimate |
|---|---:|---:|---:|---:|---:|---:|
| L4, 0/4/5/0 | 128 | 3262 | 836 | 89/16342 | 12546 | 0.90 MB |
| L4, 0/4/5/0 | 256 | 6268 | 1736 | 114/65370 | 25938 | 1.87 MB |
| L4, 0/4/5/0 | 512 | 11536 | 3557 | 171/261487 | 53370 | 3.84 MB |
| L6, 0/6/15/0 | 128 | 838 | 234 | 8/16372 | 3393 | 0.24 MB |
| L6, 0/6/15/0 | 256 | 1555 | 484 | 19/65485 | 7128 | 0.51 MB |
| L6, 0/6/15/0 | 512 | 3019 | 987 | 12/261930 | 14562 | 1.05 MB |

The ratio falls with resolution, but **every tested grid misses coast points**.
The corner/midpoint error criterion cannot certify that an all-water or
all-land leaf contains no small island or inlet. Some wrong-side samples are
kilometres from any generated segment, so a numerical tolerance near an
already-drawn contour cannot excuse them. These memory figures exclude the
adaptive-tree/sample cache, normals, seam stitching, GPU upload and the
planet-band handoff. There is no demonstrated 64 MiB global budget, and a
larger grid alone cannot prove the proposed no-flip gate. Do not implement
this candidate as the coastline system.

The next prototype uses ADR-0030's **screen-space** gate instead of demanding
literal pointwise equality at every scale: at a fixed camera, the coastline
must move by at most one pixel across a handoff; an island/inlet of at least
four pixels may not appear or disappear merely because of LOD. Points more
than one projected pixel from the canonical coast must still classify the
same way. This is a proposed acceptance rule, not yet demonstrated.

### Screen-space baseline for the current renderer (2026-10-29)

`tools/render/measure_coast_handoff.py` compares regenerated images from
`godot/tests/probe_coast_handoff.gd` using the map style's blue-water colour
heuristic. It counts changed components and uses eight-connected pixel
distance to the visible shoreline in each mask. Run:

```bash
/home/fred/src/Godot_v4.7.2-stable_linux.x86_64 --path godot \
  --script tests/probe_coast_handoff.gd --rendering-method gl_compatibility
python3 tools/render/measure_coast_handoff.py \
  /tmp/opencode/coast-handoff-1_45-cell.png \
  /tmp/opencode/coast-handoff-1_45-tile.png
```

At the fixed L4 coastal camera (1.45 radii), 4,852/123,904 central pixels
change water class (3.92%); **1,247** disagree more than one pixel inside
*both* masks; the largest connected change is 1,538 pixels, with 47 changed
components of at least four pixels. Re-running the probe produced the same
counts. A component of *difference* is not necessarily a missing island—it
may be a shifted edge. These are forced cell-only versus settled tile-only
views, not the normal dithered zoom. The final gate needs a georeferenced
reference, both styles, actual camera motion and pixel-to-world displacement;
these baseline figures alone do not pass or fail the proposed gate.

The probe also freezes the same focus at three progressively closer poses,
capturing cell-only, tile-only and half-blended images after tiles settle.
With the Python image classifier (which quantises PNG colours, so it can
differ from Godot's in-memory count by one pixel):

| Camera distance / altitude | Deepest tile level | Changed / central pixels | Changed beyond 1 px in both masks |
|---|---:|---:|---:|
| 1.45 radii / 2,867 km | 5 | 4,852 / 123,904 (3.92%) | 1,247 |
| 1.25 radii / 1,593 km | 6 | 10,183 / 123,904 (8.22%) | 5,530 |
| 1.10 radii / 637 km | 7 | 6,745 / 123,904 (5.44%) | 5,083 |

These are three **fixed-camera endpoint comparisons**, not adjacent frames
of an actual zoom; the half-blended images are visual fixtures only. The
counts are colour-heuristic differences and cannot establish a 1 px contour
displacement, four-pixel island topology, georeferenced agreement, mixed-LOD
seam integrity or performance. Candidate geometry is still needed before
the feasibility gate can be evaluated.

### Shared-mask shader experiment (2026-10-29; probe only)

`tools/render/coast_mask_spike.cpp` samples the **detailed** relief's
sea-level classification at fixed equal-angle cube-face texel centres and
writes six faces into a 3×2 atlas. The modified *probe script*, not the
renderer or style pack, substitutes a shader lookup of that same atlas for
both cell and tile water colour. Build/run from the repository root:

```bash
c++ -std=c++20 -O3 -ffp-contract=off -I . \
  tools/render/coast_mask_spike.cpp build-godot/libPlanetSimPresentation.a \
  build-godot/libPlanetSim.a -lzstd -pthread -o /tmp/opencode/coast_mask_spike
/tmp/opencode/coast_mask_spike 512 /tmp/opencode/coast-mask.ppm
python3 -c 'from PIL import Image; Image.open("/tmp/opencode/coast-mask.ppm").save("/tmp/opencode/coast-mask.png")'
/home/fred/src/Godot_v4.7.2-stable_linux.x86_64 --path godot \
  --script tests/probe_coast_handoff.gd --rendering-method gl_compatibility \
  -- --coast-atlas=/tmp/opencode/coast-mask.png
```

| Pose | Original endpoint changes | Shared 512²-face mask | Shared 1024²-face mask |
|---|---:|---:|---:|
| 1.45 radii | 4,852 | 39 (0 deep) | 50 |
| 1.25 radii | 10,183 | 108 (0 deep) | 147 |
| 1.10 radii | 6,745 | 8 (0 deep) | 10 |

Each row compares cell-only and settled-tile water **colour** over 123,904
central pixels. “Deep” means differing more than one pixel inside *both*
binary masks. The 512² atlas used 1,572,864 canonical samples, took 8.6 s
to generate on this machine and occupies 4.72 MB as uncompressed RGB; at
1024² it used 6,291,456 samples, took 15.1 s and occupies 18.9 MB.
These are global mask payloads, **not** total GPU allocation, runtime frame
times or tile-cache measurements.

Independent offset samples (0.8 texel from each sampled texel's corner,
one in every 4×4 texels; points within 1 m of sea level excluded) disagree
with the nearest mask texel at **728/98,259** points for 512² and
**1,511/393,073** for 1024². The rate falls, but neither raster preserves
all local sea-level classifications even on these sampled points.

The lower-resolution mask is **not** demonstrated faithful to the canonical
coast: comparing its tile render to the 1024² version changes 330, 671 and
300 pixels at the three poses. At 1.10 radii, 28 changed pixels are more than
one pixel inside both masks; components of ≥4 changed pixels can also be
edge shifts rather than topology changes. A 1024² atlas is itself only a
sampled approximation, not a reference bound. Moreover the shader changes
only *colour*: the old filtered/detailed heights and cell/tile geometry,
occlusion, skirts and picking remain inconsistent. Rendering one common
mask on contradictory geometry cannot satisfy ADR-0030's geometry agreement
or the final georeferenced/island gate. The experiment establishes that a
shared georeferenced colour classifier can remove most of the second
handoff; it does **not** justify merging a fixed global raster.

### Mixed-level and cube-face edge experiment (2026-10-29)

`tools/render/coast_seam_spike.cpp` independently samples detailed relief
along a fine tile edge and the adjacent **coarse** tile edge, at the same
dyadic cube-face positions. The first mixed-level edge with a crossing-count
discrepancy on face 0 is reported at L4 and L6; a separate whole-face
comparison checks face 0's right edge against face 5's left edge. Build it
against the same two libraries as `coast_geometry_spike.cpp`.

| Level, fine tile | Fine vs coarse linear crossings | Odd points flipped by coarse interpolation | Largest fine-to-nearest-coarse crossing gap* | Constrained crossing difference |
|---|---:|---:|---:|---:|
| L4, face 0 / 4 / 11 / 10 | 6 / 4 | 2 | ~9.31 km | 0 m |
| L6, face 0 / 6 / 55 / 0 | 6 / 4 | 1 | ~1.80 km | 0 m |

\* This pairs each fine crossing with its nearest coarse crossing, including
when no corresponding coarse feature exists; it is **not** a matched contour
displacement. Inserting the 64 missing boundary heights along the 128-step
fine edge on the coarse neighbour recovers the six crossings, independently
and at the same positions. Common sampled heights agree to the reported
0 m. A one-coarse-grid-cell-wide ribbon uses three triangles per two fine
boundary intervals, clipped into wet/dry polygons. On the two chosen edges,
its 192 pre-clip triangles each cover all 128 fine boundary intervals exactly
once; all six clipped sea-level intersections match the fine-side contour;
its 3D boundary vertices have a 0 m maximum gap and no clipped triangle has
inverted radial winding. Compared with an ordinary clipped coarse strip,
it adds 210 / 222 triangle-list vertices (about 15.1 / 16.0 KB using the
existing 72-byte layout). These are **local** counts before normals, skirts,
GPU buffers and neighbouring-strip corner handling, not an L6 cache budget.
The tested face 0 / face 5 shared edge has 9/9 (L4) and 37/37 (L6)
crossings, also at 0 m difference. This is a standalone stitched strip,
**not** an integrated neighbour tile, computed normal, skirt, GPU-rendered
seam, full cube-face coverage or global cache-cost proof. Along with
the shared colour mask it suggests a two-part candidate (common visible
classification plus constrained coast geometry), but the finer-grid
canonical feature and cell-band geometry problems remain open. ADR-0030
remains proposed.

### All-face and fixed-atlas seam audit (2026-10-29)

`tools/render/coast_face_seam_spike.cpp` checks all **24 oriented face edges**
(each physical edge twice) at a 128-samples-per-level-L-tile boundary lattice.
It independently constructs each side from its own equal-angle cube-face
coordinates; checks paired heights, positions, classifications and linearly
interpolated sea-level crossings; and checks all eight three-face corners.
Two seeds at L4/L6 have **zero** classification flips, unmatched crossings,
or measured position/height/crossing discrepancy at the sampled canonical
boundary points (246/396 paired crossings for seed 20260928 and 252/280 for
seed 42 at L4/L6). This validates the sampled field's boundary coordinate
contract, **not** the rendered seam triangles and corners.

The same audit samples the *actual nearest texel-centre rule* of the
probe-only fixed atlas on each side of those boundaries:

| Seed, mesh | 512² face atlas | 1024² face atlas |
|---|---:|---:|
| 20260928, L4 | 228 / 12,288 | 240 / 24,576 |
| 20260928, L6 | 346 / 12,288 | 394 / 24,576 |
| 42, L4 | 230 / 12,288 | 284 / 24,576 |
| 42, L6 | 234 / 12,288 | 284 / 24,576 |

Each numerator counts oriented border texels with opposite water class;
each physical seam is counted twice. The longest runs are five to seven
texels. Border texels lie on opposite **sides** of the exact seam, so this
does not show the canonical field is discontinuous or translate directly
to rendered pixels. It *does* show that the face-local nearest-texel mask
cannot be assumed continuous merely because its face coordinate lookup is
georeferenced. A viable shared representation needs a common boundary
constraint and explicit face/corner ownership, plus rendered seam and
feature-size tests. Increasing this fixed atlas from 512² to 1024² did not
eliminate the issue; it remains an experimental classifier, not a solution.

An additional **edge-pinning thought experiment** samples the canonical
field at each face-border interval instead of at the nearest texel centre:
the two opposite edge sequences then have zero classification differences
in these runs. But a face-corner atlas texel belongs to two such edges.
For seed 42 at L6, their proposed values conflict at **2 of 24**
within-face corner ownership checks at both resolutions (zero in the other
tested runs). No atlas pixels or renderer were changed by this calculation.
Pinning an edge without a deterministic corner owner and a measured
transition to the interior merely moves the seam problem; it does not
establish a crack-free colour or geometry solution.

### Offscreen clipped-geometry raster probe (2026-10-30)

`tools/render/coast_render_mesh_spike.cpp` exports an L4 or L6 face-0 coastal
tile as independently built parent and four-child clipped triangle lists.
Both use the same 128-step-per-parent georeferenced lattice (children take
every second one of their 128 steps); a separate child mesh samples *every*
child step as a finer detailed-relief reference. `godot/tests/
probe_coast_render_mesh.gd` draws the three in an unshaded 512² offscreen
viewport at the **same** camera pose; the comparison script excludes a
two-pixel outer silhouette. Seed 20260928, parent keys 0/4/7/0 and
0/6/16/0, OpenGL 3.3, RTX 2080 Ti:

| Mesh | Camera height (radii) | Common interior px | Parent–children different px | Parent–fine different px | Fine difference >1 px from fine coast | Largest mismatch component px |
|---|---:|---:|---:|---:|---:|---:|
| L4 | 0.15 | 59,415 | 0 | 155 | 0 | 6 |
| L4 | 0.08 | 199,633 | 0 | 441 | 2 | 22 |
| L4 | 0.04 | 258,064 | 0 | 954 | 81 | 67 |
| L6 | 0.04 | 52,578 | 0 | 125 | 0 | 4 |
| L6 | 0.02 | 191,350 | 0 | 538 | 1 | 14 |
| L6 | 0.01 | 258,064 | 0 | 1,005 | 49 | 58 |

The parent and children happen to rasterise identically at these poses;
they share the same sampled lattice, so this is **not an independent coast
fidelity result**. The fine mesh is a one-level-denser *sampled relief*
reference, not a converged mathematical contour. Its mismatches include
antialiasing and refined triangulation; connected mismatch components are
not automatically whole islands or inlets. At the close poses the coarse
classifier visibly exceeds the proposed one-pixel fidelity tolerance.
These probes have no planet-cell handoff, mixed-LOD ribbon, cube-face joins,
texture atlas, either production style, zoom flight, or cost measurement.
The raster gate and ADR-0030 acceptance therefore remain open; no production
tile implementation is authorized by this result.

### Rendered mixed-level ribbon (2026-10-30)

The same exporter accepts a `seam` argument. It uses the independently
sampled face-0 fine/coarse edges previously measured at 6 versus 4 linear
crossings (L4 fine tile 11/10, L6 55/0). Its three outputs are fine-left
with ordinary coarse-right (baseline), fine-left with a
three-triangles-per-two-steps stitched coarse-right (candidate), and
fine-left with fine-right (reference). The strip is
eight fine grid columns wide on each side; the ribbon occupies the first
two columns to the right. This experiment draws *both* classes on the same
sphere, deliberately suppressing vertical relief to isolate the rasterised
coast footprint. Same unshaded 512² offscreen renderer and fixed poses:

| Mesh | Height (radii) | Baseline–ribbon changed px | >1 px from ribbon coast | Ribbon–fine changed px | >1 px from fine coast |
|---|---:|---:|---:|---:|---:|
| L4 | 0.02 | 9 | 0 | 72 | 0 |
| L4 | 0.01 | 46 | 0 | 290 | 15 |
| L4 | 0.005 | 169 | 0 | 682 | 250 |
| L6 | 0.005 | 87 | 37 | 378 | 189 |
| L6 | 0.0025 | 344 | 241 | 1,580 | 1,153 |
| L6 | 0.00125 | 1,378 | 1,171 | 3,216 | 2,681 |

The independent boundary heights match to within 1e-6 m and the ribbon
preserves the six fine-edge crossings, but that **does not** constrain the
shoreline through its two-column interior. Its different diagonals and
unsampled relief change resolvable raster pixels, particularly at L6;
the stitched result is not closer overall to this fine reference. Distance
here is pixel-centre distance to a binary-mask boundary, not a tracked
contour displacement, and connected differences are not proof of whole
islands. This ribbon is a **failed rendering candidate** under the proposed
screen-space gate. An edge-only stitched strip cannot be adopted without an
interior contour/transition constraint and a new rendered test. Real raised
terrain, normals, skirts, all-face joins, planet handoff, and zoom still
need independent validation.

### Fine interior strip-width sensitivity (2026-10-30)

The exporter also has `seam2`, `seam4` and `seam8` probe modes: they replace
the first 2/4/8 fine columns of the right-hand coarse side with independently
sampled fine triangles, then return to the original coarse grid. `seam8`
is **the entire tested right-hand patch** and therefore reproduces the fine
reference by construction; it does not demonstrate a bounded transition.
The same L6 fixed poses give:

| Fine columns | Additional triangle-list vertices vs coarse baseline (L6) | Candidate–fine px at heights 0.005 / 0.0025 / 0.00125 | >1 px from fine coast at closest pose |
|---|---:|---:|---:|
| 2 | 1,242 | 322 / 1,357 / 2,329 | 1,954 |
| 4 | 2,490 | 237 / 1,015 / 935 | 849 |
| 8 (all visible right patch) | 4,914 | 0 / 0 / 0 | 0 |

At L4 the extra vertices are 1,242 / 2,478 / 4,974; candidate–fine
differences at heights 0.02 / 0.01 / 0.005 are 59 / 226 / 436 for width
2, 29 / 111 / 0 for width 4, and 0 throughout for width 8. The width-4
zero at the closest pose is **camera cropping**: the unresolved outer
transition lies outside that viewport, not evidence it is solved. At L6,
the far-side width-2/4 edges each have 1/2 odd water-class flips relative
to coarse interpolation, respectively, even though this flat-sphere raster
has no changed visible-background pixels at the sampled poses. Its
maximal fine-midpoint/coarse-chord geometric gap is ~0.12 m (L4 ~1.64 m);
raised relief can be worse. These are local counts, not global memory or
frame costs. Even where a fine band reduces reference error, swapping from
the baseline to it still changes resolvable pixels. A safe hand-back needs
a contour-aware, topologically matched *far* boundary as well as the near
edge; merely widening a uniform fine band moves the unresolved seam.

### Second-ribbon hand-back experiment (2026-10-30)

`seam2b`, `seam4b` and `seam6b` keep the near edge and 2/4/6 interior
columns on the fine lattice, then insert a clipped, three-triangle ribbon
from their **far** fine edge to the next coarse grid column. The remaining
right-hand columns stay coarse. The far ribbon uses fine sampled heights
along its near edge and coarse sampled heights at its outer edge; it is a
local topological match, not an independent canonical contour guarantee.
At the same L6 poses, candidate versus fine reference:

| Fine columns + far ribbon | Extra triangle-list vertices vs coarse baseline | Different px at heights 0.005 / 0.0025 / 0.00125 | >1 px from fine coast, closest pose |
|---|---:|---:|---:|
| 2 | 1,452 | 339 / 1,404 / 2,522 | 2,149 |
| 4 | 2,676 | 177 / 759 / 78 | 42 |
| 6 | 3,906 | 126 / 547 / 0 | 0 (outer transition cropped) |

The four-column far ribbon improves the closest-pose L6 difference from
935 to 78 pixels versus a plain fine-to-coarse join, but the remaining
pixels span x=475..511 on the 512² image, at the far side of the visible
patch, and **42** are over one pixel from the fine contour. At the medium
pose 759 pixels still differ (567 deeper than one pixel). The width-6
zero at the closest pose is again camera cropping: at 0.005/0.0025 radii
its outer ribbon differs by 126/547 pixels. The baseline-to-candidate
handoff is also not stable: width 4 changes 202/819/2,784 pixels at the
three L6 poses, with 68/516/2,263 farther than one pixel from the candidate
coast. This is not an acceptable replacement of the baseline.

L4 width 4 differs from the fine reference by 46/173/0 pixels at
0.02/0.01/0.005 radii (the last zero is cropped); at the middle pose it is
**worse** than the plain join's 111 pixels, and 43 differences are deeper
than one pixel. No foreground-background differences were detected in
these flat-sphere renders, but subpixel gaps, raised-terrain occlusion,
normals and skirts remain untested. These small-patch vertex counts are not
a global cache or frame-time measurement. A near/far ribbon improves one
local pose but does **not** provide a stable georeferenced coast: the
coarse interior still misses relief between its samples. Further work
requires a contour-aware *interior* hierarchy and broader fixed-pose tests,
not acceptance of ADR-0030 on this partial result.

### Coast-following adaptive squares (2026-10-30)

`adaptive0` refines a coarse 2×2 fine-sample square if its nine sampled
heights include both signs; `adaptive1` additionally refines the one-square
neighbourhood (including diagonals). At the same two face-0 mixed-level
patches, the **flat** unshaded renders match the one-level-fine reference
in all six fixed poses, while requiring fewer vertices than a full fine
right patch:

| Mesh | Coastal / total coarse squares | Refined squares (no halo / halo) | Extra vertices (no halo / halo) vs coarse baseline | Refined/coarse T-junctions (no halo / halo) | Max raised midpoint/chord gap (no halo / halo) |
|---|---:|---:|---:|---:|---:|
| L4 | 35 / 256 | 35 / 88 | 996 / 1,950 | 53 / 39 | 113 / 53 m |
| L6 | 26 / 256 | 26 / 67 | 774 / 1,512 | 42 / 35 | 13 / 11 m |

These gaps compare the independently sampled **raised** midpoint with
the unsplit coarse chord at every refined/unrefined edge; there are zero
midpoint water-class flips in this limited audit. The raster probe flattens
both land and water onto the sphere, so its zero changed-background pixels
do **not** establish a crack-free raised terrain. Transition edges still
need matching tessellation, normals and skirts. Nor has the coarse **source**
been replaced: the baseline-to-adaptive L6 masks change 355/1490/2862
interior pixels at heights 0.005/0.0025/0.00125 radii (L4: 65/244/513).
That is a resolvable handoff, not parity between LODs.

The zero comparison is also against a reference built on **the same fine
lattice**. A separate independently sampled twice-fine right patch still
differs from `adaptive1` by 117/468/1095 pixels at the three L6 poses
(7/146/605 over one pixel from its contour), and 50/197/431 at L4
(0/1/110 over one pixel). This does not prove that the twice-fine mesh is
the converged mathematical contour; it establishes that the zero at the
previous resolution is not a fidelity result. A usable scheme must share
the adaptive coast across *both* LOD representations, handle T-junctions
in raised geometry, and pass multi-seed, all-face, zoom and cost gates.

### Conforming adaptive-edge probe (2026-10-30)

`adaptive1s` replaces a uniform coarse square adjoining a refined square
with a centre fan whose boundary includes the neighbour's sampled midpoint;
`adaptive1sr` draws the same mesh at its true positive relief (unexaggerated)
instead of flattening it. An exporter invariant checks that each transition
midpoint is emitted by **both** refined and coarse triangle lists. It caught
an initial one-pass neighbour-ownership bug: refinement decisions must be
completed for the whole patch *before* any transition geometry is emitted.
The corrected two-pass probe checks 39/35 transition edges at L4/L6;
minimum midpoint uses are 3 fine and 2 coarse, versus zero coarse uses in
the unstitched control. The unsplit raised midpoint/chord gaps on these
edges were up to 53/11 m. This checks vertex sharing at the local patch
edges, **not** normals, skirts, entire mesh edge incidence, or cube-face
corners. It adds 315 (L4) / 279 (L6) triangle-list vertices over the
corresponding unstitched one-halo adaptive patch (2,265/1,791 over the
ordinary coarse baseline).

At the earlier six fixed poses the conforming mesh has zero *interior*
water-mask differences against the same-lattice fine reference with both
flat and raised geometry. Raised L6 visibility masks disagree at 12 and 4
pixels at the two wider poses, located on the patch silhouette; none at the
closest pose. That is **not** a whole-scene crack or frame-cost test.
Neither the coarse-to-adaptive water-mask jump (355/1490/2862 L6 pixels)
nor the twice-fine reference misses (117/468/1095 at L6) is resolved.
ADR-0030 stays proposed: this is a local mesh-conformity result, not a
coastline-stable production LOD.

### Independently generated coastal parent and children (2026-11-02)

`tools/render/coast_parent_child_spike.cpp` independently samples a parent
and its four child tile keys on a **shared 256-step-per-parent georeferenced
lattice**. Parent non-coastal squares span four samples and child squares
two; mixed-sign blocks and a one-block halo refine to single-step triangles.
Uniform coarse blocks adjoining refined blocks split their edges to use
the fine neighbour's sampled positions. The three exported meshes are the
adaptive parent, the four adaptive children, and a full single-step mesh on
that same lattice. This is a probe, not an R1-04 tile implementation.

At seed 20260928, face-0 parent keys 4/7/0 and 6/16/0, independent parent
and child evaluations agree at all **66,564 paired lattice vertices** per
mesh level: zero class flips and zero reported height or position difference.
Unshaded 512² offscreen water masks at heights 0.15/0.08/0.04 (L4) and
0.04/0.02/0.01 planet radii (L6) have **zero interior parent–child changes**
in both flat and unexaggerated raised modes, and zero changes versus the
same-lattice full-fine mesh. Up to three visibility pixels per wider pose
lie on the patch silhouette, which the two-pixel interior erosion excludes.
The parent and children have genuinely different non-coastal tessellation;
the shared water mask is not obtained by drawing identical complete meshes.

| Mesh | Parent coastal/refined/transition blocks | Four children coastal/refined/transition blocks | Parent / children / full-fine triangle-list vertices | Parent + children sampling/mesh time* |
|---|---:|---:|---:|---:|
| L4 | 252 / 646 / 270 | 536 / 1,444 / 609 | 98,808 / 141,690 / 404,538 | 0.20 s |
| L6 | 356 / 981 / 406 | 732 / 2,070 / 903 | 134,385 / 158,841 / 407,322 | 0.52 s |

\* Single local run, excluding planet/terrain construction, finer reference
generation, Godot mesh upload and rendering; **not** a p95 frame or cache
measurement. Each binary vertex occupies 16 bytes in this probe, not the
production tile layout. These counts may already be too expensive for
streamed coastal tiles without further sparse caching or simplification.

A separate independently sampled **twice-fine** full-parent reference does
not agree exactly. At near/mid/far L4 poses, the adaptive coast differs by
385/184/58 interior pixels, with 1/0/0 more than one pixel from the
reference binary contour. At L6 it differs by 428/236/49 pixels, with
0/0/1 beyond one pixel; the far L6 outlier reaches three pixels from the
binary reference despite being an isolated component of at most two pixels.
This is a *sampled* reference, not a converged canonical contour, and
connected mask changes are not necessarily disappearing islands. No
acceptance claim follows from the same-lattice zero. Several seeds, other
cube faces and corners, actual terrain styles and continuous planet-to-tile
zoom, geometry cracks/normals/skirts, memory and frame costs remain open.

**Child-edge counterexample (same date).** A selectable-key `audit` mode
compares which of the 129 canonical positions along each of the four
internal child-to-child edges is actually emitted as a boundary vertex.
Despite the exact height/class agreement above, independently chosen
one-block halos do **not** always choose matching subdivisions. In raised
mode, seed 20260928 has 9 unmatched child-seam vertices at 0/4/7/0
(one submerged; maximum sampled-vertex-to-opposite-chord gap 14.23 m),
and 19 at 0/6/16/0 (13 submerged; 4.87 m). Seed 42 gives 7, 11 and
46 unmatched vertices at face-1 4/15/0, face-2 4/0/15, and face-4
4/0/0 respectively; the last has 17 submerged and a 12.34 m maximum
gap. Face-5 4/15/15 has zero in this sample; seed 20260928 face-5
4/0/8 has six. The chord gap is a **local geometric proxy**, not a measured
screen-space crack. It demonstrates that identical georeferenced relief
samples and zero interior water-mask differences do **not** establish a
conforming raised mesh or stable seams. A separate face-5, seed-42 raised
render at the three L4 poses also has zero interior parent–child mask
changes, with two silhouette visibility changes at each wider pose; that
render likewise does not certify its outer face edge. The audit covers
selected patches on faces 0, 1, 2, 4 and 5, not all faces or all face-edge
and corner junctions. A common boundary subdivision or explicit stitching
across independently selected halos is required before the seam gate can
pass. Do not replace this diagnostic with the same-lattice mask result.

**Full fine-boundary control (2026-11-02).** `raised-boundary` independently
emits every canonical boundary sample at all four edges of the parent and
each child. A uniform square adjoining such an edge is triangulated as a
conforming centre fan; its coastal/interior classification is unchanged.
This is deliberately a high-cost *control*, not the proposed sparse answer.
Across eight selected L4/L6 cases (seeds 20260928 and 42, faces 0/1/2/4/5),
the four internal child seams have **zero** unmatched canonical vertices,
versus 0--46 without the rule. The parent/children outer perimeter also
has **zero** unmatched vertices, versus 245--269 without it (even an
all-dry seed-42 face-0 patch has 256 due to parent step 4 versus child
step 2). Thus the initial seam audit understated the problem: the ordinary
parent/child outer edge is not conforming either. These counts check
boundary vertex *presence*, not full clipped-edge incidence, normals,
skirts, face-to-face junctions, or a screened crack.

The original raised face-0 L4/L6 pairs and a seed-42 face-5 L4 pair were
rerendered at three fixed poses each. Parent, four children and same-lattice
full-fine meshes have zero water-mask changes on their common visible
pixels; parent/child visibility masks also agree at those poses. This is
still not a continuous zoom, production style, or planet-band check.

| Patch | Added parent vertices | Added four-child vertices | Added binary mesh bytes (16 B/vertex) | Local generation time before → after |
|---|---:|---:|---:|---:|
| seed 20260928, L4 face 0/7/0 | 3,576 | 8,562 | 194,208 B | 0.209 → 0.205 s |
| seed 20260928, L6 face 0/16/0 | 3,249 | 8,397 | 186,336 B | 0.539 → 0.542 s |
| seed 42, L4 face 0/0/0 (no sampled coast) | 3,816 | 9,120 | 206,976 B | 0.185 → 0.187 s |

Single local timings are noise-level, not p95. A rule that spends ~207 KB
of probe triangle-list geometry on a **dry** parent-plus-children group
cannot be assumed to fit the 64 MiB global coastal-constraint allowance.
It fixes the sampled edge-subdivision counterexample at a cost, but does
not clear fidelity or cache gates. Next test a deterministic sparse edge
rule derived symmetrically from both sides of a tile boundary (including
cube-face and mixed-level neighbours), then validate actual triangle edges
and raised renders. Do not silently replace the all-edge control with
another one-sided halo.

**Sparse symmetric edge-strip experiment (2026-11-02).** `raised-sparse`
uses the same physical cube-face grid for parent and children (256 canonical
steps per parent) and independently tests both sides of each two-step edge
segment. It inspects a radius-eight-sample strip in both the tangent and
normal directions for mixed sea-level classification. Every parent boundary
gets at least two-step edges to match the children's unrefined step; only
mixed strips or locally refined coastal blocks use single-step edge
vertices. Cube-face borders conservatively retain the full fine boundary:
their opposing strips are not yet implemented across face coordinates.
This is a measurable heuristic, **not** a proof that radius eight captures
every contour or every future LOD combination.

The independently generated rule gives zero unmatched canonical vertices
on child seams and parent/child outer edges in the previous eight selected
cases. Independently regenerated +x/+y same-face L4/L6 neighbours also
have zero boundary-vertex mismatches in five checked cases. The reproducible
geometry checks are `python3 tools/render/check_coast_parent_child_spike.py
/tmp/opencode/coast_parent_child_spike`: five pinned cases compare baseline,
sparse and full-edge controls, including a dry interior and a face corner.
Raised offscreen parent, children and same-lattice full-fine masks match on
all common visible pixels, with identical parent/child visibility masks,
at nine fixed poses across the original L4/L6 and seed-42 face-5 patches.

| Patch | Sparse extra vertices over baseline (parent + four children) | Sparse extra binary bytes | Full-edge extra vertices | Local baseline → sparse generation time* |
|---|---:|---:|---:|---:|
| seed 20260928, L4 face 0/7/0 | 4,488 | 71,808 B | 12,138 | 0.21 → 0.26 s |
| seed 20260928, L6 face 0/16/0 | 4,746 | 75,936 B | 11,646 | 0.54 → 0.68--0.73 s |
| seed 42, L4 face 0/8/8 (dry interior) | 2,280 | 36,480 B | 12,936 | 0.19 → 0.23 s |

\* Local sequential samples, excluding planet construction, GPU upload and
rendering; the optional same-level neighbour audit is excluded from the
reported timing. These are **probe triangle-list bytes**, not measured
cached-constraint bytes or p95 frame times. Sampling two-sided strips adds
CPU work, and the dry tile still needs a coarser-to-child boundary match.
This candidate fixes a sampled edge-topology counterexample more cheaply
than the full-edge control; the subsequent check below tests local clipped
  edges, but actual cross-face geometry/corners, normals/skirts, mixed levels
  beyond the tested parent/child pair, a continuous zoom, contour fidelity, production
styles, planet-band parity or the 64 MiB cache gate. ADR-0030 is accepted as
an architecture choice; this candidate and R1-05 are not accepted as an
implementation.

**Clipped-edge incidence follow-up (2026-11-02).** The exporter now records
the actual clipped wet/dry polygon segments on each tile's exterior, including
interpolated shoreline intersections and the exported float32 3D endpoints.
It verifies exact-once boundary coverage per tile, then compares the ordered
segments, class and positions across the four child seams, the parent versus
the combined children perimeter, and independently built same-face +x/+y
neighbours. The five pinned `check_coast_parent_child_spike.py` cases report:

| Seed / parent key | Baseline clipped child / outer mismatches | Sparse child / outer mismatches | Sparse same-face mismatches | Boundary coverage errors |
|---|---:|---:|---:|---:|
| 20260928 / 0/4/7/0 | 70 / 529 | 0 / 0 | 0 | 0 |
| 20260928 / 0/6/16/0 | 128 / 532 | 0 / 0 | 0 | 0 |
| 42 / 4/4/0/0 | 217 / 590 | 0 / 0 | 0 | 0 |
| 42 / 0/4/8/8, dry | 0 / 512 | 0 / 0 | 0 | 0 |
| 42 / 5/4/15/15 | 0 / 532 | 0 / 0 | 0 | 0 |

For corresponding sparse intervals the exported float32 endpoints agree
exactly in these same-build runs (reported position delta 0 m). The baseline
mismatch count is **ordered interval disagreement**, not a count of holes or
pixels; coordinates of different intervals are not compared as if they
corresponded. This strengthens the local seam result but still omits
cross-face tile pairs, three-face junctions, GPU rasterisation of complete
neighbouring tiles and skirts/normals. It does not resolve the twice-fine
contour misses or any continuous planet-to-tile handoff. The implementation
gates in accepted ADR-0030 remain open.

**Independent cube-face clipped-edge audit (2026-11-02).** The same exporter
now locates the opposite tile by an independent inverse of the cube-face
frames, accounting for edges whose varying axis swaps or reverses. It
generates that tile from its own key and relief samples, then compares the
clipped wet/dry boundary intervals and exported float32 positions, rather
than copying the first tile's edge. A first version of this audit mapped
top/right/bottom/left as left/right/top/bottom; the interval assertion caught
that *probe* bug before any face-seam result was recorded. The corrected
`--all-faces` check in `check_coast_parent_child_spike.py` samples one
midpoint tile on each of all **24 oriented L4 face edges**, seed 42: 6,146
boundary intervals, two sampled shoreline crossings, zero interval
mismatches and zero reported float32 endpoint gaps. The selected seed-42
face-4 and face-5 corner-adjacent tiles independently check four more
oriented tile edges with eight shoreline crossings (five and three), also
with zero mismatches. Selected L6 boundary and corner cases have zero
mismatches but no sampled edge crossings. The sparse rule deliberately uses
full-fine boundaries *at* cube-face edges. The initial comparison is
same-level; the mixed-level follow-up below extends it. Neither is a sweep
of every coastal tile along those edges or a rendered neighbouring-tile crack
or three-face corner incidence test.

**Mixed-level cube-face follow-up (2026-11-02).** For each sampled face edge,
the exporter also builds the two *finer* tiles on the opposite face from
their own keys and relief samples (not from the same-level neighbour's edge).
It compares their combined clipped wet/dry intervals and exported float32
endpoints with the coarse tile after mapping swapped or reversed face axes.
Boundary coverage is checked on each finer tile. The seed-42 L4 midpoint
`--all-faces` sweep compares 6,146 coarse intervals against the finer pair
over 24 oriented face edges: zero interval mismatches and zero reported
endpoint gaps, but only two sampled shoreline crossings. The two selected
corner-adjacent face-4/face-5 patches add eight crossings, also with zero
mismatches; selected L6 boundary/corner patches likewise agree but have no
sampled crossings. This is a **sampled geometry audit**, not a rendered seam
or three-face junction check. It does not establish broad coastal coverage,
normal/skirts continuity, planet-to-tile zoom, canonical-contour fidelity,
both styles, or the L6 cache and frame-cost gates. Production terrain is
unchanged and R1-05 is not complete.

**Broader face-edge sampling (2026-11-02).** The reproducible
`--coastal-faces` probe checks five tile positions per oriented L4 face edge
for seed 42 (120 cases). Corner tiles check two edges each, yielding 168
oriented comparisons and 166 sampled shoreline crossings; **20 of 24** edge
sweeps encounter at least one crossing. Both same-level and mixed-level
independently generated neighbour audits report zero clipped-interval and
float-endpoint mismatches, with exact-once tile-boundary coverage. The counts
include repeated corner/reciprocal edges, so 166 is not a count of unique
coastline intersections. Four edge sweeps have no crossing in this sample.
This increases coastal seam coverage without proving all seeds, all coastal
tiles, three-face vertex incidence, or any rendered or screen-space gate.

**Rendered opposite-face pair probe (2026-11-02).** The probe-only exporter
now writes a complete raised parent plus an independently generated
same-level opposite-face tile, and a second mesh with the same parent plus
all four independently generated finer opposite-face tiles. The existing Godot
offscreen mesh renderer (`--coast-face-pair`) draws both at the identical
seam-centred camera pose, unshaded and double-sided;
`measure_coast_render_mesh.py --face-pair` reports visible-mask XOR and
water-mask changes over the common
interior. On OpenGL 3.3 / RTX 2080 Ti, seed 42, L4:

| Parent / face edge | Camera height (radii) | Common interior px | Water-mask differences | Visible-mask XOR |
|---|---:|---:|---:|---:|
| 2/4/7/0, top (six edge crossings) | 0.08 / 0.04 / 0.02 | 195,963 / 258,064 / 258,064 | 0 / 0 / 0 | 0 / 0 / 0 |
| 4/4/0/0, left (corner-adjacent) | 0.08 / 0.04 / 0.02 | 208,431 / 258,064 / 258,064 | 0 / 0 / 0 | 0 / 0 / 0 |

An initial export included only the two finer tiles touching the edge and
produced 11,261 far-pose corner silhouette-difference pixels. That was a
**probe footprint limitation**, not evidence of a crack; the full four-tile
rerender above removes it. The zero common-interior water changes are local
same-lattice handoff evidence, not a subpixel seam coverage test. No
three-face vertex or skirts/normals, reference contour,
continuous zoom, production styles or global cost has been rendered here.
The one-pixel shoreline gate remains open.

## Acceptance after the decision

- A scripted zoom holds the same coastal focus, freezes simulation and
  compares georeferenced water/land classification across the planet–tile
  transition and every tile handoff;
  the same feature does not jump as it crosses an LOD edge. Report maximum
  shoreline displacement in metres and screen pixels at orbit, region and
  local altitude, with a numeric gate selected by the ADR.
- No cracks, holes, wet land or dry ocean at mixed-level tile seams; V4
  hypsometry, V9 flight, V10 tile determinism and V13 precision still pass.
- The existing semantic water extent and the authoritative cell fractions
  remain unchanged, as do all simulation state hashes. Both styles agree on
  the coast, and the L6 60 fps/tile-memory gate remains in force.
