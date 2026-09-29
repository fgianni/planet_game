# The Godot prototype as a behavioural oracle

Specification §25 keeps the GDScript prototype alive as the gameplay fast lane
and as a **behavioural oracle** for PlanetSim: run the C++ core against the same
experiments and compare with the prototype's recorded results before trusting
its own calibration. This document extracts what the oracle offers, so that
PlanetSim work never has to start from the GDScript.

Source: `prototype/climate_planet_0.5/` (Godot 4.7, GDScript), mainly
`scripts/climate_model.gd`, `tests/validate_climate.gd`,
`tests/validate_ocean.gd` and `README.md`. Earlier prototype versions are in
git history.

## 1. What the prototype is

A reduced, explainable climate model coupled to a small game:

- **Grid:** a level-4 icosphere with one cell per vertex, 2,562 cells: the
  same cell count as PlanetSim's L4 dual mesh. Positions differ (the
  prototype's pole is on +Y, PlanetSim's on +Z, and PlanetSim's centres are
  optimised), so compare fields after mapping each prototype cell to the
  nearest PlanetSim cell, or compare zonal means.
- **Time:** annual-mean climate, quarter-year steps (`step(0.25)`), spin-up to
  a preindustrial equilibrium at 280 ppm.
- **Physics:** Budyko–Sellers energy balance per cell; a slow deep ocean; ice
  with the ice–albedo feedback; a prescribed three-cell wind pattern whose
  edges and strengths respond to climate; moisture transport and rain with
  orographic lift; high and low clouds with a tunable low-cloud feedback;
  Stommel wind-driven gyres; a Stommel two-box overturning circulation;
  vegetation following rainfall.
- **Game:** one region, buildings, five-year turns, a shadow planet, fast
  local loops, a newspaper, and seeded, replayable games with an action log.

## 2. Validation recipes to port

Each is a headless experiment with a numeric target; together they are most
of the calibration harness of specification §23 and §24. Port the *recipe*,
not the code. Values are those recorded in the prototype README; the README
notes that the atmosphere-only numbers (marked †) were measured before the
ocean was added and were not all re-measured afterwards.

| Recipe (prototype source) | Procedure | Target | Prototype result |
|---|---|---|---|
| Preindustrial equilibrium (`validate_climate`) | spin up 300 years at 280 ppm | 13–15 °C | 14.2 °C |
| Equilibrium sensitivity | from equilibrium, set 560 ppm, spin up 400 years | 2.5–4 °C | +3.15 °C (medium cloud feedback) |
| Sensitivity across cloud feedback | as above for low, medium and high low-cloud loss (0, 2.5, 5 %/K) | spans roughly 2–5 °C | +2.4 / +3.1 / +4.4 °C † |
| Transient / equilibrium | CO₂ +1 %/yr to doubling (~70 years), compare with equilibrium | 0.5–0.75 | 0.63 † |
| Polar amplification | poles (>60°) vs tropics (<30°) warming at doubling | ×2–4 | ×1.6, **below target** |
| Land vs ocean warming | 60°S–60°N, ice-free, same latitudes | land warms more, ~×1.3–1.6 | too weak (README) |
| Hadley-cell edge | edge latitude before and after doubling | moves poleward | 30° → 33.5° † |
| Global precipitation | %/K at doubling | +2 to +3 %/K | +3.4 %/K † |
| Tropical circulation | trade-wind strength %/K | slows, −1 to −2 %/K | −1.9 %/K † |
| Wet vs dry belts | equatorial vs subtropical zonal rain | wet vs dry by a factor of several | 2,300 vs 270 mm/yr † |
| Rain shadow | windward vs lee slopes of land above 300 m | windward clearly wetter | yes |
| Western boundary currents (`validate_ocean`) | poleward velocity on western vs eastern subtropical basin edges | poleward and fast on the west | +0.35 vs −0.30 m/s |
| SST, west vs east | same latitude, 15–40° | western warmer | 20.0 vs 17.7 °C (N), 20.7 vs 19.5 °C (S) |
| Overturning, scenario A | CO₂ +1 %/yr to 560 ppm, hold 330 years | weakens 15–40 %, no collapse | −29 % after 400 years |
| Overturning, scenario B | CO₂ +1 %/yr to 1,120 ppm, hold | collapse possible | collapses ~250 years after the threshold |
| Overturning, scenario C | as B, hold 250 years, then back to 280 ppm | stays collapsed (hysteresis) | stays at 0 %; northern sea 1.2 °C colder than 1850 while the planet is +0.6 °C |
| Brief overshoot | cross the threshold and reverse quickly | may recover | recovers from 52 % to 88 % |
| Historical forcing | observed CO₂ 1850–2025 | ≈ +1.2 °C | +1.24 °C, not tuned for † |

