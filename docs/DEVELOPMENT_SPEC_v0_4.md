# Planetary Civilization Simulator --- Development Specification

Version: 0.4 (reconciled with design v0.9 and accepted ADRs 0001--0005 in
`docs/decisions/`)\
Purpose: implementation contract for Codex / Claude Code\
Primary target: PC/Linux, C++20 + Godot 4\
Current phase: P0 --- Living Planet (M0 and M1 complete; M2 in progress)

Design document: `docs/planetary_civilization_simulator_design_v0_9.docx`.
Accepted decision records take precedence over this specification where they
conflict.

Decision records live in one directory, `docs/decisions/`, with an index in
`docs/decisions/README.md`; superseded records are kept under
`docs/decisions/archive/`.

What is new in v0.4, relative to v0.3 --- all of it from design v0.9
sections 36 and 37, which v0.3 predates:

- **fast local loops** (design v0.9 §36): every civilization action must have a
  local, visible, largely reversible consequence, and that speed must come
  from short physical residence times, never from a gameplay multiplier
  (section 9.14). A per-region consequence ledger records them (section 15.1);
- **pacing is presentation, not simulation** (design v0.9 §36.5): the report
  interval shortens and the simulation slows near events, but the tick
  sequence may not depend on either (section 8);
- **knowledge gates the climate view** (design v0.9 §37.1): six knowledge stages,
  an instrument-plus-knowledge unlock rule, and knowledge that may lag
  measurement (section 17, rewritten);
- **the projection of the committed future** (design v0.9 §37.2): a forward
  ensemble run from the civilization's *estimated* state, whose spread is the
  uncertainty cone the player sees (section 7.2). This is a third instance
  family and it changes the ADR-0001 compute budget (section 7.3);
- **tipping points staged as events** (design v0.9 §37.3): thresholds stay
  emergent and seed-varied; the game layer only detects and stages. Early
  warnings are real critical-slowing-down statistics (sections 13.3, 15);
- **adaptation as a first-class path** (design v0.9 §37.4): what it may and may not
  touch in the physical interface (section 18);
- behavioural acceptance experiments for all of the above (section 23.1);
- a traceability map from design v0.9 to this specification (section 28).

Four items require decision records before the code lands; they are listed in
section 26.

What v0.3 added, relative to v0.2, and which still stands:

- the couplings the design added in v0.5--v0.7: people acting on weather
  (irrigation, aerosols, land-use albedo, short-lived gases, dust, reservoirs),
  weather acting on people, and the living biosphere (sections 9.10--9.13);
- validation acceptance targets and the calibration harness (sections 23--24);
- the counterfactual "shadow planet" as an engine capability (section 7.1);
- staged tropical cyclones (section 10);
- current implementation status and the outstanding migration tasks
  (section 26).

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

Determinism, identity and time (ADR-0003):

-   Determinism is scoped: **L0** --- same build, platform, seed and command
    list gives bit-identical state for any thread count, frame rate, pause
    pattern or weather-window schedule; **L1** --- across builds, only
    statistical equivalence within the tolerances of section 23; **L2** ---
    cross-platform bit-equality is explicitly out of scope.
-   Simulated time is an integer tick count (`SimulationTick`, 60 s per tick).
    No accumulated `double` seconds anywhere in the core; SI seconds are
    derived on demand.
-   Every persisted or snapshotted field has a stable numeric `FieldId` in the
    field registry. Ids are append-only: never renumbered, never reused with
    different meaning or units. A CI check diffs the registry against the
    previous release.
-   Randomness is counter-based and keyed by `(world_seed, stream_id, tick,
    cell, sample_index)`. No global generator, no shared mutable RNG state.
-   Cells are partitioned into fixed blocks at mesh construction, independent
    of thread count. Reductions use per-block partials combined in block order;
    atomics are forbidden in reductions.
-   Floating point: `-ffp-contract=off` (MSVC `/fp:strict`), no fast-math.
    FMA or reassociation is opt-in per kernel and only where a test shows the
    result is unchanged.

## 5. Planet discretization

Use an icosphere.

P0 resolution policy:

-   development resolution L5 with 10,242 dual surface cells;
-   shipped/reference resolution L6 with 40,962 dual surface cells;
-   atmosphere initially designed for 3--5 vertical layers;
-   lower resolutions must be supported for tests and debugging.

The accepted simulation mesh is the hexagonal--pentagonal dual of the
subdivided icosahedron. Its cells are centred on primal vertices; exactly
twelve cells are pentagons and the remainder are hexagons. Corners are the
primal triangles' circumcentres and the cell centres are optimised into a
spherical centroidal Voronoi tessellation, which the finite-volume operators
require (ADR-0002 §4.1, §9).

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
`docs/decisions/0002-mesh-and-field-layout.md`.

Geometry/connectivity must be immutable after initialization and
separate from evolving state.

Required mesh acceptance tests (they are what prevent a silent regression to
primal triangles or a broken dual):

-   cell counts: 2,562 at L4, 10,242 at L5, 40,962 at L6;
-   exactly twelve pentagons at every level;
-   spherical area closure within 1e-12 relative (record the measured value
    per level; the primal implementation reached 5e-14);
-   every edge shared by exactly two cells, neighbour relation symmetric;
-   operator error maps on analytic fields show no icosahedral structure and
    no pentagon signature in the gated measures (divergence and discrete
    Poisson solutions). The two-point Laplacian's pointwise truncation error
    and, more weakly, the gradient do show a pentagon-ring and seam signature
    that is measured and bounded rather than removed (ADR-0002 §9).

Measured spherical area closure on the centroidal Voronoi mesh is about 1e-16
relative at L5 and L6 (ADR-0002 §9); the test tolerance is 5e-14.

Coastlines are a **fractional land area per cell**, derived from sub-cell
hypsometry with ocean connectivity, and runoff is routed on the cell mesh with
one downstream neighbour per land cell (accepted ADR-0005).

## 6. Core data model

Initial conceptual types:

``` cpp
PlanetMesh
Field2D<T>
Field3D<T>
EdgeField<T>
FieldRegistry / FieldId / FieldDescriptor
SimulationTick / SimulationClock
SimulationMode
Scheduler / deterministic block executor
CounterRng (keyed, stateless)
PlanetParameters
PlanetState
ForcingState
Diagnostics
StateSnapshot
```

