# Planetary Civilization Simulator --- Development Specification

Version: 0.2 (reconciled with design v0.4)\
Purpose: implementation contract for Codex / Claude Code\
Primary target: PC/Linux, C++20 + Godot 4\
Current phase: P0 --- Living Planet

## 1. Mission

Build a physically causal planetary simulation that can later support a
civilization game.

The first objective is **not** to build the civilization layer. The
first objective is a living, observable planet in which solar forcing,
surface temperature, atmosphere, winds, water, clouds, precipitation,
hydrology, snow/ice, vegetation and ocean heat transport form a coupled
system.

The simulator must favor interpretable physical state and conserved
fluxes over scripted climate modifiers.

Examples:

-   Do not implement `land_temperature += warm_current_bonus`.
-   Transport heat in the ocean, change SST, exchange heat/moisture with
    the atmosphere, then let atmospheric transport affect land.
-   Do not implement `spawn_hurricane()`.
-   Create the physical conditions under which organized rotating moist
    convection may emerge.
-   Do not implement `deforestation_climate_penalty`.
-   Change vegetation, albedo, evapotranspiration, soil infiltration and
    carbon state, then let the coupled system respond.

## 2. Non-negotiable architecture

The authoritative simulator is a standalone C++20 library named
`PlanetSim`.

Godot is a client of PlanetSim.

``` text
Godot / UI / Renderer
        |
        v
     Game API
        |
        v
    PlanetSim
        |
        v
Domain simulation modules
        |
        v
 Core / Math / Fields
```

Forbidden dependencies:

``` text
PlanetSim -X-> Godot
Physics   -X-> UI
Climate   -X-> Population entities
Core      -X-> Domain modules
```

Godot may: - submit commands; - request/read snapshots; - interpolate
snapshots for rendering; - visualize physical fields.

Godot must not: - own authoritative climate state; - mutate solver
arrays directly; - contain hidden climate/gameplay physics.

## 3. Repository layout

``` text
planet_game/
├── CMakeLists.txt
├── cmake/
├── docs/
│   ├── design/
│   ├── physics/
│   └── decisions/
├── sim/
│   ├── core/
│   │   ├── scheduler/
│   │   ├── fields/
│   │   ├── units/
│   │   ├── math/
│   │   ├── serialization/
│   │   └── diagnostics/
│   ├── planet/
│   │   ├── mesh/
│   │   ├── orbit/
│   │   ├── terrain/
│   │   └── geology/
│   ├── atmosphere/
│   │   ├── radiation/
│   │   ├── thermodynamics/
│   │   ├── circulation/
│   │   ├── moisture/
│   │   └── clouds/
│   ├── ocean/
│   │   ├── surface/
│   │   ├── circulation/
│   │   └── deep_ocean/
│   ├── hydrology/
│   ├── biosphere/
│   ├── carbon/
│   ├── observation/
│   ├── population/
│   ├── economy/
│   ├── energy/
│   ├── infrastructure/
│   ├── technology/
│   ├── government/
│   ├── scenario/
│   └── history/
├── apps/
│   ├── planet_cli/
│   ├── climate_lab/
│   └── benchmarks/
├── godot/
│   ├── project.godot
│   ├── gdextension/
│   ├── scenes/
│   ├── shaders/
│   ├── materials/
│   ├── ui/
│   └── assets/
├── scenarios/
│   ├── validation/
│   ├── industrial_dawn/
│   ├── ice_age/
│   └── polluted_world/
├── tests/
│   ├── unit/
│   ├── physics/
│   ├── conservation/
│   ├── regression/
│   └── scenarios/
└── tools/
    ├── planet_generator/
    ├── climate_analysis/
    └── state_inspector/
```

P1+ directories may initially exist only as placeholders. Do not
implement them during P0 unless required by an explicit task.

## 4. Technology and coding constraints

-   C++20.
-   CMake.
-   Linux is the first development platform.
-   Godot 4 with GDExtension / godot-cpp.
-   Core simulator must build and run without Godot installed.
-   Prefer data-oriented structures and structure-of-arrays for dense
    planetary fields.
-   Use SI units internally: K, m, s, Pa, kg, J, W, m/s.
-   Use explicit strong types or clearly named unit-bearing variables
    where practical.
