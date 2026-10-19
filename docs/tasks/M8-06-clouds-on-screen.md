# Task M8-06 — Clouds on screen

- **Milestone:** P0 / M8 (sixth task)
- **Status:** complete (2026-10-19)
- **Governing decisions:** ADR-0023 §4.8, V12; ADR-0018 (channels, style
  packs, the readability gate); ADR-0022 §4.8–§4.9 (altitude bands,
  procedural detail keyed by position)

## Done

- **The column's cloud optical depth** as the step's radiation saw it,
  Σ f_k τ_k (`CloudOptics::column_optical_depth`), is kept in a new derived
  field `cloud_optical_depth` (0x0008'0006, cell, float32, "1"; never
  persisted, so PSNAP is unchanged). The presentation cannot rebuild it
  from the layers' fractions: τ needs r_e and the layers' mass.
- **Presentation snapshot schema 6** (`StateSnapshot`): `cloud_cover`, the
  column fraction 1 − Π (1 − f_k) of the layers' `cloud_fraction` (random
  overlap, as the radiation's), and `cloud_optical_depth`; both empty until
  a step with clouds has run. PFRAME02 frames of schema 6 carry them;
  schema 3–5 frames still read.
- **The channels** (`make_visual_frame`): `cloud_cover` = the column
  fraction, clamped to 0–1; `cloud_thickness` = 1 − exp(−τ / 10). Absent
  without clouds.
- **Tests:**
  - `test_visual_frame` (V12, headless): clear, half-covered and overcast
    cells give cover 0, 0.5 and 1 and thickness 0, 1 − e⁻¹ and more;
    a frame without clouds has neither channel; schema 6 frames round-trip
    their clouds, a schema 5 frame reads without them;
  - `test_cloud_radiation_run`: after three L3 months the snapshot's cover
    is the layers' random overlap in every cell, its area mean the step's
    global cover, and τ > 0 exactly where there is cover;
  - `test_climate_circulation`: the frames are schema 6.

- **The cloud layer** (`godot/gdextension/planet_mesh_node.{hpp,cpp}`):
  - a `CloudLayer` child, a second `MeshInstance3D` over the cell mesh's
    own `ArrayMesh`, so it adds no geometry. Its shader lifts every vertex
    to `cloud_radius` = 1 + e·3 km/R, with e the drawn relief
    exaggeration, so exaggerated mountains rise through the clouds as
    real ones do, and at ground level it sits 3 km overhead;
  - drawn alpha-blended, without depth writes and from both sides: over
    the terrain tiles, seen from below at ground level, and in every
    altitude band;
  - drawn only in the style view when the style declares a
    `cloud_shader` (an optional `PlanetStyle` property) and the frame has
    `cloud_cover`. `set_clouds_visible`, `is_cloud_layer_drawn`,
    `get_cloud_radius`, and `get_channel_values` for tests;
  - the `cloud_cover` and `cloud_thickness` textures are uploaded with
    the other channels and bound to the cloud material, with the daylight
    channel, the cell size and a new detail seed
    (`DetailKind::cloud`, ADR-0018 §4.8).
- **The cloud shaders**, one per style (`godot/styles/<style>/clouds.gdshader`):
  - both draw the cover as patches over exactly its area at region scale
    (`detail_cover`, keyed by position), and as a smooth fraction at
    planet scale;
  - **stylised** (revised 2026-10-19 after the user found the clouds
    thick, grey and uniform):
    - opacity is the cloud's reflectance as the radiation computes it,
      R = (1 − g) τ / (2 + (1 − g) τ), τ recovered from `cloud_thickness`:
      thin cloud is a veil, thick cloud nearly opaque;
    - from above, white, brightest where thick; from below, greyer where
      thick (the shell knows which side the camera is on);
    - soft-edged shapes about two cells across, with the fBm's finer
      octaves inside, switching on from 1.5 px per cell, so the field has
      structure from orbit;
  - **map:** hatching, as a map shades cloud: two-tone diagonal lines,
    white edged in dark slate, whose width carries the cover, over a
    faint veil stronger where the cloud is thick, at a fixed 6 px period
    so they never alias at orbit. A uniform veil could not show half
    cover and keep land and ocean apart under overcast in the same
    style; the lines do both;
  - both darken on the night side, with day/night shading on.
- **The app:** `C` shows or hides the clouds; the HUD shows their state.

## Results

- **`godot/tests/test_clouds.gd`** (in CI; Godot 4.7.2 and 4.2.2):
  - a frame with clouds draws the layer, in both styles;
  - hiding it, or a climate-lab view, removes it;
  - the shell is above the surface;
  - from orbit the clouds brighten the planet (mean luminance 0.074 →
    0.086);
  - from just above the ground under the cloudiest cell, looking up, they
    fill the sky (0.021 → 0.422).
- **V12** (`render_readability.gd`, `check_readability.py`; the same on
  4.7.2 and 4.2.2): the surface drawn on a unit sphere with the style's
  cloud shell over it, the view inside the disc. Mean CIEDE2000 at the
  worst of normal vision, the three dichromacies and luminance alone,
  gate 10:

  | pair | stylised | map |
  |---|---|---|
  | clear vs overcast | 32.7 | 16.0 |
  | clear vs half cloud | 10.5 | 10.0 |
  | land vs ocean under overcast | | 11.2 |

  The surface pairs are unchanged. Half cloud passes with thin margins,
  the map's bound by luminance alone.
- The live view at L4 shows soft cloud over the oceans and thin cloud
  over land (`--live=true --water=true`).
- All 97 headless tests pass, and every Godot test in CI.

## Open

- Half cover's readability margins are thin (10.0 map, 10.5 stylised):
  any change to either cloud shader should re-run V12.
- The cloud shell is a single height: no cloud-top structure by layer
  and no shadows on the ground.
- **The simulated cloud field is unlike Earth's** (L4, one year after a
  5-year spin-up; zonal annual cover against Earth's, roughly):
  - 0.71–0.83 from 20° S to 20° N (Earth ~0.7 at the ITCZ), with no
    subtropical minimum: 0.70–0.78 at 20–35° (Earth 0.4–0.5);
  - 0.36–0.41 in the storm tracks at 50–60° (Earth ~0.8, its cloudiest)
    and 0.07–0.16 at the poles (Earth 0.5–0.7);
  - almost all in the bottom layer (the layers above ~0.01): no high
    cloud;
  - mean optical depth about 3 (Earth's clouds about 5–15), so the drawn
    clouds are thin.
  That is the physics (ADR-0023's cloud fraction from the cloud water and
  M8-05's calibration to the global targets), not the rendering.
