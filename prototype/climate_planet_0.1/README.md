# Climate Planet – look-dev prototype (Godot 4.7)

A visual prototype of the planet view. One **Warming** slider drives the whole look from 2025 (+1.2 °C) to 2140 (+3.4 °C):
polar caps and sea ice shrink, snow leaves the peaks, the sea rises and drowns the coastal lowlands (pale teal),
forests thin out and turn brown, deserts spread, clouds and atmosphere change colour, hurricanes and wildfires appear.

There is no game logic here yet: `ClimateLook` is the single place where the future simulation will plug in.

## Run it
1. Open Godot 4.7 → **Import** → select this folder's `project.godot`.
2. Press **F5** (Run Project).

Controls: drag with the left mouse button to orbit, mouse wheel to zoom. **Play** runs 2025 → 2140 in 30 seconds.
**Hurricane** and **Wildfire** trigger a one-off event; **Spin on/off** stops the planet's rotation.

## Files
| File | What it does |
|---|---|
| `scripts/climate_look.gd` | Three keyframes (2025, 2085, 2140) and interpolation. **Tweak the look here first.** |
| `scripts/planet.gd` | Builds the planet in code: icosphere terrain from noise, ocean, clouds, atmosphere, trees, cities, factories, storms, fires. |
| `scripts/main.gd` | Space background, light, orbit camera, on-screen controls, command-line screenshots. |
| `shaders/terrain.gdshader` | Land colours from height, latitude and climate: sea floor, drowned land, beaches, grass/desert, rock, snow, ice caps. |
| `shaders/ocean.gdshader` | Semi-transparent sea with sea ice near the poles. |
| `shaders/clouds.gdshader`, `atmosphere.gdshader`, `storm.gdshader` | Moving clouds and haze, the rim glow, the spinning hurricane. |

The ocean is a sphere whose radius is the sea level, so raising it floods low land exactly where the terrain is low.
Coastal lowlands are kept deliberately flat in `Planet.radius_at()` so that flooding is visible.

## Useful knobs
- In the Inspector on the `Planet` node at runtime, or as defaults in `planet.gd`: `seed` (a different world), `land_bias` (more or less land), `subdivisions`, `tree_count`, `spin_speed`.
- In `climate_look.gd`: sea rise, ice latitude, snow line, desert share, forest health, colours, cloud cover, haze, number of storms and fires.

## Screenshots from the command line
```
godot --path . -- --t=0.52 --shot=/tmp/2085.png           # t: 0 = 2025, 1 = 2140
godot --path . -- --t=1 --dist=1.8 --yaw=-0.9 --shot=/tmp/closeup.png
```
Other flags: `--pitch=`, `--storm`, `--fire`. The screenshot is taken after 1.5 s, then Godot quits.

## Renderer
The project uses the **Compatibility** renderer (OpenGL 3 / WebGL 2) so it can later be exported to the browser.
Keep scripts in GDScript for the same reason: Godot 4 projects written in C# cannot yet be exported to the web.

## Known gaps / next steps
- Cities are small and few; no night-side lights yet.
- No boats, whales or birds yet (they were in the concept board).
- Icebergs, calving ice sheets and dust storms are not modelled.
- Next: connect `ClimateLook` to the ported simulation, and add a close-up building mode on hex tiles.