-   Avoid premature CUDA dependence. CPU reference implementation first.
-   Parallelize only after correctness tests exist.
-   Preserve deterministic execution for a fixed scenario, seed and code
    version where practical.
-   Avoid deep inheritance hierarchies. Prefer composition, plain data
    and explicit process functions.
-   No global mutable simulation state.
-   No engine pointers in serialized state.
-   Every physical module must expose diagnostics sufficient to test its
    conservation behavior.

## 5. Planet discretization

Use an icosphere.

P0 resolution policy:

-   development resolution L5 with 10,242 dual surface cells;
-   shipped/reference resolution L6 with 40,962 dual surface cells;
-   atmosphere initially designed for 3--5 vertical layers;
-   lower resolutions must be supported for tests and debugging.

The accepted simulation mesh is the hexagonal--pentagonal dual of the
subdivided icosahedron. Its cells are centred on primal vertices; exactly
twelve cells are pentagons and the remainder are hexagons.

For an icosphere:

``` text
primal_faces = 20 * 4^L
dual_cells = 10 * 4^L + 2
```

Each dual surface cell needs at minimum a stable ID, center, area, local
tangent basis, and a range into immutable edge/topology arrays. Each edge
stores its neighbour, length, centroid distance, and outward normal in the
cell's local basis. Neighbour and edge topology use the CSR layout defined by
ADR-0002.

The mesh, resolution policy, field layout, precision, ordering, and
deterministic block decomposition are governed by accepted
`adrs/ADR-0002-mesh-and-field-layout.md`.

Geometry/connectivity must be immutable after initialization and
separate from evolving state.

## 6. Core data model

Initial conceptual types:

``` cpp
PlanetMesh
Field2D<T>
Field3D<T>
EdgeField<T>
SimulationClock
Scheduler
PlanetParameters
PlanetState
ForcingState
Diagnostics
StateSnapshot
```

Representative state:

``` cpp
struct SurfaceState {
    Field2D<float> elevation_m;
    Field2D<float> temperature_K;
    Field2D<float> soil_moisture_kg_m2;
    Field2D<float> snow_water_equivalent_kg_m2;
    Field2D<float> vegetation_fraction;
    Field2D<float> albedo;
};

struct AtmosphereState {
    Field3D<float> temperature_K;
    Field3D<float> pressure_Pa;
    Field3D<float> specific_humidity;
    Field3D<float> eastward_wind_m_s;
    Field3D<float> northward_wind_m_s;
    Field3D<float> cloud_water_kg_m2;
    Field2D<float> precipitation_kg_m2_s;
};

struct OceanState {
    Field2D<float> surface_temperature_K;
    Field2D<double> deep_temperature_K;
    Field2D<float> mixed_layer_depth_m;
    Field2D<float> eastward_surface_current_m_s;
    Field2D<float> northward_surface_current_m_s;
    // Salinity may begin as a constant/reference field,
    // but the architecture must permit dynamic salinity later.
};
```

These are conceptual, not mandatory exact APIs. Scalar component fields keep
the layout structure-of-arrays. Prognostic and diagnostic fields default to
`float`; global accumulators and slow ocean/carbon reservoirs use `double` as
specified by ADR-0002. Before changing semantics, document why.

## 7. Authoritative state and presentation

The solver may run many timesteps between visual frames.

``` text
PlanetSim
   |
   +--> StateSnapshot --> Godot
   ^
   |
PlayerCommand
```

`StateSnapshot` is read-oriented. It may be downsampled or transformed
for rendering.

`PlayerCommand` is validated by PlanetSim.

For P0, commands may include developer/debug operations: - set CO2
concentration; - modify solar flux; - inject/remove ocean heat; -
paint/remove vegetation; - change simulation speed; - pause/step; -
inspect a cell.

These are climate-lab controls, not necessarily final gameplay controls.

## 8. Multi-rate simulation

Do not force all systems to use the same timestep.

Conceptual cadence:

-   atmospheric dynamics: minutes;
-   clouds/moisture/weather: minutes to hours;
-   surface/hydrology: hours;
-   ocean surface: hours to days;
-   deep ocean: days to months;
-   vegetation: days to months;
-   carbon reservoirs: days to months;
-   later economy: days;
-   later population/politics/research: months to years.