State partition (ADR-0001 §4.1). Every registered field belongs to exactly one
partition. `FieldPartition` is the sole classification; the former
`FieldKind` vocabulary was removed during M2-01b so classification cannot
conflict with persistence and ownership:

``` text
SlowState     authoritative, snapshotted, always integrated
              surface/soil, ocean, ice, vegetation, carbon, composition
FastState     weather; allocated only in reference mode or a weather window;
              never required to reconstruct SlowState
Climatology   derived statistics (monthly means, variances, event rates);
              regenerated from SlowState and fitted coefficients
```

Civilization-side state is **not** a fourth partition. Knowledge, estimates,
projections and the consequence ledger are consumers of `SlowState` and
producers of presentation, never producers of planetary state. This keeps the
forbidden dependency of section 2 (`Climate -X-> Population entities`) intact
while satisfying design v0.9 sections 37.1 and 37.2:

``` text
ObservationState    per-instrument coverage, precision, start date
KnowledgeState      which of the six stages of section 17.1 are unlocked
EstimatedState      the civilization's reconstruction of SlowState, with a
                    per-field uncertainty; the seed for every projection run
ProjectionEnsemble  N forward runs from EstimatedState + committed-warming run
ConsequenceLedger   per-region fast-loop changes and their attributed causes
```

`EstimatedState` has the same field identities as `SlowState` so that a
projection run is an ordinary simulation with a different initial condition,
not a second physics. It is **never** written back into `SlowState`.

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

## 7.1 Counterfactual planet (shadow instance)

The engine must support running a second `PlanetSim` instance initialised from
the same state, receiving the same external forcing but **not** this
civilization's fluxes, and exposing the difference as a first-class result
(design v0.9 section 29).

``` text
PlanetSim (real)      PlanetSim (shadow: civilization fluxes zeroed)
       |                              |
       +---------- difference --------+
                     |
         local and global attribution
```

Requirements:

-   no new architecture: it is a history fork with one input set to zero, so
    it reuses the snapshot and fork machinery;
-   both instances run at the **same resolution** --- a coarser shadow would
    inject resolution error into the number the player is asked to trust;
-   the ADR-0001 performance budget therefore applies to half the available
    compute;
-   the difference must be exposed per cell and globally, for temperature,
    precipitation and any field the observation layer can estimate.

## 7.2 Projection of the committed future (forward ensemble)

Design v0.9 section 37.2 makes the projection a science unlock rather than a
gift to the player, which turns it into an engine requirement: the projection
must be **computed**, and computed from what the civilization actually knows.

``` text
SlowState (true)
   -> observation layer (section 17)
   -> EstimatedState + per-field uncertainty
   -> N perturbed initial conditions
   -> N forward runs, climate mode, current-policy forcing
   -> ensemble mean = the projected future
      ensemble spread = the cone the player sees
```

Requirements:

-   **seeded from the estimate, never from the truth.** A projection run that
    starts from `SlowState` would be an oracle, and the whole mechanic
    collapses. The only legitimate sources of projection error are observation
    gaps and model maturity;
-   **it is a history fork**, like section 7.1: same snapshot and fork
    machinery, no new architecture;
-   **it may run coarser than the main instance.** Unlike the shadow planet,
    the projection is explicitly a model *of* the world made *by* the
    civilization, so resolution error is in character. State the running level
    in the run manifest;
-   **a separate committed-warming run**: the same ensemble with all
    civilization fluxes set to zero from now on, which is what "already locked
    in" means. Note that this is the shadow planet's machinery pointed
    forwards instead of backwards;
-   **model maturity is an explicit parameter**, not an accident of
    resolution: later knowledge stages reduce a structural bias term and the
    perturbation amplitude. It must be possible to show that projection skill
    improves stage by stage (section 23.1);
-   **off the main thread, once per report interval**, and never blocking the
    main instance. If the ensemble is not finished, the player keeps the
    previous one; the simulation does not wait;
-   **deterministic**: the perturbations come from the keyed RNG of ADR-0003
    with the run seed and the tick, so a replay produces the same cone.

Presentation consumes ensemble quantiles, not individual members: a ghost
coastline and a future ice edge on the planet, a temperature band past the
current year, and committed warming shown as its own quantity.

## 7.3 Instance families and the compute budget

There are now three families of PlanetSim instance, and ADR-0001's budget was
written for one:

| Family | Count | Resolution | Cadence | Purpose |
|---|---|---|---|---|
| Authoritative | 1 | full | continuous | the world |
| Counterfactual (7.1) | 1 | full, mandatory | continuous | attribution |
| Projection (7.2) | N + 1 | may be coarser | once per report interval, forward | the cone and committed warming |

The authoritative and counterfactual instances already split the ADR-0001
budget in half. The projection ensemble is additional, bursty, and runs
forward over decades rather than in step with the world, so it cannot be
costed the same way. **ADR-0001 must be amended before the ensemble is
implemented** (section 26.1): it needs an ensemble size, a running level, a
wall-clock ceiling per report interval, and a stated policy for what happens
when the ceiling is hit --- reduce N, drop a level, or extend the interval,
but never stall the world.

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

Scheduling rules that follow from determinism:

-   the step sequence is a function of state and ticks only --- never of wall
    clock, frame rate or thread count;
-   snapshot cadence is independent of solver cadence; Godot interpolates;
-   simulation runs on worker threads and the render thread never blocks on
    it;
-   while a weather window owns a region, climate mode does not also apply its
    statistical fluxes there; the difference is recorded as a diagnostic;
-   **pacing is presentation, not simulation** (design v0.9 section 36.5). The
    report interval may shorten from a decade to a year as the slow loop
    becomes active, and the presentation may slow or pause on a major event,
    but neither may change the step sequence, the timestep or the tick count.
    A replay must produce identical state whatever the player's pacing was;
-   **an automatic mode change is a function of simulation state**, never of
    wall clock, frame rate or whether anyone is watching. If approaching a
    threshold opens a weather window or raises the reporting rate, the
    predicate is evaluated on state at a fixed tick cadence and the switch is
    recorded in the run manifest (ADR-0003). This is the one place where
    design v0.9 section 36.5 could quietly break determinism, and it is
    precisely why the predicate must be state-only;
