# Task R1-04 — Continuous globe camera and streamed terrain tiles

- **Track:** rendering (specification §31.10), in parallel with P0 / M7
- **Status:** ready (ADR-0022 accepted 2026-10-13)
- **Scope:** replace the client's single fixed planet mesh and orbit camera
  with an Earth-viewer camera and a quadtree of terrain tiles, on today's
  terrain, so the player can zoom continuously from orbit to about a hundred
  metres above the ground.
- **Governing decisions:** ADR-0022 §4.1, §4.8, V4, V9–V13; ADR-0018
  (presentation boundary, presentation RNG); ADR-0005 (sub-cell
  hypsometry); specification §31.5, §31.9
- **Must not touch:** solvers, field meanings, PSNAP, the simulation tick;
  the existing styles' signals (the R1–R4 readability gates must still pass)

## 1. Starting point (audit at `b465ba5`)

`PlanetMeshNode::rebuild(subdivision, radius_m, …)` builds one `ArrayMesh`
for the whole planet at a fixed subdivision; `scripts/main.gd` orbits a
camera at 1.3–10 planet radii with mouse-wheel zoom. Channels are uploaded
as float textures indexed by cell. There is no level of detail, no tilt and
no relief below cell resolution.

Vertices are built in `double` and handed to Godot as 32-bit `Vector3` in
units of the planet radius (`build_geometry`, radius 1.0). A 32-bit float
near 1.0 resolves about 1.2 × 10⁻⁷, which is about 0.8 m on an Earth-sized
planet. That is invisible from orbit and visible as vertex jitter, z-fighting
and shaking camera motion at 100 m. Changing units does not help: the
precision is relative to the distance from the origin.

## 2. What to build

0. **Precision spike, first and before any tile code.** A throwaway scene
   in `godot/tests/precision_spike/` with a flat-shaded procedural patch of
   1 m relief placed on an Earth-radius sphere, and a camera flying from
   orbit to 100 m and holding there while turning. Measure it twice:
   - **(a) camera-relative rendering** in a stock Godot build: the patch's
     vertices are stored relative to a tile origin in `double`, and each
     frame the client sets the tile's transform to `origin − camera_position`
     computed in `double`, so the GPU only sees small numbers. The camera
     stays at the scene origin and the world moves around it;
   - **(b) large-world coordinates**: the same scene in a Godot editor and
     export templates built with `precision=double`, and the extension built
     against a matching godot-cpp, with no camera-relative trick.

   For each, capture frames with the R4-06 path and report: maximum vertex
   screen jitter in pixels while the camera holds still at 100 m (from
   frame differences), visible z-fighting, the shadow and depth artefacts at
   1080p, the frame time, and for (b) the build time and any incompatibility
   with the current extension or styles. Also check the depth buffer:
   Godot's reverse-Z depth should be enough from 100 m to the horizon; say so
   explicitly or note the near/far planes needed.

   **Decision rule.** Pass is less than 0.5 px of jitter at 100 m, no
   z-fighting, and a frame time inside the §31.9 budget. Choose (a) if it
   passes: it keeps stock Godot and the existing CI. Choose (b) if only (b)
   passes. If neither passes, stop the task and report: that is the trigger
   to reconsider the engine for the client (PlanetSim is unaffected, since it
   has no Godot dependency). Record the choice as an amendment to ADR-0022
   §4.8 before writing the tile system, and build steps 1–6 on it.
1. **Camera** (`scripts/globe_camera.gd`): drag to turn the globe; zoom
   towards the point under the cursor; tilt that follows altitude, from
   straight down in orbit to near-horizontal close to the ground, with a
   player override; ground collision; a `fly_to(cell, altitude)` call for
   event staging. Smooth, frame-rate independent motion.
2. **Altitude bands**: planet, region, local, from ADR-0022 §4.8, exposed as
   a read-only `current_band` with a blend factor across each edge, so later
   tasks (placement tools, staging) can query it.