The scheduler must make subsystem cadence explicit.

Reference physics and accelerated game-time physics are separate concerns.
The three explicit modes are:

-   reference mode: explicit weather, conservative small timesteps, and the
    validation/calibration baseline;
-   climate mode: the normal gameplay path, using long or implicit steps and
    statistical weather while conserving climate-scale budgets;
-   weather windows: bounded explicit regional/temporal runs seeded from
    climate-mode state.

All modes share physical time, planetary coordinates, units, and diagnostics.
Starting at M3, climate mode must target at least 20 simulated years per
wall-clock minute at L5 and 5 at L6; a 250-year headless CI scenario must
complete in under ten minutes. A regional weather window should run a season
in real time or faster. See ADR 0001.

## 9. Required planetary couplings

Treat the planet as one coupled system.

### 9.1 Radiation and surface energy

Conceptual balance:

``` text
C dT/dt =
    absorbed solar
  - outgoing longwave
  + atmospheric exchange
  + ocean/ground exchange
  - latent heat flux
```

Absorbed solar:

``` text
Q_abs = (1 - albedo) * Q_solar
```

Longwave may initially use a reduced greenhouse formulation, but it must
have physically correct signs and documented assumptions.

### 9.2 Atmosphere and wind

Atmospheric motion must respond approximately to: - pressure
gradients; - Coriolis acceleration; - friction/drag; - thermal
structure.

Conceptually:

``` text
dv/dt = -(1/rho) grad(p) - 2 Omega x v - drag + ...
```

Wind vectors must remain tangent to the spherical surface where
appropriate.

### 9.3 Water cycle

``` text
surface water
  -> evaporation
  -> atmospheric vapor
  -> transport/ascent/cooling
  -> condensation
  -> cloud water/ice
  -> precipitation
  -> soil/runoff/rivers/ocean
```

Evaporation must remove latent energy from the source surface.

Condensation must release latent heat into the atmosphere.

Water mass must be diagnosed globally.

### 9.4 Physical clouds

Clouds are state, not cosmetic effects.

They must eventually affect: - precipitation; - shortwave reflection; -
longwave trapping; - latent heating.

Godot renders the simulated cloud field.

### 9.5 Ocean-atmosphere coupling

``` text
atmospheric wind
    -> wind stress
    -> ocean currents

ocean currents
    -> heat transport
    -> SST
    -> sensible/latent/radiative air-sea flux
    -> atmosphere
    -> land climate
```

Do not apply direct ocean-current temperature bonuses to land cells.

Ocean V0 may be simplified, but must contain: - surface temperature; -
deep temperature; - surface mixed layer; - horizontal heat transport; -
air-sea heat exchange.

The architecture must permit later temperature/salinity-sensitive
density and thermohaline circulation.

### 9.6 Cryosphere

Snow and ice affect: - water storage; - latent heat; - albedo; -
freshwater input.

Ice-albedo feedback must emerge from those quantities.

### 9.7 Vegetation and soil

Vegetation affects: - evapotranspiration; - albedo; - soil
infiltration; - carbon storage; - later fire susceptibility.

Avoid arbitrary forest climate bonuses.

### 9.8 Carbon

Initial greenhouse forcing may use:

``` text
DeltaF = 5.35 * ln(C / C0)
```

Keep forcing logic distinct from the carbon-reservoir model.

Later reservoirs: - atmosphere; - vegetation; - soil; - ocean.

### 9.9 Aerosols

Do not merge aerosols into CO2.

The architecture must permit: - shortwave effects; - cloud
interactions; - finite atmospheric lifetime; - spatially heterogeneous
pollution.

Detailed aerosol physics is not required in the first implementation.

## 10. Tropical cyclones

Do not create hurricanes as arbitrary disaster events.

The architecture should permit organized tropical storms to emerge
from: - warm SST and adequate upper-ocean heat; - high lower-atmospheric
moisture; - convective instability; - a seed disturbance; - sufficient
Coriolis away from the equator; - sufficiently favorable vertical wind
shear.

Atmospheric vertical structure is therefore important. Design for 3--5
layers rather than permanently locking the model to a single layer.

Storm feedback:

