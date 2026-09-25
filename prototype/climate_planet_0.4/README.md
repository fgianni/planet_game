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
- **Map**: Planet · Temperature · Warming since 1850 · Rainfall · Rain change · Clouds · Cloud change (each with a legend).
- **Cloud feedback: low / medium / high**: how strongly low clouds thin as the sea warms — the largest uncertainty in real climate projections (see below).
- **Winds on/off**: animated streaks following the model's winds.
- **Currents on/off**: ocean current streaks, coloured by water temperature (red warm → blue cold).
- **Meltwater pulse**: floods the northern ocean with fresh water (fades over ~25 years) — try it to push the overturning circulation towards its tipping point.
- The first line of the readout shows the **ocean overturning** strength (strong / weakening / collapsing / collapsed) and warns when it is past its tipping threshold.

The first launch spins the model up to a preindustrial equilibrium (about 15 s, in a background thread) and caches it in `user://`; later launches are instant.

## The climate model (`scripts/climate_model.gd`)
A deliberately simple, explainable model on a 2,562-cell icosphere (one cell per vertex). Pure data, no nodes: deterministic, runs in a worker thread, testable headless.

- **Temperature** – energy balance per cell (Budyko–Sellers type): sunlight by latitude × (1 − albedo), minus heat lost to space, plus CO₂ forcing `5.35·ln(C/280)` W/m². Heat spreads to neighbours and is carried by the wind. Ocean cells exchange heat with a slow deep ocean, which creates the **committed-warming lag**.
- **Ice** – forms when a cell's annual mean drops below about −2 °C (full cover at −10 °C). Ice is bright, so melting it warms the cell further: the **ice–albedo feedback** and **polar amplification** emerge by themselves. Sea ice reacts within a few years, land ice sheets over decades.
- **Winds** – three circulation cells per hemisphere (trade winds, westerlies, polar easterlies). Winds are weaker over land and mountains. The circulation responds to the climate in three ways:
  - the edge of the tropical cell (and the dry belts with it) moves **poleward** as the planet warms;
  - the **westerlies scale with the equator–pole temperature contrast** (the thermal-wind link): the poles warm faster, the contrast shrinks, the westerlies weaken;
  - the **tropical circulation slows**: water vapour grows ~6–7 %/K (constant relative humidity) but evaporation and rain only ~3 %/K, so the air has to be exchanged more slowly. The model computes this ratio each step rather than imposing it.
- **Moisture and rain** – evaporation (energy-limited: a few % more per °C), carried by the wind, capped by a humidity ceiling that is low where air sinks and high where it rises. Rain comes from each cell's moisture budget. Windward slopes get wet and lee sides dry (rain shadows). The tropical overturning circulation ships moisture from the sinking subtropics to the rising tropical rain belt. Water is conserved globally.
- **Clouds** – high/convective clouds where air rises and rain falls; **low cloud decks** over the cool subtropical oceans where air sinks. Low clouds are bright; as the sea warms they thin and let in more sunlight: the **cloud feedback**. Its strength (`cloud_feedback`) is uncertain in reality, so it is a setting: low (0), medium (2.5 %/K of low cloud lost), high (5 %/K). The average effect of clouds is already in the calibrated albedo; only their *change* since the baseline alters the energy balance, so the baseline climate is the same for all three settings.
- **Ocean currents (wind-driven)** – the Stommel gyre model: the winds' twisting force (wind-stress curl), the change of the Coriolis effect with latitude (β) and friction. Solved for a streamfunction on the ocean cells (zero at the coasts). **Western boundary currents emerge by themselves** (Gulf-Stream-like, 1.5 m/s), with slow equatorward drift in the interior and cold equatorward currents along eastern edges. Currents carry heat. Poleward heat transport is split between the atmosphere (heat spreading) and the ocean (currents), roughly two thirds / one third as on Earth.
- **Overturning circulation (AMOC-like)** – Stommel's two-box model, placed over the ocean sector with the most northern sea (found automatically). Its strength is driven by the tropical-vs-northern temperature difference and weakened by fresh water (northern land ice melting, more northern rain, meltwater pulses). When strong it brings heat north (15 W/m² to that region, taken from the tropical ocean). It is **bistable**: past a threshold it collapses, and the collapsed state stays stable even after the forcing returns. Because it responds over decades, a **brief overshoot** of the threshold can still recover if emissions are reversed quickly.
- **Vegetation** follows rainfall; the game can lower it (`veg_use`, e.g. deforestation), which reduces evaporation and rain downwind.

