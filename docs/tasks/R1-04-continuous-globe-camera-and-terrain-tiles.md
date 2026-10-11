# Task R1-04 — Continuous globe camera and streamed terrain tiles

- **Track:** rendering (specification §31.10), in parallel with P0 / M7
- **Status:** proposed (2026-10-11); may start when ADR-0022 is accepted
- **Scope:** replace the client's single fixed planet mesh and orbit camera
  with an Earth-viewer camera and a quadtree of terrain tiles, on today's
  terrain, so the player can zoom continuously from orbit to about a hundred
  metres above the ground.
- **Governing decisions:** ADR-0022 §4.1, §4.8, V4, V9–V12; ADR-0018
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

## 2. What to build

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
   Each finer level adds procedural relief inside cells from the
   presentation RNG keyed by `(world_seed, tile_id)`, constrained so each
   cell's area–elevation distribution still matches its nine hypsometry
   quantiles. Neighbouring tiles of different levels are stitched (skirts or
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