``` text
organized convection
 -> condensation
 -> latent heat release
 -> lower pressure / stronger circulation
 -> increased surface fluxes
 -> stronger convection
```

Storm-ocean negative feedback:

``` text
strong winds
 -> upper-ocean mixing/upwelling
 -> cooler SST / cold wake
 -> reduced enthalpy supply
```

P0 does not require operational meteorological hurricane accuracy. It
requires a physically interpretable reduced model whose storm behavior
depends on environmental conditions rather than a random event table.

## 11. Feedback loops to preserve

At minimum, architecture and diagnostics must support:

1.  ice \<-\> albedo \<-\> temperature;
2.  temperature -\> evaporation -\> water vapor -\> radiation;
3.  clouds \<-\> shortwave/longwave radiation;
4.  ocean heat uptake -\> climate inertia;
5.  ocean circulation -\> heat transport -\> regional climate;
6.  vegetation -\> evapotranspiration -\> humidity/rain -\> soil
    moisture -\> vegetation;
7.  drought -\> fire -\> carbon release / vegetation loss;
8.  vegetation/soil -\> infiltration -\> runoff -\> erosion/flood;
9.  snow accumulation/melt -\> seasonal river discharge;
10. atmosphere \<-\> ocean carbon exchange;
11. aerosols \<-\> radiation/clouds;
12. urban surfaces/waste heat -\> local climate later.

## 12. Coupling registry rule

For every major field, document:

-   owner;
-   producers;
-   consumers;
-   units;
-   update cadence;
-   conservation budget;
-   expected range;
-   initialization;
-   serialization;
-   diagnostics.

Example:

``` text
soil_moisture
owner: hydrology
units: kg/m2
producers: precipitation, snowmelt, irrigation
consumers: evaporation, transpiration, runoff, drainage
budget: water
```

A new cross-module dependency should not be added casually. Prefer
explicit flux/state interfaces.

## 13. P0 milestone plan

### M0 --- Icosphere and simulation skeleton

Deliver: - CMake project; - PlanetSim static/shared library; - tests; -
icosphere generator; - immutable topology; - `Field<T>`; -
`PlanetState`; - simulation clock; - headless `planet_cli`; - basic
Godot GDExtension bridge; - renderable sphere colored by a scalar field.

Acceptance: - known cell counts at L0--L6; - valid topology; -
normalized centers; - positive areas; - total area approximately
`4*pi*R^2`; - no invalid neighbor indices; - headless tests run without
Godot.

### M1 --- Orbit, sun, day/night and seasons

Deliver: - inertial, body-fixed and local tangent coordinate concepts; -
rotation axis and sidereal period; - fixed Keplerian orbital elements; -
numerical Kepler-equation solution; - orbital distance and inverse-square
stellar flux; - axial tilt; - solar direction; - top-of-atmosphere
insolation; - day/night; - seasonal variation; - stable local East/North/Up
bases.

Default Earth-like parameters: - radius \~6,371 km; - day \~24 h; -
mass \~5.9722e24 kg; - axial tilt \~23.44 deg; - year \~365 days; -
semi-major axis \~1 AU; - eccentricity \~0.0167; - incident flux near
1 AU \~1361 W/m2.

Acceptance: - zero night-side insolation; - equinox symmetry; - solstice
hemisphere asymmetry; - correct day/night progression; - inverse-square
distance forcing; - annual global-mean incoming solar approximately S0/4
for circular orbit; - finite orthonormal right-handed local tangent bases.

The implementation conventions, derived-state ownership, numerical
approximation, and test matrix are defined in `docs/M1_TECHNICAL_SPEC.md`.

### M2 --- Terrain and ocean mask

Deliver: - procedural elevation; - configurable sea level; - land/ocean
mask; - basic terrain rendering.

Do not spend excessive time on realistic geology yet.

### M3 --- Surface energy balance

Deliver: - absorbed shortwave; - longwave cooling; - heat capacity; -
stable temperature integration; - land/ocean heat-capacity
distinction; - energy diagnostics.

### M4 --- Atmosphere and pressure

Deliver: - layered atmospheric state; - temperature/pressure
initialization; - simple hydrostatic vertical structure; -
atmosphere-surface exchange.

### M5 --- Wind and Coriolis