3. **Terrain tile quadtree** (C++ in `godot/gdextension/`): a cube-sphere
   (or icosahedral) quadtree whose root level reproduces today's cell mesh.
   Each finer level samples procedural relief inside cells by hypsometric
   rank (ADR-0022 §4.9): a fixed noise field keyed by
   `(world_seed, detail_kind = terrain)` and sampled by position, mapped
   through each cell's inverse hypsometry so its area–elevation
   distribution still matches its nine quantiles. No tile or cell seeds
   anything. The quantiles reach the client as `TerrainSnapshot::hypsometry_m`
   (nine floats per cell, ADR-0022 §4.6), a presentation copy of slow-state
   data; PSNAP does not change. Neighbouring tiles of different levels are stitched (skirts or
   edge morphing) so there are no cracks.
4. **Selection and streaming**: tiles chosen by screen-space error;
   generated on worker threads; cached with a bounded memory budget; a
   missing tile is drawn from its parent until ready.
5. **Channels on tiles**: every tile samples the same per-cell channel
   textures as today, so styles and overlays work unchanged at every
   altitude. Styles may add per-band detail later; this task adds none.
6. **Cell picking** keeps working at every altitude (right-click selects the
   cell under the cursor, as today).

The hypsometry quantiles are already in the slow state (ADR-0005); expose
them to presentation as ADR-0022 §4.6 specifies, without changing PSNAP.

## 3. Acceptance and report

- **Precision (step 0):** the spike report and the chosen approach are in
  the task's report and in ADR-0022 §4.8 before any tile code is merged; the
  final tile system meets the same pass line (under 0.5 px of jitter at
  100 m, no z-fighting) in the V9 flight.
- **V4 (region and local tiles):** as ADR-0022 V4, amended 2026-10-15
  (the terrain hands over to the corners' curves at cell edges, so V4
  bounds that mixture rather than holding every quantile to 2 %).