### Validation
Atmosphere: `godot --headless --path . -s tests/validate_climate.gd`
Ocean: `godot --headless --path . -s tests/validate_ocean.gd` (gyre checks) and `-- --A`, `-- --B`, `-- --C` for the overturning scenarios.

| Check | Target | Model |
|---|---|---|
| Preindustrial global mean (280 ppm) | ≈ 14 °C | 14.2 °C |
| Equilibrium warming for doubled CO₂ (medium cloud feedback) | ≈ +3 °C | +3.15 °C |
| Polar amplification | ×2–4 | ×1.6 – **weaker than wanted** (ocean mixing and the weakening overturning cool the north) |
| Western vs eastern boundary currents (subtropics) | poleward on the west, equatorward on the east | +0.35 vs −0.30 m/s (both hemispheres) |
| Sea temperature, western vs eastern side (same latitude) | western warmer | 20.0 vs 17.7 °C (north), 20.7 vs 19.5 °C (south) |
| Overturning under doubled CO₂ | weakens ~15–40 %, no collapse | −29 % after 400 years, below threshold |
| Overturning under quadrupled CO₂ | can collapse | collapses (≈ 250 years after the threshold is crossed) |
| After collapse, CO₂ back to 280 ppm | stays collapsed (hysteresis) | stays at 0 %; the northern sea ends **1.2 °C colder than in 1850** while the planet is still +0.6 °C |
| Brief overshoot of the threshold | may recover if reversed quickly | recovers from 52 % back to 88 % |

Before the ocean was added (atmosphere-only calibration) the model also reproduced: the committed-warming lag (0.63), the poleward shift of the dry belts, the tropical circulation slowdown (−1.9 %/K), weaker westerlies, thinning low clouds, rain +3.4 %/K, and +1.24 °C for the historical 1850–2025 run. These were not all re-measured after the ocean recalibration — rerun `validate_climate.gd` to update them.

Calibration lessons worth keeping:
- An earlier calibration relied on the heat-loss parameter going slightly *negative* near the poles (unphysical). It is now clamped to at least a quarter of its mean everywhere.
- Adding ocean heat transport on top of an atmosphere calibrated to carry *all* the heat double-counted the transport (the planet warmed to 17 °C and stopped amplifying at the poles). The atmospheric part was reduced to leave room for the ocean.

### Known limitations
- No seasons (annual means only). Only surface gyres and one overturning cell; no equatorial current system, upwelling or sea-ice drift.
- Wind *patterns* are prescribed (three cells); only their position and strength respond to the climate, they are not solved from pressure gradients.
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
Sandbox flags: `--years=`, `--hist`, `--nocurrents`, `--co2=`, `--feedback=low|medium|high`, `--overlay=off|temperature|warming|rain|rainchange|clouds|cloudchange`, `--nowind`, `--nospin`, `--frame=` (capture frame), `--yaw=`, `--pitch=`, `--dist=`.

## Renderer
Compatibility renderer (OpenGL 3 / WebGL 2) and GDScript only, so a browser export stays possible (Godot 4 C# projects cannot yet export to the web).

## Next steps
- Seasons (moving rain belt, monsoons).
- Connect the game simulation: emissions from buildings → CO₂; land use → `veg_use`; regional harvests from local rain and temperature.