Deliver: - horizontal pressure-gradient response; - tangent-space
wind; - Coriolis; - friction; - stable transport operators.

### M6 --- Humidity and evaporation

Deliver: - saturation relation; - evaporation; - humidity transport; -
latent surface cooling; - global water diagnostics.

### M7 --- Clouds and precipitation

Deliver: - condensation; - cloud water; - latent atmospheric heating; -
precipitation; - simplified cloud-radiation coupling.

### M8 --- Hydrology

Deliver: - soil moisture; - infiltration; - runoff; -
drainage/catchments; - river discharge.

### M9 --- Snow and ice

Deliver: - snowfall; - accumulation; - melt; - albedo coupling; - latent
energy; - freshwater transfer.

### M10 --- Vegetation

Deliver: - climate suitability; - vegetation fraction; -
evapotranspiration; - albedo coupling; - soil-water coupling.

### M11 --- Ocean heat and currents

This is a core P0 climate milestone, not cosmetic polish.

Deliver: - mixed-layer SST; - deep thermal reservoir; - wind-driven
surface currents; - horizontal heat transport; - air-sea heat/moisture
exchange.

Acceptance: - recognizable maritime vs continental behavior; - current
changes alter SST; - coastal climate changes through atmospheric
coupling.

### M12 --- CO2 forcing

Deliver: - configurable atmospheric CO2; - greenhouse forcing; - climate
response; - forcing diagnostics.

### M13 --- Climate equilibrium experiments

Automate: - no atmosphere; - doubled CO2; - zero rotation; - high axial
tilt; - water world; - frozen world; - altered ocean heat transport; -
deforestation perturbation; - long equilibrium run.

### M14 --- Visual polish

Render authoritative: - temperature; - wind; - currents; -
precipitation; - soil moisture; - vegetation; - snow/ice; - clouds; -
ocean temperature; - energy imbalance.

Wind/current visualization should support animated tracers/particles.

### M15 --- Coupled storm experiment

Attempt organized tropical rotating storms in favorable environments.

A/B tests: - warm ocean + low shear; - same ocean + high shear; -
insufficient Coriolis near equator; - cooler ocean; - varying
mixed-layer depth.

If the reduced model cannot produce useful storm dynamics without
destabilizing P0, document the limitation and implement a physically
constrained mesoscale parameterization rather than an arbitrary random
disaster generator.

## 14. Validation and CI

Tests are first-class.

Required categories:

``` text
tests/unit
tests/physics
tests/conservation
tests/regression
tests/scenarios
```

Initial tests:

-   icosphere topology and area;
-   solar geometry;
-   no-energy temperature invariance;
-   sign of radiative cooling;
-   evaporation water transfer;
-   evaporation latent cooling;
-   condensation latent heating;
-   global water conservation;
-   ice-albedo sign;
-   ocean heat transport sign;
-   SST-to-atmosphere coupling;
-   CO2 forcing sign;
-   deterministic replay;
-   serialization round trip.

Long-run CI scenarios should track tolerances for: - total energy
residual; - total water residual; - later total carbon residual; -
NaN/Inf count; - min/max temperatures; - max wind/current; - drift from
stored regression metrics.

Never accept a visually plausible simulation as proof of correctness.

## 15. Diagnostics

The CLI and climate lab should expose at least:

``` text
simulation time
wall-clock performance
global mean surface temperature
min/max surface temperature
incoming solar
reflected shortwave
outgoing longwave
net energy imbalance
atmospheric water
soil water
snow/ice water
ocean water reference inventory
precipitation rate
evaporation rate
mean SST
ocean heat content
CO2 concentration
NaN/Inf counters
```

Later add per-module timing.

## 16. Scenario system

No initial condition should be hard-coded as "the game."

Scenario data should eventually define:

``` text
Planet state
Climate state
Biosphere state
Population state
Infrastructure state
Technology state
Economy state
Knowledge state
Government state
```

P0 validation scenarios may contain only physical state.

Future first gameplay scenario: `industrial_dawn`, approximately
1880--1900-equivalent technology.

Other planned scenarios: - ice age; - polluted industrial world; -
post-collapse; - dry world; - water world; - high-CO2 world; -
procedural planets.

## 17. Observation architecture