-   the projection ensemble (section 7.2) is scheduled as low-priority
    background work: it may be pre-empted, restarted or skipped for an
    interval without affecting the authoritative tick sequence.

**Seasonal resolution.** Design v0.9 keeps seasons as a P0 completion item and
sections 37.1--37.3 raise the stakes: fire seasons, monsoons, growing seasons,
sea-ice seasonality and the early-warning statistics of section 13.3 are all
properties of the seasonal cycle, not of the annual mean. The position of this
specification is that **climate mode resolves the seasonal cycle explicitly**,
at twelve sub-steps per year, for three reasons:

1.  *the targets are seasonal.* Ice, permafrost, fire and monsoon behaviour
    are driven by seasonal extremes; an annual mean cannot produce them;
2.  *annual-mean stepping is biased, not merely coarse.* Outgoing longwave
    goes as T\^4, saturation vapour pressure is exponential in temperature,
    and ice is a threshold process. Stepping the annual mean therefore does
    not give the annual mean of the stepped seasons; the error has a sign;
3.  *it is affordable.* Twelve climate-mode sub-steps per year still sits
    inside the ADR-0001 budget, and it is far cheaper than the alternative of
    fitting seasonal statistics that would themselves need calibrating
    against reference mode.

This needs a decision record before M3 (section 26.1), because it fixes the
climate-mode step and therefore the fit targets of section 24.

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

Aerosols are a P1 priority rather than a curiosity: they mask warming, dim
solar output and weaken monsoons, so cleaning the air improves health and
solar yield **and** accelerates warming. That trade-off is one of the
strongest available to the civilization layer.

### 9.10 Land use, albedo and water withdrawal

Clearing land must do more than reduce evapotranspiration:

``` text
land use change
 -> surface albedo        (crops/bare soil/cities are usually brighter
                           than forest; snow over cleared land brighter still)
 -> evapotranspiration    (less moisture returned to the air)
 -> roughness/drag        (later)
 -> local heat            (urban surfaces, industry, waste heat)
```

In high latitudes the albedo term can cool a region while its carbon release
warms the planet. That opposition must be representable; do not collapse land
use into a single "drying" parameter.

Irrigation and water withdrawal are the counterweight:

``` text
irrigation -> evaporation up -> local cooling and humidity up
           -> river/groundwater/reservoir stocks down
```

Water must therefore exist as **stocks** (soil, snowpack, rivers, lakes,
groundwater, reservoirs), not only as a precipitation flux.

### 9.11 Short-lived climate forcers

Keep separate from CO2, with their own lifetimes:

-   methane and other short-lived gases (livestock, rice, landfill, leaks):
    potent, short-lived, so mitigation shows results within a player's term;
-   dust from degraded land: dims sunlight, and deposits on snow;
-   black carbon on snow and ice: lowers albedo, accelerates melt.

### 9.12 Biosphere carbon, fire and dieback

Vegetation is not only an evapotranspiration coefficient. Required state and
behaviour:

``` text
forest biomass carbon   slow to accumulate (decades), fast to release (one season)
soil carbon             larger than the atmospheric stock; respires faster when warm;
                        depleted by tillage, rebuilt by cover and rotation
peat / permafrost carbon  preserved by cold or water; drainage or thaw releases it
                          irreversibly on gameplay timescales
```

Required processes:

-   **fire regime**: ignition probability from fuel, dryness and temperature;
    carbon released; vegetation and soil state reset;
-   **dieback**: a forest recycles part of its own rainfall, so clearing,
    heat and drought past a threshold dry the remainder and convert it to
    savanna. This is the most legible tipping point available to the player
    because it occurs inside their own territory within a few turns;
-   **CO2 fertilisation**: a damping feedback, limited by nutrients and heat;
-   regrowth with realistic asymmetry: clearing is near-irreversible on the
    scale of a player's term.

### 9.13 Wildlife, pollination and the sea

``` text
habitat fraction / monoculture share / chemicals
   -> pollinator abundance -> crop yield multiplier
temperature and moisture
   -> pest and disease range and generations per year -> crop and forest loss
warming / stratification / acidification
   -> plankton -> biological carbon pump -> ocean uptake
   -> fisheries distribution and collapse
   -> coral bleaching -> fisheries and coastal storm protection
grazing pressure -> grassland degradation -> albedo and dust
```

Pollinators and pests are P1: they are cheap to model, immediately legible,
and they are the first mechanic that **rewards** protecting something rather
than limiting damage. The marine terms are P2, but the carbon and fisheries
interfaces should exist so they can be filled without a redesign.

### 9.14 Fast local loops and residence times

Design v0.9 section 36 requires that **every major civilization action have at
least one consequence that is local, visible on the rendered planet within one
or two report intervals, and largely recoverable if the cause stops.** This is
a constraint on the physics, not a feature to be added on top of it: the
speed must come from genuinely short residence times in the state that already
exists, so the architectural work is mostly to make sure those stocks are not
averaged away.

| Fast loop | Existing coupling | State that must be short-lived | Recovery |
|---|---|---|---|
| Haze and air quality | 9.9, 9.11 | aerosol burden, wet-deposition sink | days to a season after emissions stop |
| Downwind drying | 9.7, 9.12 | recycled-rainfall term, soil moisture | years to decades; past the threshold, dieback |
| Soil exhaustion | 9.12 | soil carbon and nutrient stock under tillage | years to decades with cover and rotation |
| Water drawdown | 9.10 | river, lake, reservoir, groundwater stocks | rivers fast, groundwater slow |
| Urban heat | 9.10 | sealed-surface fraction, waste heat, local evaporation | persists with the city; eased by green space and water |
| Habitat and yield | 9.13 | pollinator abundance, fish stock, pest pressure | years, if habitat and stocks return |

Binding rules:

-   **no gameplay multiplier.** There is no `deforestation_penalty` and no
    `haze_malus`. If a loop is not fast enough, the residence time or the
    stock is wrong, and that is what gets fixed;
-   **visible before it is measured** (design v0.9 §36.4). Every fast loop must have
    a rendered signature that needs no instrument: haze, bare and eroded
    ground, a shrinking river, browning fields. This is a requirement on the
    field set --- if a loop has no renderable field, it is not done;
-   **the asymmetry is the lesson.** Fast loops recover; the slow loop does
    not. Do not soften either side to make the game feel fairer;
