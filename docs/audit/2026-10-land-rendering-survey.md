# planet\_game — land rendering survey

Oct 10, 2026 · @frederick gianni

## Summary

The land is not short of detail so much as scrambled: the surface shaders read cell data at interpolated texel addresses, so every triangle samples unrelated cells and the planet renders as dark speckle and concentric hex rings. Fixing that one line of UV setup brings the continents back; a shader-only detail pass on the existing channels then takes it most of the way to a finished look.

Surveyed at `b465ba5` (9 Oct 2026), built against the pinned Godot 4.2.2 / godot-cpp and rendered offscreen at L6, seed 20260928, `earth_like`, day/night off.

> **Status (13 Oct 2026).** Point-in-time survey, not implementation guidance; the specification and accepted ADRs govern. Step 1 landed: the mesh change in `0f7fa97`, with the `surface_detail.gdshader` prototype (step 3, not yet selected by any material), and the non-constant readability case that closes it. Step 2 landed: the bridge loads every `godot/styles/<name>/style.tres` it finds, refusing an invalid pack by name, and `reload_styles()` picks up a new directory without C++. Step 3 landed: both styles draw sub-cell detail from `godot/shaders/surface_detail.gdshaderinc`, which replaces the prototype. Every detail is a coverage (equalised noise thresholded against the channel's interpolated fraction), off at planet scale and on at region scale, seeded per world from a reserved presentation RNG stream; `test_detail_coverage.gd` is the §31.5 test. Of the prototype's effects, slope rock and the beach band were dropped because they change class proportions the channels set; beaches now come from the sand weight itself. Step 4 landed: the root cause of the dark scene was the mesh, whose fan triangles were wound anticlockwise; Godot's front faces are clockwise, so every fragment shaded as a back face with its normal flipped inward and no light reached it, only ambient. With the winding fixed, the planet is lit by its own sun (the frame's sun direction, a light owned by the bridge) with the camera light as a fill, `stylised` leaves the day-side gradient to that light, and the scene opens on the day side; `test_scene_lighting.gd` checks land and ocean lightness and contrast at planet and region scale in both styles. Separately, Godot's Compatibility renderer returns offscreen captures with values below about 62/255 compressed (7/255 and below as 0) on both NVIDIA and llvmpipe, which also bears on the V8 readability measurements of dark colours. The placement decision below is now carried by the proposed ADR-0022. The survey's screenshots were not exported; the image references are kept as captions only.

Three moves, in order:

1. **Fix texel addressing** (bug, \~40 lines of C++): give each fan triangle constant texel addresses plus barycentric weights, so styles can choose flat cells or exact smooth corners.
2. **Sub-cell procedural detail in the style shaders** (no new channels): fractal coasts on the water-weight isoline, beaches, slope rock, patchy class mixing, noisy snow lines, micro bump. Prototyped and rendered below.
3. **Forests and other assets through a channel, not the shader**: the specification forbids faking an absent channel, so trees need `vegetation_vigour` (R5). M7's bucket and precipitation make an earlier, honest stand-in possible if an ADR amendment allows a climate-derived potential-vegetation channel.

## How land is rendered today

Every visible land property is one number per Voronoi cell, coloured by a handful of flat constants; nothing in the pipeline adds structure below a cell (\~110 km at L6).

&#91;embedded content: land rendering path · 3 stages\]

- **Geometry** (`planet_mesh_node.cpp` `build_geometry`): each hex/pent cell is a fan from its centre to its corners, displaced by mean elevation × 25 over land, smooth area-weighted normals. Corners average their three cells.
- **Channels** (`visual_frame.cpp`): `surface_class` = water, rock, sand, soil, ice weights. Land class comes from elevation alone: sand below 200 m, rock above 1,500 m, soil between (`visual_frame.hpp:16-17`). No temperature, rain or latitude enters it.
- **Shaders** (`styles/stylised|map/surface.gdshader`): weights × five constant colours, a highland tint, snow/sea-ice lerps, day/night, a rim tint. `filter_nearest`, no noise, no slope term.
- **Rules that bound any fix** (ADR-0018, spec §31): styles see semantic channels only; an absent channel is shown neutrally, never faked; procedural detail must follow its channel and use a presentation RNG keyed by `(world_seed, cell_id, detail_kind)`; changes must not touch solvers, fields or PSNAP.

## Finding 1 — the texel interpolation bug

Each fan triangle gives its centre vertex the cell's texel address and its two outer vertices corner texel addresses (`planet_mesh_node.cpp:883-889`). Those addresses are rows apart in the 2048-wide channel texture, and the shaders read `texelFetch(map, ivec2(channel_texel + 0.5))` on the interpolated UV. So a fragment halfway across a triangle fetches whichever unrelated cell sits halfway between the two addresses in memory.

Result at L6: the planet reads as dark noise from orbit and as concentric hexagon rings up close. Same camera, same build, before and after making all three texels the cell's own:

