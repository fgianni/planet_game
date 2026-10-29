# ADR-0030 — A shared coastline for terrain LOD

- **Status:** proposed (2026-10-29); not accepted or implemented
- **Amends on acceptance:** ADR-0022 §4.8–4.9. Independently sampled coarse
  tiles currently disagree with detailed relief between shared vertices.
- **Scope:** presentation terrain geometry only. No solver, terrain-field
  meaning, sea-level semantics, `VisualFrame` channel, PSNAP or simulation
  state change.

## Context

R1-04 coarse tiles sample `TerrainRelief::smooth_sample()` (blended cell and
corner means), while detailed tiles sample `TerrainRelief::sample()` (fixed
spatial noise ranked against blended hypsometric curves).
`make_terrain_tile()` switches between them at `tile_filtered_cells` times
cell spacing. At L4, seed 20260928, the first detailed level is 2. The
R1-05 probe finds 100 land/water flips in 532 sampled coastal points
(18.8%), a mean elevation difference of 365 m and a maximum of 2467 m.
These are presentation elevations; simulated land fractions do not jump.

There is a **second, independent handoff** at the planet–region band edge:
both `godot/styles/*/surface.gdshader` files classify the cell mesh's water
from `surface_class` and `detail_seed_coast`, but classify tile water from
interpolated tile height (`detail_geometry_water`). The cell mesh's vertices
also choose above-sea height by cell land fraction. A tile-only contour
cannot make that transition stable, even if every tile edge is identical.

Moving the threshold, changing a colour ramp, or cross-fading unrelated
coastlines does not make the shore stable. A relief hierarchy with
geomorphing would remove the sudden pop but still move the coast during zoom.

## Proposed decision

1. The **canonical visible shoreline** is the sea-level contour of the
   existing detailed, position-keyed `TerrainRelief::sample()` field:
   `sample(direction).elevation_m == TerrainSnapshot::sea_level_m`.
   Its position depends on the world seed, terrain and sea level, never tile
   key, LOD, camera or style. A future authoritative sea-level change
   recomputes it from the new snapshot.
2. Every visible coastal LOD, including parents drawn while children load,
   uses a **shared coastal constraint**: compatible shore vertices/edges and
   classification from that contour. Away from a constrained coastal strip,
   coarse tiles may keep the filtered mean-height surface. The strip hands
   back continuously without introducing another sea-level crossing. It is
   presentation-derived, generated and cached lazily, not a new snapshot
   field or a simulation mask.
   The planet-band cell mesh must use a display-filtered representation of
   the *same* canonical coast across its cross-fade with root/coarse tiles.
   Its current independent `detail_seed_coast` water threshold may remain
   for unrelated fractional coverages but may not define a second coastline.
   Filtering may merge subpixel features at orbit, yet the transition must
   not produce a resolvable water/land jump, and cell-level water coverage
   must remain consistent with `surface_class` within its readability gate.
3. Anchor constraint samples and intersections in global cube-face
   coordinates, with deterministic tie-breaking at face edges and shared
   dyadic coordinates. Parent/child and neighbouring tiles derive the same
   boundary independently. Stitch mixed-level geometry and normals so
   skirts do not expose a contradictory coast. Both style packs use that
   geometry and the existing semantic channels.
4. Geometry and classification agree at sea level: no dry ocean or wet land
   because a tile uses a different relief. Altitude-dependent exaggeration
   still falls to 1× locally; zero-height shore vertices remain fixed under
   that scaling. Picking and collision use the drawn coastal representation.

This selects a shared coast over geomorphing the current mean-to-detail
handoff. It does not mandate a fixed-resolution global raster or require
high-frequency relief everywhere on a coarse tile. A binary shader mask
alone is insufficient if land and water geometry still disagree.

## Feasibility gate before acceptance

- First spike a bounded, crack-free representation (for example, a lazily
  generated contour with clipped coastal triangles). Extend
  `tools/render/coast_lod_probe.cpp` to sample georeferenced points on drawn
  parent/child surfaces, not just two relief functions. Cover several seeds,
  L4/L6, all cube faces including seams, varied coastal relief and the
  snapshot sea level. Report flip counts, maximum shoreline displacement in
  metres, and the contour approximation error.
