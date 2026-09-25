# Planetary Civilization Simulator --- Development Specification

Version: 0.2\
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

Target P0 reference resolution:

-   subdivision level L6;
-   81,920 triangular surface cells;
-   atmosphere initially designed for 3--5 vertical layers;
-   lower resolutions must be supported for tests and debugging.

For an icosphere:

``` text
faces = 20 * 4^L
```

Each surface cell needs at minimum:

``` cpp
CellId id;
Vec3d center_unit;
double area_m2;
std::array<CellId, 3> neighbors;
std::array<double, 3> edge_length_m;
```

If the implementation uses a dual mesh or changes neighbor count,
document the decision in an ADR before propagating the assumption
through the solver.

Geometry/connectivity must be immutable after initialization and
separate from evolving state.

## 6. Core data model

Initial conceptual types:

``` cpp
PlanetMesh
Field<T>
LayeredField<T>
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
    Field<double> elevation_m;
    Field<double> temperature_K;
    Field<double> soil_moisture_kg_m2;
    Field<double> snow_water_equivalent_kg_m2;
    Field<double> vegetation_fraction;
    Field<double> albedo;
};

struct AtmosphereState {
    LayeredField<double> temperature_K;
    LayeredField<double> pressure_Pa;
    LayeredField<double> specific_humidity;
    LayeredField<Vec3d> wind_m_s;
    LayeredField<double> cloud_water_kg_m2;
    Field<double> precipitation_kg_m2_s;
};

struct OceanState {
    Field<double> surface_temperature_K;
    Field<double> deep_temperature_K;
    Field<double> mixed_layer_depth_m;
    Field<Vec3d> surface_current_m_s;
    // Salinity may begin as a constant/reference field,
    // but the architecture must permit dynamic salinity later.
};
```

These are conceptual, not mandatory exact APIs. Before changing
semantics, document why.

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

Reference physics and accelerated game-time physics are separate
concerns.

Reference mode: - conservative; - deterministic; - numerically stable; -
validation baseline.

Accelerated mode may: - increase timestep; - reduce weather update
frequency; - use reduced-order approximations.

It must still preserve climate-scale budgets within documented
tolerances.

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

The milestone order is revised so that basic snow/ice and albedo
feedback are available before atmospheric dynamics. M2 also becomes a
geological planet generator rather than a simple noise-based terrain
generator.

### M0 --- Icosphere and simulation skeleton

Keep the existing M0 implementation contract unchanged. M0 must be
reviewed and accepted before Codex proceeds to M1.

### M1 --- Orbit, Sun, rotation, day/night and seasons

Earth is a default parameter set, not a hard-coded planet.

Design toward:

``` cpp
struct PlanetParameters {
    double radius_m;
    double mass_kg;
    double rotation_period_s;
    double axial_tilt_rad;
    double orbital_period_s;
    double semi_major_axis_m;
    double eccentricity;
    double longitude_periapsis_rad;
    double star_luminosity_W;
};
```

Use three explicit coordinate concepts:

``` text
STAR / INERTIAL FRAME
    orbital position and orbital plane
            |
            v
PLANET ROTATING FRAME
    icosphere, terrain, ocean, atmosphere
            |
            v
LOCAL TANGENT FRAME
    East / North / Up
    wind, currents, slopes
```

The 3D unit vector is authoritative surface geometry. Latitude and
longitude are derived values.

For cell normal `n` and normalized direction toward the star `s`:

``` text
mu = max(0, dot(n, s))
Qsolar = S(d) * mu
S(d) = Lstar / (4*pi*d^2)
```

Support orbital eccentricity from M1; a numerical Kepler-equation
solution is sufficient.

Acceptance: - zero night-side insolation; - correct day/night
progression; - equinox hemispheric symmetry; - solstice hemispheric
asymmetry; - inverse-square distance forcing; - annual global-mean
incoming solar near `S0/4` for a circular orbit; - stable orthogonal
local East/North/Up bases.

### M2 --- Geological planet, terrain and ocean basins

Selected design: **procedural geological history implemented
incrementally**.

Do not make final terrain simply `elevation = noise(position)`. Begin
with synthetic plate-scale structure while preserving geological state
that can later support crust age, sedimentary basins, volcanism and
resource formation.

Conceptual state:

``` cpp
enum class CrustType { Oceanic, Continental };

struct TectonicPlate {
    Vec3d rotation_pole;
    double angular_velocity_rad_s;
    CrustType dominant_crust;
    double base_elevation_m;
};

struct GeologyState {
    Field<PlateId> plate_id;
    Field<double> crust_age_s;
    Field<CrustType> crust_type;
    Field<double> elevation_m;

    // Added progressively:
    Field<double> sediment_depth_m;
    Field<double> volcanic_activity;
    Field<double> tectonic_stress;
};
```

Generation sequence:

``` text
seed
 -> spherical plate seeds / Voronoi regions
 -> plate rotation vectors
 -> relative motion at boundaries
 -> convergent / divergent / transform classification
 -> continental/oceanic crust
 -> mountains / ridges / trenches / rifts
 -> multiscale roughness
 -> erosion approximation
 -> drainage topology
 -> final elevation + bathymetry
```

For an Earth-sized default world, roughly 8--20 major plates is a useful
configurable starting range.

Plate surface velocity can be derived from:

``` text
v = omega x r
```

Distinguish: - continental convergence -\> mountain belts; -
oceanic/continental convergence -\> trench + uplifted margin; -
divergence -\> rifts / mid-ocean ridges; - transform motion -\>
fault-zone structure.

Geological evolution is primarily world generation; do not simulate
millions of years during ordinary gameplay.

#### Bathymetry

Preserve structures such as:

``` text
continental shelf
 -> continental slope
 -> abyssal basin
 -> mid-ocean ridge
 -> trench
```