-   **the bridges are the same fluxes at two scales**, not two models. Coal
    emits aerosols *and* CO2 from one combustion flux; clearing releases a
    carbon stock *and* changes albedo and evapotranspiration from one land-use
    change. Unmasking --- cleaning the air and revealing the warming the
    aerosols were hiding --- must fall out of this, and it is one of the
    clearest tests that the bridge is real rather than two parallel tables.

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

Staged implementation (design v0.9 section 34). Emergent cyclogenesis must not
block P0 or P1; the consumer interface is identical at every stage:

``` text
S1  genesis probability from resolved conditions (upper-ocean heat, humidity,
    rotation, shear proxy); storms are tracked objects steered by resolved winds
    -> real: where and when they form, tracks, landfall, rainfall, damage
S2  S1 plus two-way coupling: storms mix the upper ocean, extract heat,
    modify local humidity and rainfall
    -> real: self-limitation and track-dependent ocean cooling
S3  organized convection emerges from the atmospheric solver  (research)
```

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
12. urban surfaces/waste heat -\> local climate later;
13. land-use albedo -\> absorbed shortwave -\> local temperature (may oppose
    the carbon effect at high latitudes);
14. irrigation -\> evaporation -\> local cooling/humidity -\> water stocks;
15. forest -\> recycled rainfall -\> forest (dieback when broken);
16. warming -\> fire -\> biomass and soil carbon -\> forcing;
17. soil carbon \<-\> tillage/cover and soil temperature;
18. habitat -\> pollinators -\> crop yield;
19. warming -\> pests and disease -\> crop and forest loss;
20. warming -\> cooling demand -\> energy use -\> emissions;
21. meltwater/freshwater -\> overturning circulation -\> regional cooling
    while the planet warms (bistable; does not recover when forcing is
    removed).

### 11.1 Civilization-side loops (P1 and later)

These are loops through people rather than through the atmosphere, so they are
kept out of the physical list above; they belong to the game layer and must
not be implemented as modifiers on planetary state. From design v0.9 sections
36.3 and 37.4:

22. aerosol cleanup -> loss of masking -> faster apparent warming (physical in
    origin, but felt as a policy trap);
23. sea walls -> perceived safety -> development behind them -> greater
    exposure when the design height is exceeded (maladaptation);
24. irrigation -> hidden regional warming -> continued expansion -> water
    stocks exhausted -> both revealed at once (maladaptation);
25. investment in science -> earlier warning -> earlier and cheaper action
    (the loop the game exists to teach);
26. warming -> adaptation spending -> less capacity for mitigation -> more
    warming (the losing race of design v0.9 §37.4).

Loops 23, 24 and 26 are the ones that make adaptation a genuine strategy
rather than a free repair, and 24 must emerge from the water stocks of
section 9.10 rather than from a scripted reveal.

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

This numbering differs from the Design Record v0.4 §25 table: snow/ice
moves from M9 to M4, and atmosphere through hydrology shift from M4--M8 to
M5--M9. M0--M3 and M10--M15 are unchanged. Milestone references in
`docs/decisions/` use this specification's numbering.

### M0 --- Icosphere and simulation skeleton

Completed. The implementation contract is sections 21--22. The mesh is the
hexagonal--pentagonal dual defined by ADR-0002, which superseded the
original primal-triangle M0 representation.

### M1 --- Orbit, Sun, rotation, day/night and seasons

Completed. Coordinate, orbit and validation conventions are defined in
`docs/M1_TECHNICAL_SPEC.md` and ADR-0004.

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