| Today (stylised, L6) | Cell texel on all three vertices |
| --- | --- |
| &#91;image: Today, planet view\] | &#91;image: Fixed, planet view\] |

&#91;image: Today, close view: concentric hexagon rings\]

- **Scope:** both style shaders and `shaders/climate_lab_overlay.gdshader` use the same pattern, so overlays are scrambled too.
- **Why CI passed:** `tests/render_readability.gd` drives the shader with constant textures, where every texel holds the same value, so wrong addresses still read right. The live L4 capture is saved, not compared.
- **Fix (prototyped, `exact-interpolation.patch`):** every vertex of a fan triangle carries the same three addresses — cell in `UV`, first corner in `UV2`, second corner plus two barycentric weights in `CUSTOM0` (RGBA float). Interpolation leaves the addresses constant; a style then picks flat cells (`UV` only, existing shaders unchanged) or exact smooth blending (all three).
- **Test to add:** a readability case with a non-constant channel (a checkerboard of cells), asserting each rendered cell centre matches its own value.

One caveat: I rendered with Mesa llvmpipe, as CI does. The arithmetic is the same on any GPU, but if your desktop build looks clean, tell me and I'll look for what differs. *(13 Oct: confirmed on an NVIDIA GeForce RTX 2080 Ti under Godot 4.7.2: the same moiré before the fix, clean cells after.)*

## Other findings

With the bug fixed, the land is legible but reads as a hex board: one flat colour per cell, classed by height alone.

&#91;image: Fixed but flat: hex cells, height-only sand blobs, unshaded rock plateau\]

| Finding | Evidence | What you see |
| --- | --- | --- |
| No detail below a cell | shaders use only per-cell channels; no position-keyed noise | hexagons at any zoom past planet view |
| Land class from height only | `visual_frame.cpp:126-131`: sand < 200 m, rock ≥ 1,500 m | orange "sand" scattered over green lowlands; no deserts or forests where climate says so |
| No slope or lapse rate | class ignores slope; snow only from the snow channel | grey rock plateau with no ridges; no snow on peaks in previews (snow channel absent without a run) |
| Coasts follow cell edges | water weight blended linearly per cell | stair-stepped coastlines; no beaches or shelves |
| Relief barely shades | relief × 25 on a 110 km mesh, one light parented to the camera | mountains read by colour, not by light; scene is dark overall |
| Styles hard-coded | `load_styles()` lists two `style.tres` paths | a new style pack (spec §31.3 says "a new style is a directory") needs a C++ change |
| Manifest has no `assets` | `PlanetStyle` carries shader + channels only | nowhere yet for the "detail meshes and textures by camera scale" of §31.3 |

The earlier prototype (`prototype/climate_planet_0.5/shaders/terrain.gdshader`) already did fbm-broken class borders, a lapse-rate snow line and rain-driven deserts; much of that look can come back inside the channel rules.

## Prototype — sub-cell detail from existing channels

A shader-only pass on the fixed mesh removes the hexagons and adds coastlines, beaches and texture without a new channel or any solver change (`surface_detail.gdshader`, 164 lines, dropped in place of `stylised/surface.gdshader`).

| Fixed mesh, original shader | Detail prototype |
| --- | --- |
| &#91;image: Fixed, close view\] | &#91;image: Prototype, close view\] |

&#91;image: Prototype, planet view\]

&#91;image: Prototype, highland view\]

What it does, each tied to a channel:

- **Exact smoothing:** blends the cell and its two corners with the barycentric weights from the fix.
- **Coastline:** the 0.5 isoline of the interpolated water weight, roughened by fbm at \~1 and \~5 cells; light shelf water just offshore, deep water by `relief`.
- **Beaches:** a band on the land side of the coast, low ground only.
- **Patchy class mixing:** each land class weight is scaled by its own noise and sharpened, so a 30/70 rock/soil cell shows rock patches rather than a brownish average.
- **Slope rock and noisy snow:** rock where the geometric slope is steep; snow placed by noise and slope but sized by `snow_cover`.
- **Sea-ice floes, micro bump:** floes thresholded from `sea_ice`; a screen-space bump from fine noise on land.

What it still needs before it ships:

- Key the noise by `world_seed` (a uniform offset) to meet §31.5's presentation-RNG rule; today it hashes planet-space position only.
- The §31.5 test that detail never contradicts its channel: sharpening changes local proportions, so check that mean class coverage per cell stays within tolerance.
- Tune: the fine grain aliases at planet scale (fade octaves with distance), and the dark scene needs a lighting/exposure pass.
- The same pass in the `map` style, then the readability harness on both.

## Forests and other assets

Forests are the one improvement the rules don't let a shader invent: §31.2 says an absent channel is never faked, and §31.5 says tree density follows `vegetation_vigour`, which the track schedules for R5 after M9–M10. There are two honest roads.