#### Drainage preparation

Plan for:

``` cpp
Field<CellId> downstream;
Field<BasinId> basin_id;
Field<double> catchment_area_m2;
```

Depressions must be handled deliberately so they can later form lakes,
endorheic basins or spill into downstream drainage.

#### Future resource coupling

Preserve:

``` text
geological history
      +-> terrain -> climate -> ecosystems
      +-> crust/sediment/volcanism -> resources -> civilization
```

Do not implement the economy/resource system in M2.

Acceptance: - deterministic generation for a fixed seed; - coherent
plate-scale regions; - major terrain structures correlate with
boundaries; - configurable land/ocean fraction; - non-flat bathymetry; -
finite valid elevation; - valid drainage references; - generation
diagnostics exportable for inspection.

### M3 --- Surface energy and first thermal planet

M3 deliberately has no dynamic atmosphere so the surface/radiative model
can be validated independently.

``` text
C * dT/dt =
    absorbed shortwave
  - outgoing longwave
  + ground/ocean exchange
  + optional explicitly documented reduced horizontal transport
```

with:

``` text
Qabsorbed = (1 - albedo) * Qsolar
Qlongwave ~= epsilon * sigma * T^4
```

At minimum distinguish thermal properties for: - ocean; - rock; - dry
soil; - wet soil.

Physical constants/approximations must be documented rather than tuned
only for appearance.

Expected behavior: - land has stronger diurnal/seasonal variation than
ocean; - ocean thermal inertia damps rapid changes; - latitude and
season affect equilibrium temperature; - do not fake atmospheric
lapse-rate effects before the atmosphere exists.

Acceptance: - stable integration under documented timestep limits; -
per-cell/global energy accounting; - correct shortwave/longwave signs; -
predictable simplified equilibria; - land/ocean thermal-inertia A/B
tests.

### M4 --- Basic snow, ice and albedo feedback

Basic cryosphere physics moves before atmospheric dynamics.

Deliver: - freeze/melt rules; - snow/ice surface state; - latent heat; -
snow/ice albedo; - seasonal accumulation/melt hooks; - water-equivalent
accounting.

Until atmospheric moisture exists, controlled/test forcing may supply
snow.

Required loop:

``` text
cooling
 -> snow/ice increases
 -> albedo increases
 -> absorbed shortwave decreases
 -> further cooling
```

Acceptance: - latent-energy accounting; - correct albedo-feedback
sign; - water-equivalent conservation in controlled tests; - stable
seasonal snow/ice experiment.

### M5 --- Atmosphere and pressure

Deliver a 3--5-layer-capable atmosphere, hydrostatic initialization and
surface-atmosphere exchange. Do not permanently lock the model to one
layer.

### M6 --- Wind and Coriolis

Pressure-gradient response, tangent-space wind, Coriolis, friction and
stable transport.

### M7 --- Humidity and evaporation

Saturation, evaporation, humidity transport, latent surface cooling and
global water diagnostics.

### M8 --- Clouds and precipitation

Condensation, physical cloud water, latent atmospheric heating,
precipitation and simplified cloud-radiation coupling. Godot renders
authoritative cloud state.

### M9 --- Hydrology, rivers and lakes

Soil moisture, infiltration, runoff, catchments, river discharge,
depression/lake handling and M4 snowmelt input.

### M10 --- Vegetation

Climate suitability, vegetation fraction, evapotranspiration, albedo and
soil-water coupling.

### M11 --- Ocean heat and currents

Mixed-layer SST, deep thermal reservoir, wind-driven currents,
horizontal heat transport and air-sea heat/moisture exchange.

Coastal climate must respond through:

``` text
current -> SST -> air-sea flux -> atmosphere -> land
```

Never use direct current-to-land-temperature modifiers.

The API must permit later dynamic salinity and
temperature/salinity-dependent density.

### M12 --- Carbon and CO2 forcing

Configurable CO2, greenhouse forcing, diagnostics and architecture for
later carbon reservoirs.

### M13 --- Coupled climate experiments

Include no-atmosphere, doubled-CO2, zero-rotation, high-tilt,
eccentric-orbit, water-world, frozen-world, altered-ocean-transport,
deforestation and long-equilibrium runs.

### M14 --- Visualization

Render authoritative temperature, wind, currents, precipitation, soil
moisture, vegetation, snow/ice, clouds, SST and energy imbalance.

### M15 --- Coupled tropical-storm experiment

Test warm-ocean/low-shear, high-shear, equatorial-low-Coriolis,
cooler-ocean and mixed-layer-depth cases.

If the reduced model cannot produce useful storm dynamics robustly,
document that limitation and prefer a physically constrained mesoscale
parameterization over arbitrary random storm spawning.

## 13.1 Mandatory early reference experiments

### Experiment A --- Dead rock

``` text
atmosphere = none
ocean = none
rotation = Earth-like
axial tilt = 0
```

Validate day/night thermal response and energy accounting.

### Experiment B --- Aqua planet

``` text
surface = 100% ocean
rotation = Earth-like
axial tilt = Earth-like
```

Validate the strong thermal-inertia contrast with the rock world before
currents exist.

### Experiment C --- Geological Earth-like world

``` text
continents + oceans + mountains
basic snow/ice
Earth-like orbit
```

Validate physically interpretable latitude, season, surface-class and
cryosphere temperature patterns before full weather dynamics.

## 13.2 M2 architecture decision

The chosen direction is **procedural geological history, implemented
incrementally**.

M2 begins with plate-scale synthetic geology while retaining
`GeologyState` for later crust age, sediment depth, volcanism, tectonic
history and resource-forming context.

Do not turn M2 into a full geological-timescale simulator before climate
development can proceed.

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

## 21. First development task

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