- Script a frozen coastal focus crossing every tile handoff in both
  directions, including cell mesh to root/coarse tiles. Compare the drawn coast to the canonical contour in world
  metres and screen pixels at planet, region and local altitude. **Proposed
  screen-space gate (selected for further prototyping 2026-10-29):** after
  antialiasing, no shoreline handoff moves a resolvable boundary by over
  one pixel at the same camera pose; no island or inlet with a projected
  area of at least four pixels may appear/disappear solely with LOD. At
  fixed georeferenced test points farther than one projected pixel from
  the canonical contour, classification must agree. Count and report the
  remaining subpixel differences, not just a pass/fail. The world-metre
  tolerance is altitude- and camera-dependent, derived from projection;
  it must not be substituted with a universal vertical-height tolerance.
- Re-run ADR-0022 V4 (hypsometry), V9 (holes), V10 (path-independent tile
  bytes), V13 (precision), mixed-level seams, both styles' readability and
  render-does-not-change-state checks. Check every relief band and day/night.
- Measure tile generation, cache bytes, and p50/p95/p99 frame times during
  the L6 flight. Retain the 512-tile cache and p95 < 16.7 ms (60 fps) gate.
  Proposed extra memory ceiling: 64 MiB of cached coastal constraints at L6,
  beyond R1-04's measured ~243 MB tile vertex cache. If this cannot be met,
  return to the architecture choice; do not weaken the shoreline or V4 gate.

## Consequences and unresolved implementation detail

- Coastal generation/cache work increases; inland and deep-ocean tiles stay
  on the cheaper path. Subpixel shore detail may be filtered at orbit but
  must not shift the georeferenced boundary.
- The contour extraction method, strip width and tessellation error bound
  need the measured spike. This proposal does **not** authorize changing
  `tile_filtered_cells` or R1-04 tile code before acceptance.
- An initial 1D transect spike (R1-05, 2026-10-29) rules out a simple fixed
  coarse raster: at 64 samples over 0.6 cell, 8/78 reference crossings at
  L4 and 6/60 at L6 are missed. It is not a 2D seam or performance proof;
  adaptive contour refinement and bounded cache cost remain unverified.
- A subsequent 2D adaptive-sampling spike on one coastal tile at each level
  also does not meet the proposed no-flip gate: at a 256² grid and 50 m
  height-error trigger, independent checks find 114/65370 wrong-side points
  at L4 and 19/65485 at L6. Same-level edge samples agree exactly, but no
  clipped geometry or mixed-level seam was tested. Do not accept this ADR
  on the strength of those shared edge samples alone.
- Shader audit (2026-10-29) finds an independent cell-mesh water mask at the
  planet–region handoff. A viable spike must include the cell-mesh colour
  and geometry transition as well as tile–tile handoffs. The current 2D
  probe measures neither shader coverage nor that cross-fade.
- An offscreen L4 map-style probe at one coastal focus, forcing cell mesh
  then settled tiles at the same camera, changes a blue-colour water
  heuristic on 4852/123904 central pixels (3.9%). It bypasses the normal
  dither and is not a contour displacement metric, but confirms the second
  handoff is visible. The proposed common coastline must address both.
- A uniform clipped-triangle geometry spike generates parent and four
  children independently on a shared 128²/256² cube-face lattice. Their
  shoreline edge sets agree at two tested coastal patches, with sea-level
  vertices on the sphere to rounding, but independent midpoint checks still
  find 66/16356 wrong-side points at L4 and 65/16346 at L6 at 128².
  At 256² the candidate costs an estimated 1.30 MB / 1.67 MB of extra
  coastal vertices in those two patches, before cache/renderer overhead.
  It is not a viable accepted representation yet; neither the planet-band
  handoff nor the 64 MiB global cache budget is established.
- A screen-space estimate for that failed 256² candidate puts wrongly
  classified sample points p95 631 m (L4) and 176 m (L6) from the generated
  coast, approximately 14 px and 3.8 px at an illustrative 30 km nadir
  camera with 650 px focal length. This is *not* an exact-contour
  displacement or a rendered handoff test. It reinforces that simple
  uniform clipping is not ready to accept under the proposed 1 px gate.