Differences to expect: PlanetSim resolves the seasonal cycle (specification §8)
and radiates as εσT⁴, so its equilibria will not match the prototype's
annual-mean, linearised model cell by cell. Compare the targets and the
qualitative behaviour, not individual cell values.

## 3. Constants worth starting from

Calibrated together in the prototype; treat them as documented starting
points, and re-derive them where PlanetSim's formulation differs.

| Quantity | Prototype value | Use in PlanetSim |
|---|---|---|
| Ocean mixed-layer heat capacity | 2.9e8 J/m²/K (~70 m) | M3; reuse |
| Land heat capacity | 2.5e7 J/m²/K | M3; reuse, split by rock / dry / wet soil |
| Deep-ocean heat capacity | 2.5e9 J/m²/K | M3/M11; reuse |
| Mixed layer ↔ deep exchange | 0.7 W/m²/K | M3/M11; reuse; it sets the committed-warming lag |
| CO₂ forcing | 5.35·ln(C/280) W/m² | M12; as specification §9.8 |
| Sea-ice / land-ice formation | from −2 °C (onset) to −10 °C (full), annual mean | M4; re-derive from seasonal freeze/melt |
| Ice response time | sea ice ~2 years, land ice ~60 years | M4; a check on the seasonal model |
| Ice albedo (planetary, clouds included) | 0.5 | M4; re-derive as surface albedo once clouds are separate |
| Saturation water | 25·exp(0.064·(T − 15)) mm precipitable water | M7; replace with Clausius–Clapeyron vapour pressure |
| Evaporation scale / rain-out rate | 1,350 mm/yr; 45 /yr (~8-day vapour lifetime) | M7/M8; sanity checks on residence time |
| Overturning (Stommel two-box) | τ = 40 years, baseline freshwater 0.09, collapse when forcing > x²/4 | M11; the box model is a good first tipping element and hysteresis test |
| Gyre friction / fastest current | Stommel bottom friction 0.1 (unit sphere), 1.5 m/s | M11; the boundary-layer width depends on resolution |

## 4. What must not be carried over

These are scripted or unphysical shortcuts, several of which the
specification forbids outright:

- **Split CO₂ forcing** (`LAND_FORCING` 1.6, `OCEAN_FORCING` 0.53) to make land
  warm faster. Specification M3: do not fake lapse-rate effects before the
  atmosphere exists.
- **Rescaling global rain to equal evaporation** every step to hide the
  transport's non-conservation. Conservation must be structural.
- **Linearised outgoing radiation** (A + BT) with a latitude-dependent
  coefficient, clamped to stay positive. The clamp is the calibration lesson
  of specification §23; the linearisation is replaced by εσT⁴.
- **Prescribed three-cell winds.** Their response to climate is a good
  target; the pattern itself must come from the M5/M6 solver.
- **The overturning heat flux as a fixed 15 W/m² injection** taken from the
  tropical ocean. Energy-conserving, but a parameterisation to be replaced
  by transport at M11.
- **Binary land/ocean** and an elevation scale in planet radii; PlanetSim uses
  ADR-0005 hypsometry.
- **Sea-level rise exaggerated about 50×** for visibility.
- **Serial Gauss–Seidel sweeps and `float32` accumulation** with no
  deterministic reduction; PlanetSim uses the ADR-0002 §4.6 rules.
- **Visual-only weather:** the GPU cloud advection and hurricanes are
  presentation, not physics (the prototype README says so).

## 5. Gameplay and tooling worth reusing

- **Shadow planet:** a second model where the player's region never
  industrialised, differenced against the real one, reported as
  "+0.45 °C and −17 mm/yr here, +0.52 °C worldwide". This is specification
  §7.1 and M14.1.
- **Fast local loops:** clearing lowers `veg_use` (less moisture returned,
  less rain downwind); cities and bare land add local forcing. This matches
  specification §9.14 in effect, but in PlanetSim the local heating must come
  from surface properties, not a W/m² term.
- **Replayable games:** `GameState.export_log()` writes schema, seed, status,
  per-turn records and the ordered action list. It is a good template for the
  ADR-0003 run manifest and command log (M3).
- **Playtest data:** `prototype/html/warming-stripes-logs-2026-09-16.json`
  holds two logged games from the HTML prototype (seed, laws, research,
  builds, actions, per-turn state).
- **Rendering:** the terrain, ocean, cloud and storm shaders and the pattern
  "model → data texture → shader" are relevant to M14 and the Godot adapter.

## 6. Running the oracle

Requires Godot 4.7; from `prototype/climate_planet_0.5/`:

```bash
godot --headless --path . -s tests/validate_climate.gd
godot --headless --path . -s tests/validate_ocean.gd -- --scenarios
godot --headless --path . -s tests/test_game.gd
```

Spin-up takes about 15 s. Record the output with the date and commit when a
PlanetSim result is compared against it.
