# Task R1-04 — Continuous globe camera and streamed terrain tiles

- **Track:** rendering (specification §31.10), in parallel with P0 / M7
- **Status:** proposed (2026-10-11); may start when ADR-0022 is accepted
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
   anything. Neighbouring tiles of different levels are stitched (skirts or
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
- **V4 (region and local tiles):** for sampled cells, each generated
  quantile is within 2 % of the cell's elevation range.
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