- **V9:** a scripted flight from orbit to 100 m and back, captured with the
  R4-06 capture path, shows no crack and no hole (a test samples the frames
  for background pixels inside the globe's silhouette).
- **V10:** the same tile reached by two different flight paths gives a
  byte-identical tile mesh.
- **V11:** 60 frames per second at L6 in the `stylised` style at planet,
  region and local altitude on the reference machine of §31.9, with tile
  generation running; report frame-time percentiles.
- Rendering leaves every state hash unchanged (ADR-0018 V6).
- The existing readability gates (R1-03, R4-03) still pass at planet
  altitude.
- Report: tile counts and memory at each altitude, generation time per
  tile, the relief tolerance chosen for the region band (ADR-0022 open
  question 5), and any visible artefact left.

## 4. Out of scope

Placement and zoning tools, sites, the local band's fields and streets
(R8), fog of knowledge (ADR-0019), history scrubbing (a later task on
ADR-0003 history), and any realistic style (specification §31.12).

## 5. Report

### Step 0: precision spike (2026-10-14)

**Choice: (a) camera-relative rendering in stock Godot**, recorded in
ADR-0022 §4.8. Steps 1–6 build on it.

The spike is `godot/tests/precision_spike/` (throwaway, not in CI):
`spike.gd` runs a flat-shaded 600 m patch of about 1 m relief on an
Earth-radius sphere at a generic place (every world coordinate about
4 × 10⁶ m), with 2 m markers and a strip 0.1 m above the ground, a sun
casting shadows, and a camera flying from 20 000 km to 100 m, then holding
still, turning (0.25° per frame) and creeping (1 cm per frame) at 1080p.
`--mode=absolute` uses world coordinates as they are; `--mode=relative`
keeps the camera at the origin and places the patch at origin − camera,
computed in GDScript's 64-bit floats. Each frame, every marker 10–150 m
away is projected in 64-bit arithmetic and compared with its centroid in
the captured frame (the R4-06 path, which shows what the screen shows);
jitter is that offset minus its mean over the neighbouring ±4 frames.
`depth_probe.gd` checks depth separation to the horizon. (b) used a Godot
4.2.2 editor built with `precision=double` (`scons platform=linuxbsd
target=editor precision=double`, 8 min 17 s on 14 threads) and godot-cpp
with `GODOT_PRECISION=double` (2 min 34 s).

Jitter at 100 m, RMS / maximum in pixels, NVIDIA RTX 2080 Ti:

| Build, renderer | Mode | Turning | Creeping | Still |
|---|---|---|---|---|
| stock 4.2.2, Compatibility | absolute | 2.26 / 6.24 | 0.44 / 2.43 | 0 / 0 |
| stock 4.2.2, Compatibility | **relative (a)** | **0.11 / 0.64** | **0.07 / 0.27** | 0 / 0 |
| double 4.2.2, Compatibility | absolute (b) | 2.07 / 5.44 | 0.35 / 1.89 | 0 / 0 |
| double 4.2.2, Compatibility | relative | 0.11 / 0.64 | 0.07 / 0.27 | 0 / 0 |
| stock 4.2.2, Forward+ | absolute | 2.12 / 4.72 | 0.45 / 1.99 | 0 / 0 |
| stock 4.2.2, Forward+ | **relative (a)** | **0.09 / 0.40** | **0.09 / 0.26** | 0 / 0 |
| double 4.2.2, Forward+ | absolute (b) | 0.09 / 0.40 | 0.09 / 0.26 | 0 / 0 |
| double 4.2.2, Forward+ | relative | 0.09 / 0.40 | 0.09 / 0.26 | 0 / 0 |

- **The floor.** Every configuration with no precision loss gives the same
  figures per renderer (0.11 / 0.64 in Compatibility, 0.09 / 0.40 in
  Forward+): that is the measurement's own noise, the rasterisation of a
  rotating 40–200-pixel marker, not jitter. Camera-relative sits on it in
  both renderers and both builds, so its precision jitter is below what the
  metric resolves; at a few hundred metres from the camera a 32-bit offset
  is good to about 3 × 10⁻⁵ m, or 2 × 10⁻⁴ px at 150 m. A static camera shows
  no frame-to-frame change in any mode. Against the pass line of 0.5 px,
  (a) passes; the Compatibility maximum of 0.64 px is the floor.
- **(b) fails in Compatibility,** the renderer the project and CI use:
  double precision on the CPU does not reach its GPU transforms. It reaches
  the floor only under Forward+. The extension, rebuilt against godot-cpp
  in double precision, crashes the double engine on its first engine call
  that passes a `Transform3D` (stack smashing in
  `PlanetMeshNode::update_sun_light`); the styles and the readability gate
  themselves ran. That was not investigated further.
- **Z-fighting: none.** About 7 % of the 0.1 m strip is hidden in every
  configuration, including double precision; raised to 1.5 m none is, so
  it is relief in front of the strip, not depth precision. The depth probe
  (two planes 0.1 m apart, upper drawn first so that a tie is lost) from
  100 m, near plane 0.5 m, far 43.8 km:

  | Build, renderer | < 3 km | 3–10 km | 10–30 km |
  |---|---|---|---|
  | 4.2.2 Compatibility | 1.000 | 1.000 | 0.469 |
  | 4.2.2 Forward+ | 1.000 | 0.942 | 0.684 |
  | 4.7.2 Compatibility | 1.000 | 1.000 | 0.735 |
  | 4.7.2 Forward+ | 1.000 | 1.000 | 1.000 |

  Godot 4.2.2 has no reverse-Z; 4.7.2's Forward+ separates to the horizon.
  With the near plane at 0.5 % of the altitude, Compatibility separates
  0.1 m to 10 km, and beyond 10 km 0.1 m is under a hundredth of a pixel;
  no further near/far splitting is needed for tiles.
- **Shadows:** no acne, swimming or flicker seen at 1080p in either mode;
  the 1 m relief casts little shadow at the spike's sun.
- **Frame time** (the spike scene, light): GPU p50 0.9–1.9 ms, p95
  2.7–4.0 ms; CPU p95 0.2–0.3 ms; the camera-relative update costs nothing
  measurable. Inside the §31.9 budget; the real budget test is V11.
- **Notes for steps 1–6.** Wind fan and tile triangles clockwise seen from
  outside (Godot's front faces): wound the other way they are lit from
  behind, as the planet mesh was until 6fae9af and as the spike's first
  terrain was. The double-precision engine build lives outside the
  repository (`~/src/godot-double`) and is not needed further.