Under ADR-0005 the authoritative surface elevation is the sub-cell
hypsometry (nine quantiles per cell) plus a global sea level, both in the
slow state; a cell's mean elevation is derived from the hypsometry, so
`GeologyState` below does not store a separate authoritative `elevation_m`.
Land and ocean fractions and the drainage fields are derived.

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
    Field2D<PlateId> plate_id;
    Field2D<float> crust_age_s;
    Field2D<CrustType> crust_type;

    // Added progressively:
    Field2D<float> sediment_depth_m;
    Field2D<float> volcanic_activity;
    Field2D<float> tectonic_stress;
};
```

Fields use the `Field2D<T>` layout and `float` default precision of
ADR-0002.

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
Field2D<CellId> downstream;
Field2D<BasinId> basin_id;
Field2D<float> catchment_area_m2;
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

### M10 --- Vegetation and the living biosphere

Climate suitability, vegetation fraction, evapotranspiration, albedo and
soil-water coupling.

Extended by design v0.9 section 24.12, in this order:

-   forest biomass carbon as a stock with slow regrowth and fast release;
-   fire regime driven by fuel, dryness and temperature;
-   rainfall recycling and the dieback threshold;
-   soil carbon responding to practice and temperature;
-   pollinator abundance from habitat and monoculture share, as a yield
    multiplier;
-   pest and disease pressure from temperature and moisture.

Acceptance: vegetation and soil carbon budgets close; a controlled clearing
experiment shows reduced downwind precipitation; a dieback experiment shows a
threshold rather than a linear response; a habitat-loss experiment shows a
yield penalty through pollination alone.

Design v0.9 section 37.3 adds one requirement here: forest dieback is the
**first** tipping element the player will meet, so it is also the first place
the early-warning statistics of section 13.3 must be computed and shown to
work. Prove them on dieback before wiring them to the global elements.

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

Reservoirs to be wired as they become available: atmosphere, vegetation,
soil, peat/permafrost, ocean (with the biological pump as a later term).
Short-lived forcers (section 9.11) keep their own lifetimes and are never
folded into the CO2 concentration.

### M13 --- Coupled climate experiments

Include no-atmosphere, doubled-CO2, zero-rotation, high-tilt,
eccentric-orbit, water-world, frozen-world, altered-ocean-transport,
deforestation and long-equilibrium runs.

Design v0.9 sections 37.3 and 37.6 add three more, all of which are cheap once
the hysteresis machinery exists and all of which are gameplay-critical:

-   **sustained crossing**: hold the forcing past a threshold and confirm the
    element does not return when the forcing is removed;
-   **brief overshoot**: cross the same threshold and reverse quickly; the
    element recovers. This is already row 16 of section 23 and it is what
    makes reacting to a warning worth doing;
-   **early-warning skill**: from an ensemble of approaches to a threshold,
    confirm that the statistics of section 13.3 rise before the crossing more
    often than chance, and record the false-alarm rate. A warning system whose
    false-alarm rate is unknown is not a warning system.

### M14 --- Visualization

Render authoritative temperature, wind, currents, precipitation, soil
moisture, vegetation, snow/ice, clouds, SST and energy imbalance.

### M14.1 --- Counterfactual planet and attribution

Run the shadow instance of section 7.1 alongside the main one; expose local
and global differences; verify that with zero civilization fluxes the two
instances stay bit-identical (an L0 determinism check in disguise).

### M14.2 --- Projection ensemble and the committed future

Run the forward ensemble of section 7.2 from an `EstimatedState`, expose
quantiles rather than members, and render the ghost coastline, the future ice
edge and the temperature band. Include the separate committed-warming run.

Acceptance: with a perfect estimate and zero perturbation the ensemble mean
reproduces a single long run of the same forcing to within the tolerance of
section 24; with a degraded estimate the spread widens and still contains the
truth at the stated rate (section 23.1); a replay reproduces the same cone.

This milestone depends on the ADR-0001 amendment of section 7.3.

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

## 13.3 Tipping elements: detection and staging

Design v0.9 section 37.3 turns the tipping elements already in the physics
into the game's dramatic moments. The division of responsibility is strict:

``` text
PlanetSim            crosses a threshold because the physics took it there
Diagnostics          detect the crossing and the approach to it
Game layer           stage the event: slow, look, report
```

The game layer never decides *that* a tipping point occurs, only *how it is
presented*. In particular there is no event table, no scheduled disaster and
no threshold that the presentation can move.

Elements, mapped to the sections that already own them:

| Element | Owner | Early-warning signal | Reversibility |
|---|---|---|---|
| Forest dieback | 9.12, M10 | falling recycled rainfall, rising fire frequency | not within a game |
| Permafrost and peat | 9.12, M12 | ground temperature, methane in the record | not within a game |
| Overturning circulation | 9.5, M11 | slowing flow, reduced northward heat | hysteresis: does not recover |
| Ice sheet loss | 9.6, M4 | accelerating mass loss | millennia |
| Reef and fishery collapse | 9.13 | bleaching, falling catch | decades at best |

Requirements:

-   **per-run thresholds.** The exact threshold varies between games within
    its physical range so that it cannot be memorised, and it is drawn from
    the run seed with the keyed RNG of ADR-0003 and recorded in the run
    manifest. It is drawn once at scenario construction and never re-drawn;
-   **early warnings are statistics, not flags.** Critical slowing down is
    real: near a threshold a system recovers more slowly from disturbance and
    its variance rises. Compute lag-1 autocorrelation and variance of the
    relevant slow variable over a rolling window (section 15) --- do not
    expose a hidden "distance to threshold" number in disguise;
-   **a warning is only available if the civilization observes it.** The
    signal is computed from `EstimatedState`, so an unobserved system gives no
    warning at all and the first sign is the event itself (design v0.9 §37.1);
-   **the aftermath is state, not a flag.** A crossed element leaves the
    planet in a different physical state; nothing should need to remember that
    an event "happened".

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

From design v0.9, add as the systems they measure arrive:

``` text
committed warming (zero-emission forward run)
projection ensemble mean and spread, per lead time
projection skill against the realised outcome, by knowledge stage
per-element distance to threshold (true; debug only, never player-facing)
lag-1 autocorrelation and variance of each tipping element's slow variable
early-warning true-positive and false-alarm rate
aerosol masking: forcing with and without the aerosol burden
fast-loop residence times, measured rather than configured
```

### 15.1 Consequence ledger

Design v0.9 section 36.6 requires a per-region record of fast-loop changes and
the causes attributed to them. It feeds both the newspaper report and the
attribution of section 7.1, and it is the artefact that lets a playtest show
whether causality was *perceived*, not merely simulated.

-   one entry per region per report interval: what changed, by how much, and
    the civilization flux the shadow-planet difference attributes it to;
-   attribution comes from the difference between the real and counterfactual
    instances, never from bookkeeping the action that was taken;
-   it is derived, so it lives in the sidecar analytics file rather than the
    snapshot (section 27), and losing it must not affect the simulation.

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

Design v0.9 section 37.1 adds two requirements to scenario data:

-   **a starting knowledge stage.** `industrial_dawn` starts at *local
    records*, with no climate view at all. Other scenarios may start at any
    stage, including a post-collapse world that has lost stages it once had.
    The historical anchors of section 17.1 pace `industrial_dawn` only and
    must not be baked into the knowledge model;
-   **tipping thresholds drawn from the scenario seed** within their physical
    ranges (section 13.3), recorded in the run manifest so that a scenario is
    reproducible and a replay is exact.

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

### 17.1 Knowledge stages

Design v0.9 section 37.1 makes the climate view itself something the player
earns. The player does not begin with a climate interface: at the start of
`industrial_dawn` they see the rendered planet, the fast local effects of
section 9.14, and the sparse records of their own region. Each stage of
knowledge adds a piece of the view.

| Stage | Requires | What it unlocks | Historical anchor |
|---|---|---|---|
| Local records | thermometers, rain gauges, archives | own-region history, with gaps; nothing global | start of Industrial Dawn |
| Climatology | station networks, balloons, statistics | regional norms and anomalies; a first, very uncertain global mean | c. 1900--1930 |
| Greenhouse physics | chemistry, precision instruments, a continuous CO2 record | CO2 gauge; emissions can be linked to warming | c. 1938--1960 |
| Numerical models | electronics, computing, atmospheric science | weather forecasts; the first projection, as a wide cone (7.2) | c. 1950--1975 |
| Satellites and ocean observation | space programme, buoys, ocean ships | ice, sea level, ocean heat; narrower cone; early warnings (13.3) | c. 1975--2000 |
| Earth-system models | supercomputing, attribution science | ensemble projections, event attribution (7.1), a map of tipping zones | c. 2000 onwards |

### 17.2 The unlock rule

``` text
instrument exists        -> measurements accumulate (with error and coverage)
knowledge stage unlocked -> those measurements become a view
```

Both are required. An instrument without the matching knowledge produces data
nobody can interpret; knowledge without the instrument produces a theory with
nothing to apply it to. This is what lets a civilization record warming for
decades before its science can explain it, which design v0.9 section 37.1
asks for explicitly.

Architectural consequences:

-   **science reveals, it never changes the physics.** There must be no path
    by which `KnowledgeState` reaches a solver. The planet warms at exactly
    the same rate whether or not anyone can see it, and a test should assert
    this: two runs identical but for the knowledge state must be
    bit-identical in `SlowState`;
-   **the gate is on the view, not on the field.** Fields are always
    simulated; the presentation layer decides what may be shown. Gating at the
    solver would couple physics to the game layer and break section 2;
-   **uncertainty is produced at the sensor**, not applied as a blur at the
    UI. Coverage, precision and start date belong to `ObservationState`, and
    `EstimatedState` is reconstructed from them. The blur the player sees is
    then a consequence of a real gap;
-   **missing views are a designed state, not a broken interface.** Onboarding
    must present the absent climate view as the intended starting point
    (design v0.9 §37.1);
-   **P0 exemption.** `climate_lab` bypasses all of this. It must be a
    separate presentation path, so that removing it cannot leave a hole in the
    gameplay UI --- and so that the gameplay UI cannot quietly come to depend
    on it.

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

### 18.1 Adaptation

Design v0.9 section 37.4 makes adaptation a full strategy. The architectural
question it raises is what adaptation is allowed to touch, and the answer is
that it changes **exposure and the local boundary condition**, never the
planet's forcing and never a damage number.

| Measure | What it may change | Phase |
|---|---|---|
| Sea walls and dikes | the local flood boundary; inundation extent for a given sea level and surge | P2 |
| Managed retreat | where population and infrastructure are | P2 |
| New crops and seeds | the crop's tolerance curve, after a transition period | P1 |
| Irrigation and water storage | the water budget of section 9.10 --- a real physical change | P1 |
| Heat-resilient cities | the urban heat term of section 9.10, and cooling energy demand | P2 |
| Forecasts and early warning | nothing physical; casualties only. Requires the numerical models stage | P2 |
| Ecosystem restoration | vegetation, roughness, soil carbon --- so it mitigates as well | P3 |

Rules:

-   **no damage multipliers.** A sea wall changes where the water reaches; it
    does not reduce a flood's damage by a percentage. If a measure cannot be
    expressed as a change to state or to a boundary, it does not belong in
    PlanetSim and should live in the game layer as exposure;
-   **maladaptation must be able to emerge.** Development gathering behind a
    wall, and irrigation masking regional warming until the groundwater is
    gone, both follow from the rule above plus the water stocks of section
    9.10. Neither may be scripted (section 11.1, loops 23 and 24);
-   **limits are physical.** A wall is overtopped past its design height; a
    crop fails past its tolerance; cooling fails past the limits of humid
    heat. Failure past a limit should be abrupt, because that is what it is;
-   **adaptation needs knowledge.** Where to build and what to plant depend on
    observations and, later, on the projection (sections 17, 7.2). Adaptation
    without science is guesswork, and the game should let it be;
-   **success is measured by living conditions**, not by temperature alone, so
    a well-adapted civilization in a warmer world can still succeed. This is a
    scoring requirement on the game layer, recorded here because it is what
    stops adaptation from being a dead end.

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

From design v0.9, these are real requirements but not P0 ones. They are listed
so that P0 does not drift into them, and so that nothing in P0 forecloses
them:

-   the knowledge-stage unlock tree and its research costs (section 17.1);
-   the projection ensemble as a player-facing feature (M14.2). The forward
    fork machinery it needs is P0; the cone is not;
-   adaptation measures (section 18.1), except irrigation and water
    withdrawal, which are already P0 physics;
-   staging of tipping events --- camera, pause, newspaper (section 13.3).
    The detection statistics are P0 diagnostics; the staging is not;
-   scoring and endings based on living conditions (section 18.1).

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

## 23. Validation acceptance targets

Conservation diagnostics are necessary but not sufficient: a model can conserve
energy perfectly and behave nothing like a planet. These targets come from the
validated prototype (design v0.9 section 30) and each one caught a real defect
during its calibration. They are the definition of "plausible enough" and
belong in `tests/physics` as a runnable harness.

| Property | Acceptance range |
|---|---|
| Preindustrial global mean surface temperature | 13--15 °C |
| Equilibrium warming for doubled CO2 | 2.5--4 °C |
| ... across the cloud-feedback uncertainty setting | spans roughly 2--5 °C |
| Transient warming at doubling / equilibrium warming | 0.5--0.75 |
| Polar amplification (poles vs tropics) | ×2--4 |
| Global precipitation response | +2 to +3 %/K |
| Tropical circulation response | slows, −1 to −2 %/K |
| Subtropical dry belts | move poleward with warming |
| Tropical rain belt vs subtropics | wet vs dry by a factor of several |
| Rain shadows behind terrain | windward clearly wetter |
| Western boundary currents | poleward and fast on western basin edges |
| SST, western vs eastern basin edge at equal latitude | western warmer |
| Overturning under doubled CO2 | weakens 15--40 %, no collapse |
| Overturning under quadrupled CO2 | collapse possible |
| After collapse, CO2 returned to preindustrial | stays collapsed (hysteresis) |
| Brief overshoot of the threshold, quickly reversed | may recover |
| Historical forcing 1850 → 2025 | ≈ +1.2 °C |

Two calibration lessons are binding, not advisory:

1.  **Calibration is cross-coupled.** Adding explicit ocean heat transport to
    an atmosphere whose diffusion was tuned to carry *all* poleward transport
    double-counts it: in the prototype this warmed the planet by 3 °C and
    destroyed polar amplification. Any new transport process must be
    accompanied by recalibration of the ones it partially replaces.
2.  **A plausible calibration can rest on an unphysical parameter.** In the
    prototype, radiative damping went slightly negative near the poles and no
    visual check revealed it. Physical parameters need invariant assertions of
    their own, not only budget checks.

### 23.1 Behavioural acceptance experiments

The table above asks whether the planet is plausible. Design v0.9 sections
36.6 and 37.6 ask a different question --- whether the *game* built on it
teaches what it claims --- and that question has headless answers too. These
belong in `tests/physics` alongside the table, and each has a numeric
criterion so it can fail.

| Experiment | Acceptance |
|---|---|
| Stop coal burning | haze clears within one season; CO2 concentration is unchanged over the same period |
| Clear a forest, then let it regrow | downwind precipitation falls measurably, then recovers with the expected lag |
| Clear past the dieback threshold | the response is a threshold, not a linear extrapolation of smaller clearings |
| Clean up aerosols at fixed CO2 | net forcing rises; the unmasking is visible as a warming acceleration |
| Projection skill by knowledge stage | error against the realised outcome falls monotonically across the six stages |
| Ensemble calibration | the stated quantile of the cone contains the realised outcome at approximately that rate over many runs |
| Early warning | autocorrelation and variance rise before a crossing more often than chance; the false-alarm rate is recorded |
| Adaptation only | outlasts do-nothing, but fails at high warming |
| Adaptation plus mitigation | outlasts both, at every warming level tested |
| Sustained crossing vs brief overshoot | sustained does not recover; brief, quickly reversed, does (row 16 above) |
| Knowledge has no physical effect | two runs differing only in `KnowledgeState` are bit-identical in `SlowState` |

The last row is the cheapest and the most important: it is the assertion that
section 17.2's central promise is true in the code rather than only in the
document.

## 24. Calibration harness

-   `tests/physics` holds the acceptance table above as an executable harness.
-   Calibration constants are versioned together with the values they were
    fitted against, and with the date and the run that produced them.
-   Any change to a transport process, a feedback, or the mesh resolution
    re-runs the harness before merge.
-   Climate-mode parameterisations are fitted to reference-mode runs
    (ADR-0001 §4.3); the fit coefficients are data, not magic numbers, and
    carry the same versioning rule.

## 25. Parallel gameplay track

P0 cannot answer whether the game is worth playing, and none of its milestones
test it. The Godot/GDScript prototype is kept alive deliberately as the fast
lane:

| Track | Purpose | Cadence |
|---|---|---|
| PlanetSim P0 (C++) | physical correctness, conservation, performance, architecture | milestone-driven |
| Gameplay prototype (GDScript) | does the loop hold attention? do players perceive causality? what information, when? pacing, onboarding | days |

The prototype is also a **behavioural oracle**: run the C++ core against the
same scenarios and compare with the recorded prototype results before trusting
its own calibration. Retire the prototype only at parity, never by merging it.

Design v0.9 gives the prototype a specific queue, in this order, because each
question is cheap there and expensive in C++:

1.  **fast-loop causality** (design v0.9 §36.6): can a player name the cause of a
    local change within two report intervals, unprompted? This is the single
    question that decides whether section 9.14 is worth its cost;
2.  **the projection cone** (design v0.9 §37.6): is it read as uncertainty, or as
    the game being wrong? If players read a wide cone as a bug, the mechanic
    needs different presentation before it is built in C++;
3.  **does science pay?** Do players who invest in science act earlier and
    fare better --- and do they feel that they did?
4.  **tipping legibility**: can a player name the cause of a tipping event
    without help? Start with forest dieback, which happens on their own land;
5.  **fast-loop strength**: how strong can local penalties be before the game
    becomes local housekeeping and the global story disappears (design v0.9 §36.4)?
    This is open question 1 of section 27 and only a playtest can close it.

## 26. Current implementation status and outstanding migration tasks

Status as of this revision (2026-09-28): M0 and M1 complete; the ADR migration
(integer tick clock, dual mesh, field registry, keyed RNG, aligned SoA fields,
`Field3D` layer-major layout, deterministic cell blocks, recursive cell
ordering, `-ffp-contract=off`, presentation snapshot schema 2) is applied.
Within M2: the finite-volume operators and the centroidal Voronoi mesh (G2-M2,
ADR-0002 §9), ADR-0005 (fractional coastlines, cell-mesh drainage), the
slow/fast/climatology state partition and the persistent `PSNAP` snapshot
format with a golden save (task M2-01, ADR-0003 §8) are complete. CI runs
GCC and Clang, Debug and Release, ASan+UBSan and the floating-point policy
check. The pre-terrain foundation hardening (task M2-01b) is also complete:
area-closure measurements and RNG outputs are pinned, deterministic reductions
are reusable, field container types derive from the registry, and CI enforces
the append-only registry contract. Plates, terrain, sub-cell hypsometry and the
sea-level solve (task M2-02) are complete; drainage (M2-03) is next. M2-02
amended ADR-0005 §4.1 (the ocean is anchored at the deepest cell; ADR-0005
§9.2).

Foundation work completed before M2 terrain starts (task
`docs/tasks/M2-01b-foundation-hardening.md`):

1.  ~~Record the measured dual area closure per level (L0--L6); the cell-count
    and pentagon-count assertions of section 5 already exist.~~ Done in
    `0ed83cb`.
2.  ~~Add an RNG reproducibility test with a pinned golden vector, so a future
    change to the mixing function cannot silently invalidate recorded runs.~~
    Done in `c394bee`.
3.  ~~Add a `reduce_deterministic_blocks` helper before any kernel needs a
    global sum; otherwise the first one will invent its own and re-introduce
    non-determinism.~~ Done in `cab2e7d`.
4.  ~~Tie `FieldDataType` to the container type (a `make_field<FieldId>()`
    factory that static-asserts the descriptor's dtype).~~ Done in `afde120`.
5.  ~~Add the registry append-only CI check against the previous release.~~
    Done in `69d1af5`.
6.  ~~Reconcile `FieldKind{diagnostic, prognostic, reservoir}` with the
    SlowState/FastState/Climatology partition of section 6.~~ Done in
    `03b8843`; `FieldPartition` is the sole classification.
7.  ~~Consolidate the two decision-record directories and add an index.~~
    Done: `docs/decisions/` with `README.md` and `archive/`.

### 26.1 Decision records required by design v0.9

None of these blocks M2 terrain. All of them block the milestone named.

1.  **Seasonal resolution in climate mode** (section 8). Fixes the
    climate-mode step at twelve sub-steps per year and therefore the fit
    targets of section 24. *Blocks M3.* This is the one to write first,
    because everything calibrated before it would be calibrated against the
    wrong step.
2.  **ADR-0001 amendment: the projection ensemble budget** (section 7.3).
    Ensemble size, running level, wall-clock ceiling per report interval, and
    the degradation policy when the ceiling is hit. *Blocks M14.2.*
3.  **Per-run tipping thresholds** (section 13.3). Where in the scenario
    construction the draw happens, what the physical ranges are, and how the
    manifest records them. Touches ADR-0003. *Blocks M10's dieback threshold.*
4.  **Observation, knowledge and estimated state** (sections 6, 17). The
    module boundary, what is snapshotted, and the assertion that knowledge
    cannot reach a solver. *Blocks the P1 gameplay track, not P0 --- but the
    `EstimatedState` field-identity rule of section 6 should be settled before
    the field set grows further.*

### 26.2 Work introduced by design v0.9

| Item | Section | Phase |
|---|---|---|
| Fast-loop residence times audited against the table | 9.14 | P0, with each coupling |
| Renderable signature for every fast loop | 9.14 | P0 / M14 |
| Early-warning statistics as diagnostics | 13.3, 15 | P0, from M10 |
| Behavioural acceptance experiments | 23.1 | P0, as their physics lands |
| `EstimatedState` and the observation layer | 6, 17 | P1 |
| Forward fork and the projection ensemble | 7.2, M14.2 | P1 |
| Consequence ledger and the sidecar analytics file | 15.1 | P1 |
| Knowledge-stage unlock tree | 17.1 | P1 |
| Adaptation measures beyond irrigation | 18.1 | P2--P3 |
| Event staging: pause, camera, newspaper | 13.3 | P2 |

## 27. Open questions

-   ~~Does climate mode resolve the seasonal cycle explicitly (12 steps/year)
    or carry seasonal statistics?~~ Position taken in section 8: resolve it
    explicitly, at twelve sub-steps per year. Needs the decision record of
    section 26.1 item 1 to close.
-   Smallest regional subset for a weather window that still behaves
    physically at its boundaries?
-   Do storms during accelerated play need tracks, or only strike locations and
    intensities? Tracks are better for legibility and imply S1 objects.
-   Same horizontal mesh for ocean and atmosphere? Current position: yes.
-   ~~Rivers on the cell mesh, or a separate elevation-derived flow network?~~
    Resolved by ADR-0005: the cell mesh.
-   Player-facing analytics (charts, newspaper record, measurement archive) in
    the snapshot, or a sidecar file? Current position: sidecar --- and the
    consequence ledger of section 15.1 goes with them.

New from design v0.9:

-   How strong may fast-loop penalties be before local housekeeping crowds out
    the global story (design v0.9 §36.4, section 25 item 5)? Playtest question.
-   How wide can the first projection cone be before it frustrates rather than
    informs, and how fast should it narrow (design v0.9 §37.2)? This is partly a
    presentation question and partly a choice of perturbation amplitude in
    section 7.2.
-   How should scoring and endings value living conditions, so that adaptation
    in a warmer world still counts as success (design v0.9 §32 and 37.4)?
-   What ensemble size N is enough for a cone the player can read? Too few
    members and the spread is noise; too many and section 7.3's budget breaks.
    Start from the cheapest N that passes the calibration row of section 23.1.
-   Does the counterfactual instance have to run at full resolution forever,
    now that the projection ensemble also wants compute (section 7.3)? The
    argument in section 7.1 for full resolution still holds; the argument for
    a coarser *projection* does not extend to it.

## 28. Traceability: design v0.9 to this specification

Design v0.9 added sections 36 and 37 and three rows to its decision register.
This table is the complete map, so that a later reader can check nothing was
dropped and a later design revision can be diffed the same way.

| Design v0.9 | Requirement | Specification |
|---|---|---|
| 36.1 | every action has a local, visible, recoverable consequence | 9.14 |
| 36.1 table | the six fast loops and their recovery times | 9.14 table |
| 36.2 | the slow loop is global, delayed and sticky | 9.1--9.13, unchanged |
| 36.3 | bridges: one flux seen at two scales; aerosol unmasking | 9.14 rules; 11.1 loop 22 |
| 36.4 | no scripted modifiers; visible before measured; warn, do not decide | 9.14 rules |
| 36.5 | report interval shortens; simulation slows on events; physics unaffected | 8 (pacing is presentation) |
| 36.6 | headless experiments per loop; consequence ledger | 23.1; 15.1 |
| 37.1 | six knowledge stages gate the climate view | 17.1 |
| 37.1 | instrument plus knowledge; knowledge may lag measurement | 17.2 |
| 37.1 | science reveals, never changes the physics | 17.2; 23.1 last row |
| 37.1 | scenarios may start at any stage, including after a loss | 16 |
| 37.2 | projection computed from the estimated state, as an ensemble | 7.2 |
| 37.2 | committed warming shown separately | 7.2; 15 |
| 37.2 | it is a history fork; coarse; off-thread; once per interval | 7.2; 8 |
| 37.2 | the cone narrows with observation and model maturity | 7.2; 23.1 |
| 37.3 | thresholds emergent, seed-varied, never scripted | 13.3 |
| 37.3 | warning / crossing / aftermath; the game layer only stages | 13.3 |
| 37.3 | early warnings are real critical-slowing-down signals | 13.3; 15 |
| 37.3 | no warning without observation | 13.3; 17.2 |
| 37.3 | brief overshoot may recover | 23 row 16; M13 |
| 37.3 | forest dieback is the first tipping point met | M10; 13.3 |
| 37.4 | adaptation changes exposure and boundaries, not forcing | 18.1 |
| 37.4 | maladaptation emerges; limits are physical | 18.1; 11.1 loops 23--24 |
| 37.4 | adaptation needs knowledge | 18.1; 17 |
| 37.4 | success measured by living conditions | 18.1; 27 |
| 37.4 | phasing P1 / P2--P3 / P4+ | 18.1 table; 19 |
| 37.5 | the three acts | 25 (prototype), 8 (report interval) |
| 37.6 | four validation experiments | 23.1 |
| register | feedback timescales; climate knowledge; tipping and adaptation | 9.14; 17; 13.3 and 18.1 |
| open questions | fast-loop strength; cone width; scoring | 27 |

Compute cost, accumulated. Worth stating in one place, because design v0.9 is
the revision that made it non-trivial: the authoritative instance and the
mandatory full-resolution counterfactual (7.1) split the ADR-0001 budget in
half; the projection ensemble (7.2) adds N + 1 forward runs per report
interval on top. Section 7.3 is where that gets resolved, and it is the
largest single risk this revision introduces.
