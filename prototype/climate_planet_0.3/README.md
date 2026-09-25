# Climate Planet – Godot 4.7 prototype

Two scenes:

| Scene | What it is |
|---|---|
| `sandbox.tscn` (**main scene**, F5) | The planet driven by a simplified **climate model**: change CO₂ and watch temperature, ice, rain, deserts, clouds, forests, sea level and winds respond. |
| `main.tscn` (F6 on it) | The original look-dev scene: one hand-made warming slider (`ClimateLook` keyframes). |

## Climate sandbox – controls
- Drag to orbit, mouse wheel to zoom.
- **Play / Pause**, **Speed ×** (0.5 → 4 simulated years per second).
- **Historical CO₂ to 2025**: follows a fit of the observed CO₂ curve from 1850 to 2025, then holds. Moving the **CO₂** slider takes over (CO₂ changes by at most 4 ppm a year).
- **Map**: Planet · Temperature · Warming since 1850 · Rainfall · Rain change since 1850 (each with a legend).
- **Winds on/off**: animated streaks following the model's winds.

The first launch spins the model up to a preindustrial equilibrium (about 15 s, in a background thread) and caches it in `user://`; later launches are instant.

## The climate model (`scripts/climate_model.gd`)
A deliberately simple, explainable model on a 2,562-cell icosphere (one cell per vertex). Pure data, no nodes: deterministic, runs in a worker thread, testable headless.

- **Temperature** – energy balance per cell (Budyko–Sellers type): sunlight by latitude × (1 − albedo), minus heat lost to space, plus CO₂ forcing `5.35·ln(C/280)` W/m². Heat spreads to neighbours and is carried by the wind. Ocean cells exchange heat with a slow deep ocean, which creates the **committed-warming lag**.
- **Ice** – forms when a cell's annual mean drops below about −2 °C (full cover at −10 °C). Ice is bright, so melting it warms the cell further: the **ice–albedo feedback** and **polar amplification** emerge by themselves. Sea ice reacts within a few years, land ice sheets over decades.
- **Winds** – three circulation cells per hemisphere (trade winds, westerlies, polar easterlies). The edge of the tropical cell moves poleward as the planet warms. Winds are weaker over land and mountains.
- **Moisture and rain** – evaporation (energy-limited: a few % more per °C), carried by the wind, capped by a humidity ceiling that is low where air sinks and high where it rises. Rain comes from each cell's moisture budget. Windward slopes get wet and lee sides dry (rain shadows). The tropical overturning circulation ships moisture from the sinking subtropics to the rising tropical rain belt. Water is conserved globally.
- **Vegetation** follows rainfall; the game can lower it (`veg_use`, e.g. deforestation), which reduces evaporation and rain downwind.

### Validation (`tests/validate_climate.gd`)
Run: `godot --headless --path . -s tests/validate_climate.gd`

| Check | Target | Model |
|---|---|---|
| Preindustrial global mean (280 ppm) | ≈ 14 °C | 14.0 °C |
| Equilibrium warming for doubled CO₂ | ≈ +3 °C (IPCC likely range 2.5–4) | +3.45 °C |
| Transient warming / equilibrium (1 %/yr to doubling) | 0.5–0.75 | 0.57 |
| Polar amplification | ×2–4 | ×2.6 |
| Global rain response | 2–3 %/K | +3.5 %/K |
| Subtropical dry belt edge | moves poleward | 30° → 33.8° |
| Tropical rain belt vs subtropics | wet vs dry | ≈ 2,300 vs 270 mm/yr |
| Rain shadows | windward wetter | yes |
| Historical run 1850 → 2025 | ≈ +1.2 °C observed | +1.20 °C (not tuned for) |
| Land warms faster than ocean | ×1.3–1.6 | ×1.04 – **known limitation** |

### Known limitations
- No seasons (annual means only), no ocean currents yet (the ocean stores heat but does not move it).
- The land/ocean warming contrast is too weak.
- Deserts cover about 40 % of ice-free land (Earth's hyper-arid and arid land is roughly 20–30 %); it partly reflects this planet's geography, which puts much land in the subtropics.
- Sea-level rise is exaggerated about 50× for visibility and follows the deep-ocean warming only.

### Clouds and hurricanes follow the winds
- **Clouds** use a *flow map*: the model's winds are written into a second small texture, and the cloud shader shifts its cloud pattern along the local wind (westward in the trade-wind belt, eastward in the westerlies). Two copies half a cycle apart are cross-faded so the pattern moves continuously without stretching. Cloud *amount* still comes from the model (more cloud where air rises and rain falls).
  Tuning: `flow_speed` and `flow_cycle` in `shaders/clouds.gdshader`. Keep `flow_speed × flow_cycle × wind speed` well below a cloud's size (≈ 0.3 rad), otherwise clouds smear into streaks. The wind streaks use the same speed (`WIND_SPEED` in `climate_view.gd`) so both move together.
- **Hurricanes** ride the trade winds, drift poleward as real ones do, and fade out when they leave the tropics or make landfall; a new one then forms over warm tropical water. How many can exist depends on how much tropical ocean is above about 27.5 °C.

## How the pieces fit
`ClimateModel` (physics) → `ClimateView` (data texture, overlays, wind streaks) → `Planet` shaders and trees.
The data texture (256×128, RGBA = temperature, √rain, cloudiness, ice) is sampled by the terrain, ocean and cloud shaders when `use_climate` is on. Tree health comes from the model's vegetation, temperature and ice at each tree.

## Files
| File | Role |
|---|---|
| `scripts/climate_model.gd` | The climate model; constants at the top, calibrated together (see comments). |
| `scripts/climate_view.gd` | Model → visuals: data texture, overlays and legends, wind streaks. |
| `scripts/sandbox.gd` | Sandbox scene: threading, CO₂ scenarios, UI, planet coupling. |
| `scripts/planet.gd` | Builds the planet (terrain, ocean, clouds, atmosphere, trees, cities, events). |
| `scripts/climate_look.gd` | Keyframe palette (still used for atmosphere colours and haze in the sandbox). |
| `scripts/main.gd` | Look-dev scene. |
| `shaders/*` | Terrain, ocean, clouds, atmosphere, storm. |
| `tests/validate_climate.gd` | Headless checks against known climate patterns. |

## Command-line screenshots
```
godot --path . res://sandbox.tscn -- --years=175 --hist --overlay=warming --shot=/tmp/2025.png
godot --path . res://sandbox.tscn -- --years=300 --co2=700 --overlay=rainchange --shot=/tmp/future.png
godot --path . res://main.tscn -- --t=0.52 --shot=/tmp/lookdev.png
```
Sandbox flags: `--years=`, `--hist`, `--co2=`, `--overlay=off|temperature|warming|rain|rainchange`, `--nowind`, `--nospin`, `--frame=` (capture frame), `--yaw=`, `--pitch=`, `--dist=`.

## Renderer
Compatibility renderer (OpenGL 3 / WebGL 2) and GDScript only, so a browser export stays possible (Godot 4 C# projects cannot yet export to the web).

## Next steps
- **Ocean currents**: wind-driven gyres carrying heat poleward, plus a single overturning-circulation strength that weakens as meltwater freshens the northern ocean (a regional-cooling tipping point).
- Seasons (moving rain belt, monsoons).
- Connect the game simulation: emissions from buildings → CO₂; land use → `veg_use`; regional harvests from local rain and temperature.