- Sparse adaptive coastal leaves (corner/midpoint tests plus a 50 m
  interpolation-error trigger) reduce the clipped geometry but do not
  certify contour topology: at a 512² maximum grid, 171/261487 independent
  L4 points and 12/261930 L6 points still have wrong land/water class;
  coastal vertices alone cost about 3.84 MB and 1.05 MB in those patches.
  Hidden islands/inlets inside nominally uniform leaves remain possible.
  The chosen direction for further prototyping is the screen-space contract
  above, rather than an exact mathematical bound on unsampled crossings.
  The candidate still fails it where omitted features are resolvable.
- A regenerated fixed-camera map-style baseline (forced cell mesh versus
  settled tiles) has 4852/123904 changed water-colour pixels and 1247
  disagreements more than one pixel inside both masks. The largest connected
  change has 1538 pixels, but changed components are not necessarily islands.
  This is not a georeferenced contour displacement test or normal zoom; it
  establishes the planet–tile handoff baseline before a candidate is built.
- At fixed poses nearer the same focus (1.25 and 1.10 radii), the same
  forced endpoint heuristic changes 10183/123904 and 6745/123904 pixels,
  including 5530 and 5083 disagreements deeper than one pixel in both
  masks. Half-blended frames were captured but not assessed as a continuous
  zoom. These measurements do not establish contour displacement or a
  candidate that meets the screen-space gate.
- A probe-only shader sampling a georeferenced atlas of the detailed relief
  on both cell and tile geometry reduces endpoint water-colour changes at
  1.45/1.25/1.10 radii from 4852/10183/6745 to 39/108/8 pixels for 512²
  samples per cube face (0 deeper than one pixel in both masks). The atlas
  takes 4.72 MB uncompressed RGB and 8.6 s to generate in this test.
  Doubling the face sampling changes another 330/671/300 tile-render pixels,
  with 28 deep disagreements at the closest pose: the fixed raster is not
  a proven canonical contour. Independent within-texel checks find
  728/98259 and 1511/393073 wrong-side points at 512² and 1024².
  The probe replaces only water colouring,
  leaving geometric heights, occlusion, seams and picking inconsistent;
  it is not an acceptable implementation of the shared-coast decision.
- A mixed-level edge probe independently samples both sides: at tested L4
  and L6 face-0 seams, fine/coarse linear contours have 6/4 crossings even
  though shared vertex heights agree exactly. Refining the coarse boundary
  with 64 missing heights gives the same six crossing positions (0 m
  difference); one face-0/face-5 edge also matches. A standalone one-cell
  stitched ribbon clips wet/dry triangles and covers all 128 fine boundary
  intervals once. On those two edges its six clipped shoreline intersections
  and 3D edge positions agree, without inverted radial winding; extra
  triangle-list vertices cost roughly 15--16 KB per tested edge. It is not
  integrated with actual tiles or skirts; rendered cracks, normals, all-face
  coverage, planet-band geometry and global cache cost remain untested.
  Shared *boundary* samples alone cannot accept this ADR.
- An independent all-face audit over two seeds, L4/L6 and all 24 oriented
  cube-face edges finds no mismatched canonical boundary samples, crossings
  or three-face corners. In contrast, the probe-only fixed atlas's opposite
  nearest texels disagree in 228--394 / 12288--24576 oriented border
  comparisons for seed 20260928 (230--284 for seed 42), with runs up to
  seven texels. Border texels sample distinct nearby points, so this is
  not a rendered-pixel measurement or a discontinuity in the relief; it
  rules out assuming the unmodified face-local nearest-texel classifier
  supplies a seamless shared coast. A boundary/corner ownership rule and
  rendered validation remain required.
  Pinning atlas edge values to the canonical edge makes the two sides
  agree on the tested border sequence, but seed 42 at L6 has two conflicting
  within-face corner texels (each is shared by two pinned edges). This
  thought experiment does not render or resolve the corner owner or
  transition from edge to interior.
- Future water-extent semantics remain R5 work. This proposal changes only
  the presentation of today's static sea-level boundary.