The true world state and player-visible state are distinct.

``` text
TRUE WORLD STATE
    -> sensors
    -> measurements + uncertainty
    -> scientific knowledge/models
    -> observed state / forecast
    -> UI
```

During P0, `climate_lab` may expose true state for debugging.

Do not bake omniscient debug access into the future gameplay UI.

## 18. Future civilization interface

Do not implement now, but keep the physical interface clean.

Civilization may eventually inject/modify:

``` text
CO2 emissions
CH4 emissions
aerosols
land use
vegetation removal/planting
water withdrawal
irrigation
reservoir operations
waste heat
ocean engineering
solar geoengineering
carbon removal
```

PlanetSim returns physical consequences:

``` text
weather
climate
water availability
river discharge
soil conditions
crop environment
hazards
resource conditions
```

Civilization code must not command desired climate outcomes.

## 19. Explicit P0 non-goals

Do not implement yet:

-   individual citizens;
-   elections/government;
-   detailed economy;
-   warfare;
-   diplomacy;
-   detailed cities;
-   vehicles;
-   financial markets;
-   100+ commodities;
-   full CFD;
-   full Navier-Stokes ocean;
-   operational NWP accuracy;
-   photorealistic cities;
-   multiplayer;
-   Android;
-   exact historical Earth reproduction.

## 20. Development workflow for an AI coding agent

For each requested milestone:

1.  Inspect the existing repository before editing.
2.  Summarize relevant architecture and current implementation.
3.  Identify the smallest coherent implementation slice.
4.  State assumptions.
5.  Implement production code and tests together.
6.  Build.
7.  Run relevant unit/physics/conservation tests.
8.  Run at least one headless scenario where applicable.
9.  Report numerical diagnostics, not only "tests pass."
10. Do not silently weaken a test to make it pass.
11. Do not introduce a dependency that violates the architecture.
12. If a design decision has multiple credible options with major future
    consequences, stop and present the alternatives before committing to
    one.
13. Record significant architecture choices in `docs/decisions/`.
14. Keep commits milestone-sized and understandable.

## 21. First development task (completed)

Start with **M0 only**.

Do not begin atmospheric physics yet.

Implement:

1.  repository skeleton;
2.  top-level CMake;
3.  `sim/core`;
4.  `sim/planet/mesh`;
5.  `apps/planet_cli`;
6.  `tests/unit` and `tests/physics`;
7.  icosphere generation;
8.  cell IDs and immutable connectivity;
9.  cell center, area, neighbor and edge-length data;
10. generic dense `Field<T>`;
11. `PlanetParameters`;
12. `SimulationClock`;
13. minimal `PlanetState`;
14. mesh diagnostics;
15. CLI command to generate a planet and print diagnostics.

The CLI should support something similar to:

``` bash
./planet_cli mesh --subdivision 6 --radius 6371000
```

Expected diagnostics:

``` text
subdivision
cell count
vertex count if applicable
total area
expected sphere area
relative area error
min/max cell area
neighbor validity
construction time
```

Add tests for L0 through at least L6.

Only after M0 is clean, tested and reviewed should development proceed
to M1.

The original primal-mesh M0 was completed and reviewed on 2026-09-23.
ADR-0002 was accepted on 2026-09-25 and superseded that representation. The
dual-mesh M0 infrastructure and mesh-dependent M1 work were migrated and
revalidated on 2026-09-25. This did not begin M2: finite-volume operators,
terrain, conservative remapping, and persistent snapshots remain later work.

## 22. Definition of done for M0

M0 is complete when:

-   a clean checkout configures and builds with documented commands;
-   `planet_cli` runs without Godot;
-   icosphere topology tests pass;
-   sphere-area error is within a documented tolerance;
-   no invalid adjacency exists;
-   `Field<T>` has basic bounds/size tests;
-   deterministic mesh generation is demonstrated;
-   sanitizer-friendly code is used;
-   there are no compiler warnings under the chosen warning set;
-   README contains build/test commands;
-   an ADR records the chosen mesh representation;
-   the agent provides a concise implementation report and identifies
    remaining M1 prerequisites.

Do not optimize M0 for GPU execution. Correct topology, numerical
clarity, tests and clean interfaces are more important.
