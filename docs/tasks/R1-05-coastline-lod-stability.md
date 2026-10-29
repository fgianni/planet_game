# Proposed R1-05 — Coastline-stable terrain LOD

- **Track:** rendering, following R1-04 (ADR-0022 §4.8–4.9)
- **Status:** proposed, not implemented (2026-10-24)
- **Decision gate:** proposed ADR-0030 selects a shared coastline, pending a
  measured feasibility spike and acceptance before changing ADR-0022's terrain
  LOD contract.
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

## Decision needed

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

Proposed ADR-0030 chooses route 1: the existing detailed field defines the
visible coast. A shared coastal constraint, georeferenced handoff measurement
and 64 MiB extra cache ceiling still need feasibility validation. ADR-0030 is
not yet accepted, and this task remains proposed.

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