| Road | Source of "forest here" | Needs | When |
| --- | --- | --- | --- |
| A. Wait for vegetation | `vegetation_vigour` from the M10 vegetation model | M9, M10 | as planned (R5) |
| B. Potential vegetation now | a new `potential_biome` weights channel from climatology: annual mean temperature, precipitation and bucket water (M7) through a Whittaker-style table | ADR-0018 amendment; precipitation and bucket added to the presentation snapshot and `PresentationReference` (not PSNAP); headless channel tests | after M7-07 closes |

Road B shows where the climate would support forest, not where forest is. It would be labelled as such and retired, or kept as a ceiling, once `vegetation_vigour` lands. It also replaces the height-only sand rule with real deserts.

How the assets would be drawn, whichever road feeds them:

- **Planet scale** (§31.5: channels only): forest as canopy colour and texture in the surface shader, weighted by the channel.
- **Region and local scale:** one `MultiMeshInstance3D` per asset kind per chunk of cells, built when the camera comes near and faded by distance. Instance count per cell = channel value × cell area × a style density; positions from the presentation RNG keyed by `(world_seed, cell_id, detail_kind)`; rejected where the shader's coast field says water or slope says cliff, using the same noise function on the CPU.
- **Style manifest:** add the `assets` entry §31.3 already names (detail meshes by camera scale) to `PlanetStyle`, so `stylised` ships low-poly trees and `map` ships symbols.
- **Other assets on today's channels:** boulders and scree on the rock class, icebergs along the `sea_ice` edge. Buildings wait for `urban` (R8).
- **Gate:** the §31.5 test that sampled instance density per cell stays within tolerance of its channel, plus a readability signal for forest vs bare land.

## Decision — what the player touches at each scale

Decided 10 Oct 2026, and now carried by the proposed [ADR-0022](../decisions/0022-player-placement-zoning-and-continuous-zoom.md), which supersedes this section where they differ: the player places **major investments and paints zoning**. Fields, farms and city growth are not placed by hand; they emerge from the economy under those rules, in line with design v1.4 §46.2 (no tile-stacking) and §46.8 (Investment and Regulation levers).

| Scale | Shows | Player can | Detail from |
| --- | --- | --- | --- |
| Planet | ice caps, desert belts, cloud bands, spread of cities, night lights | read trends, open overlays | channels only, as colour and texture |
| Region (a basin or coast, a few dozen cells) | towns, fields, plants, dikes, a river in flood, a drying valley | place investments (plants, industry, dikes, rail, new city or expansion zone) at a cell or a site within it; paint zoning (farm here, protect forest, managed retreat); see lever previews | channels + instanced assets seeded by `(world_seed, cell_id, detail_kind)` |
| Local (one city or valley) | flooded streets, failed crops, burnt forest, smog | watch only: opened by event staging (§42.8) or by the player on any city | a procedural patch generated from that cell's state |

Rules for the local view:

- **Floods from the cell's hypsometry.** The patch's terrain is generated to match the cell's nine elevation quantiles (ADR-0005); the simulated inundated fraction and any dike's protected elevation decide which streets flood and whether the wall holds.
- **Crops and forest from channels.** Field count follows `farmland`; the share browned, stunted or failed follows `dryness` and `vegetation_vigour`; burn scars follow `fire`.
- **Same seed, same place.** A city revisited decades later keeps its layout, so before-and-after views come for free.
- **No levers at local scale.** It is a consequence view, never a placement view.

First local scene to build: a coastal city flooding against its hypsometry, because it exercises terrain, inundation, dikes and staging at once. Drought on crops follows M10.

## Proposed plan

The first four steps fit the current R-track rules with no ADR change and no solver contact; step 5 needs an amendment, step 6 waits on M10.

| # | Step | Touches | Done when |
| --- | --- | --- | --- |
| 1 | Fix texel addressing (UV / UV2 / CUSTOM0) | `planet_mesh_node.cpp/.hpp`, three shaders | non-constant readability case passes; all three shaders show cells as their own values |
| 2 | Load style packs by directory scan | `load_styles()` | a new `godot/styles/<name>/` appears without C++ |
| 3 | Detail pass in `stylised` and `map` | style shaders, a `world_seed` uniform | readability passes in both styles; §31.5 per-cell coverage test passes |
| 4 | Lighting and exposure pass | `main.tscn` environment, light | land and ocean contrast holds at planet and region scale |
| 5 | `potential_biome` channel (road B) | ADR-0018 amendment, `sim/presentation`, presentation snapshot | headless direction tests (wet/warm → forest, dry → desert) |
| 6 | Tree and boulder instancing, `assets` in the manifest | bridge, `PlanetStyle`, both styles | density-vs-channel test; frame budget of §31.9 at L6 |

Open question: should step 5 be road B now, or do you prefer to hold forests for M10 and keep the planet strictly to authoritative fields?

Files from this survey: `exact-interpolation.patch` (step 1) landed in `0f7fa97`, with `godot/shaders/surface_detail.gdshader` (step 3 prototype).
