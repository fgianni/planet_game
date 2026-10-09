# Planetary Civilization Simulator --- Development Specification

Version: 0.8 (reconciled with design v1.3 and accepted ADRs 0001--0011 in
`docs/decisions/`)\
Purpose: implementation contract for Codex / Claude Code\
Primary target: PC/Linux, C++20 + Godot 4\
Current phase: P0 --- Living Planet (M0 through M6 complete, M7 in progress)

Design document: `docs/planetary_civilization_simulator_design_v1_4.docx`. Design v1.4 adds
section 46 (systems architecture) and changes nothing earlier; this
specification is reconciled with v1.3 and does not yet trace section 46
(ADR-0020 proposes its PlanetSim part).
Accepted decision records take precedence over this specification where they
conflict.

Decision records live in one directory, `docs/decisions/`, with an index in
`docs/decisions/README.md`; superseded records are kept under
`docs/decisions/archive/`.

What is new in v0.8, relative to v0.7 --- all of it from design v1.3
section 44, which v0.7 predates:

- **the fog of knowledge** (section 32): five layers --- geographic,
  observation, understanding, social and foreign --- built on the
  observation architecture of section 17;
- **a guessed map** for unexplored areas, generated as a pure function of
  the seed and corrected by exploration (section 32.4);
- **exploration** commands and passive sources, and a last-seen record so old
  knowledge can be out of date (sections 32.5, 32.6);
- **social estimates** with polling error and preference falsification under
  censorship, so the government sees its people through statistics
  (section 32.7); **foreign estimates** and verification (section 32.8);
- five fog channels added to the semantic channels, with readability
  signals (section 32.9), acceptance experiments (section 32.10), and
  ADR-0019 (section 26.1).

What v0.7 added, relative to v0.6 --- all of it from design v1.2
section 42 --- and which still stands:

- **a presentation boundary** (section 31.1): a Godot-free `sim/presentation`
  library turns snapshots into a `VisualFrame` of semantic channels;
- **swappable style packs** (section 31.3): art direction is data and shaders
  over the semantic channels; `stylised` and `map` styles are built first;
- **the readability invariant** (section 31.8): every style must make each
  planetary signal distinguishable, in colour and for colour-blind viewers,
  checked by a CI harness on test snapshots;
- temporal smoothing, camera scales, procedural detail that never
  contradicts its cell, event staging, knowledge-gated overlays and a
  performance budget (sections 31.4--31.9);
- **M14 is replaced by a rendering track R0--R8** that runs alongside the
  physics (section 31.10), and ADR-0018 is required before R1.

What v0.6 added, relative to v0.5 --- all of it from design v1.1
section 40 --- and which still stands:

- **natural forcing** (design v1.1 §40.5, §40.6): the solar cycle, volcanic
  eruptions as seeded events on the plate boundaries, and slow orbital change
  for long scenarios (section 30.2). The human signal must be found against
  them, not shown;
- **ocean carbonate chemistry** (design v1.1 §40.7): DIC and alkalinity in the
  M12 ocean carbon reservoir, with pH and aragonite saturation diagnosed
  (section 30.3);
- **a biodiversity index** with a permanent extinction counter, from M10
  (section 30.4);
- **pollution stocks** for water, soil, nutrients and toxic waste, with
  sources in the civilization flux interface (sections 18, 30.5);
- **the food system**: diets, livestock, fertilizer, storage and fisheries
  with collapse (section 30.6);
- **public finance and prices**: budget, taxes on income bands, debt whose
  interest rises with lost credibility (section 30.7);
- **conflict** as a P4 consequence system (section 30.8); **future
  generations**, **lifestyle norms**, **indicators and endings**, and a
  **learning debrief** isolated from the simulation (sections 30.9--30.12);
- milestone hooks in M10, M12 and M13, three new decision records
  (section 26.1), acceptance experiments (section 30.13) and traceability
  (section 28.2).

The four physical additions are P0 work inside existing milestones; the rest
is P1 or later.

What v0.5 added, relative to v0.4 --- all of it from design v1.0
sections 8.4 and 38 --- and which still stands:

- **the player's identity** (design v1.0 §8.4): the player is the civilization
  acting through its current government. Credibility and promises belong to
  each administration; an election loss continues the game (section 29.6.7);
- **a population and society model** (design v1.0 §38): regions, cohorts with
  inequality inside them, demography, consumption, opinion dynamics, media,
  political actors, a promise ledger, the implementation gap, mobilisation,
  elections and climate perception, with starting equations (section 29);
- **the exposure interface** (section 29.4): the one read-only path from the
  planet to the population, and the part P0 must leave room for;
- **three new module directories and dependency rules** (sections 2, 3, 29.2);
- **seven society phases, S1 to S7**, mapped onto P1, P3 and P4, each with
  headless acceptance experiments (sections 29.1, 29.12);
- civilization-side loops through people (section 11.1, loops 27--31);
- three decision records required before the code lands (section 26.1);
- a traceability map from design v1.0 (section 28.1).

Nothing in v0.5 is P0 work. Section 19 is unchanged in that respect.

What v0.4 added, relative to v0.3 --- all of it from design v0.9
sections 36 and 37 --- and which still stands:

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

Four items required decision records before the code lands; they are listed
in section 26.1.

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
Population / society / government -X-> planetary solver arrays
sim/presentation -X-> PlanetState, solvers
Style packs      -X-> raw snapshot fields
```

The last rule is the mirror of the third: the population reads the planet
only through `ExposureState` and reaches it only through the civilization
flux interface of section 18 (section 29.2).

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
│   ├── society/
│   ├── economy/
│   ├── energy/
│   ├── infrastructure/
│   ├── technology/
│   ├── government/
│   ├── scenario/
│   ├── history/
│   └── presentation/
├── apps/
│   ├── planet_cli/
│   ├── climate_lab/
│   └── benchmarks/
├── godot/
│   ├── project.godot
│   ├── gdextension/
│   ├── styles/
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
FogState            explored cells, last-seen record, territory (section 32)
SocialEstimate      the government's estimate of its own cohorts (32.7)
ForeignEstimate     estimates of other civilizations (32.8)
```

`EstimatedState` has the same field identities as `SlowState` so that a
projection run is an ordinary simulation with a different initial condition,
not a second physics. It is **never** written back into `SlowState`.

The population and society state of section 29.3 is different: it is
authoritative and must be snapshotted, but it is not planetary state either.
It lives in a separate `SocietyState` container outside `PlanetState`, under
the same registry, snapshot and migration rules (ADR-0012, section 26.1).
`ExposureState` (section 29.4) is derived from `SlowState` and is not
snapshotted.

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
    with the run seed and the tick, so a replay produces the same cone. They
    use a dedicated `RandomStreamId` (a projection stream), never one shared
    with weather, geology or validation, so adding or resizing an ensemble can
    never change a draw that the authoritative instance makes.

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
-   later population/politics/research: months to years. The society step
    runs once per climate sub-step, after the planetary sub-step, with
    quarterly and yearly processes inside it (section 29.5).

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

Decided by `docs/decisions/0006-seasonal-climate-steps.md` (accepted
2026-09-29): twelve sub-steps per orbital year, bounded at fixed mean-anomaly
phases and rounded to ticks, forced by the sub-step mean insolation, with no
adaptive coarsening.

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

The ocean reservoir carries DIC and alkalinity, so pH and aragonite
saturation are diagnosed and acidification is a separate consequence of
emissions that cooling cannot undo (section 30.3).

### 9.9 Aerosols

Do not merge aerosols into CO2.

The architecture must permit: - shortwave effects; - cloud
interactions; - finite atmospheric lifetime; - spatially heterogeneous
pollution.

Detailed aerosol physics is not required in the first implementation.

Stratospheric sulphate from volcanic eruptions is a separate tracer from
tropospheric pollution aerosol, with a lifetime of about a year, and uses the
same shortwave path (section 30.2).

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

From design v1.0 section 38 (section 29):

27. promises -> raised expectations -> unmet expectations -> grievance ->
    unrest (the J-curve; overpromising feeds future anger);
28. broken promise exposed -> credibility lost -> statements weaker ->
    harder to win support for the next policy (trust asymmetry);
29. efficiency gain -> cheaper service -> more use -> part of the saving lost
    (rebound; Jevons when the elasticity exceeds one);
30. slow warming -> generational baseline shift -> lower perceived risk ->
    delayed response (the shifting baseline);
31. economic stress -> climate salience crowded out -> policy delay ->
    more warming -> more stress (the finite pool of worry).

All five must emerge from the equations of section 29.6; none may be a
scripted modifier, and each has an acceptance row in section 29.12.

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

Completed (2026-09-29): tasks M3-01, M3-02 and M3-03, ADR-0006 §4.3 and
ADR-0007; see section 26.

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

Added in v0.6: the biodiversity index per region and biome, with its
monotonic extinction counter, is M10 state (section 30.4). Its functions are
P2.

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

Added in v0.6 (section 30): the solar cycle in the luminosity forcing;
volcanic eruptions as seeded events with stratospheric sulphate; DIC and
alkalinity in the ocean reservoir with diagnosed pH and Ω.

Reservoirs to be wired as they become available: atmosphere, vegetation,
soil, peat/permafrost, ocean (with the biological pump as a later term).
Short-lived forcers (section 9.11) keep their own lifetimes and are never
folded into the CO2 concentration.

### M13 --- Coupled climate experiments

Include no-atmosphere, doubled-CO2, zero-rotation, high-tilt,
eccentric-orbit, water-world, frozen-world, altered-ocean-transport,
deforestation and long-equilibrium runs.

Added in v0.6: the eruption-masks-trend experiment of section 30.13. Slow
orbital change for long scenarios follows M13 (section 30.2).

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

Replaced in v0.7 by the rendering track R0--R8 of section 31.10, which runs
alongside the physics milestones instead of after them. The original
obligation (render authoritative temperature, wind, currents, precipitation,
soil moisture, vegetation, snow/ice, clouds, SST and energy imbalance) is met
in the climate lab by R2--R7 and the overlays of section 31.7.

### M14.1 --- Counterfactual planet and attribution

Run the shadow instance of section 7.1 alongside the main one; expose local
and global differences; verify that with zero civilization fluxes the two
instances stay bit-identical (an L0 determinism check in disguise).

### M14.2 --- Projection ensemble and the committed future

Run the forward ensemble of section 7.2 from an `EstimatedState`, expose
quantiles rather than members, and render the ghost coastline, the future ice
edge and the temperature band. Include the separate committed-warming run.

Acceptance: with a perfect estimate and zero perturbation, and the projection
running at the authoritative instance's mesh level, the ensemble mean
reproduces a single long run of the same forcing to within the tolerance of
section 24 (a coarser projection cannot reproduce a full-resolution run, so
this check is made at the same level); with a degraded estimate the spread widens and still contains the
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
-   **five layers.** Design v1.3 extends this section into a fog of
    knowledge over the map, the civilization's own society and other
    civilizations (section 32);
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
livestock methane and fertilizer N2O
river pollutant and nutrient loads
soil degradation (tillage, overgrazing, salinisation)
fish catch
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

The population consumes these consequences through `ExposureState`, a
per-region derived aggregate defined in section 29.4.

### 18.1 Adaptation

Design v0.9 section 37.4 makes adaptation a full strategy. The architectural
question it raises is what adaptation is allowed to touch, and the answer is
that it changes **exposure and the local boundary condition**, never the
planet's forcing and never a damage number.

| Measure | What it may change | Phase |
|---|---|---|
| Sea walls and dikes | the local flood boundary; inundation extent for a given sea level and surge. Expressed on the ADR-0005 sub-cell hypsometry (a protected elevation within the cell's curve), not as a second coastline model | P2 |
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

From design v1.0, the whole population and society model of section 29 is
P1 (phases S1 and S2) or later. P0's only obligations toward it are not to
foreclose the exposure fields of section 29.4, the immutable cell-to-region
map, and a scheduler slot after the planetary sub-step.

From design v1.1, the solar cycle, volcanic eruptions, carbonate chemistry
and the biodiversity index are P0 (section 30.1). Everything else in section
30 is P1 or later, and conflict remains excluded before P4.

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

The oracle is distilled in `docs/prototype-oracle.md`: the prototype's
validation recipes with their recorded results, the constants worth starting
from, and the shortcuts PlanetSim must not copy. Use it instead of reading the
GDScript; the prototype itself is `prototype/climate_planet_0.5/`.

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

Status (2026-10-08): M0 through M6 complete. Climate mode's balanced
circulation carries the heat (ADR-0011 §17); the azonal flow carries none,
τ₀ = 1.442 gives 288 K with a 52 K equator-to-pole difference until M7 and
M11 add latent and ocean transport; both 250-year gates are met (L5 219 s,
L6 588 s). Task-level status is kept in `docs/tasks/README.md`, which
takes precedence over the history below.

Status at v0.4 (2026-09-29): M0 through M3 complete; the ADR
migration (integer tick clock, dual mesh, field registry, keyed RNG, aligned SoA fields,
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
sea-level solve (task M2-02) and deterministic static drainage topology
(task M2-03, ADR-0005 V5--V8) are complete. M2-02 amended ADR-0005 §4.1
(the ocean is anchored at the deepest cell; ADR-0005 §9.2). The
simulation-mode scheduler and the twelve-sub-step calendar (task M2-04,
ADR-0006) are complete.

M3 is complete (2026-09-29):

-   **M3-01**, sub-step mean insolation (ADR-0006 §4.3, V3, V4, V6): complete.
-   **M3-02**, surface energy columns (ADR-0007 V1--V10): land and ocean
    two-layer columns, T⁴ longwave through an optional grey layer,
    backward-Euler steps in both modes, experiments A and B, the calibrated
    Earth-like grey emissivity (§24), and PSNAP schema 2 with the first
    migration: complete.
-   **M3-03**, the remaining M3 obligations of accepted ADRs: complete.
    -   Ocean mixed-layer precision (ADR-0007 §10): the mixed layer is
        `float64`, under the new field ID `0x0003'0005` (the `float32` field
        `0x0003'0003` is retired), with PSNAP schema 3 and an exact v2 → v3
        step; v1, v2 and v3 golden saves load.
    -   Run manifest (`PRUNv1`), command log, yearly XXH3 checkpoint state
        hashes and replay (ADR-0003 §3.3), with V1 and V2 in CI;
        `planet_cli run` and `planet_cli replay`. The P0 commands so far are
        `set_mode` and `set_solar_luminosity_factor` (section 7); a command
        takes effect at the next step boundary, which in climate mode is the
        next sub-step.
    -   Golden saves of every schema load and step ten simulated years with
        closing budgets (ADR-0003 V5).
    -   The first performance gate (ADR-0001 §5, §10): 250 climate years in
        CI at L5 (≥ 20 years/min) and L6 (≥ 5 years/min); measured 2,981
        and 774 years/min on four workers.

Carried forward from M3, not blocking M4: ADR-0001 V4's > 20 % regression
comparison against a runner baseline; weather-window schedules in ADR-0003
V1 (weather windows are M10--M12); autosave and the continuously appended
manifest (ADR-0003 §3.7). Without horizontal transport the equator is too
hot and the poles too cold (ADR-0007 §6); the §23 temperature targets wait
for M5 and M11.

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

### 26.1 Decision records required by designs v0.9 to v1.3

None of these blocks M2 terrain. All of them block the milestone named.

1.  ~~**Seasonal resolution in climate mode** (section 8).~~ Done:
    ADR-0006, accepted 2026-09-29. Fixes the
    climate-mode step at twelve sub-steps per year and therefore the fit
    targets of section 24. It must also say how the sub-steps stay aligned
    with the orbit: the default orbital period is 525,948.7536 one-minute ticks,
    which twelve does not divide, and the integer clock may not accumulate a
    fractional remainder. *Blocks M3.* This is the one to write first,
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

Decision records required by design v1.0. None blocks P0.

5.  **ADR-0012: society state, regions and snapshots** (sections 6, 29.3,
    29.4). The `SocietyState` container and its partition, the field-id range
    for civilization fields, the cell-to-region map and where it lives,
    `ExposureState` as derived data, and the extension of the
    knowledge-has-no-physical-effect assertion to society state.
    *Blocks S1.*
6.  **ADR-0013: cohort model** (sections 29.3, 29.5, 29.6.1--29.6.4). Social
    groups and age bands, append-only group creation, the income-band
    representation, the society step order and its RNG streams, and the
    parameter-file format with provenance. *Blocks S1.*
7.  **ADR-0014: player commands, statements and the promise ledger**
    (sections 29.6.6, 29.6.7, 29.9, 29.10). Command validation, what makes a
    promise measurable, administrations and what carries over between them,
    and how the latent ledger of hidden gaps is released. *Blocks S4.*

Decision records required by design v1.1.

8.  **ADR-0015: natural forcing events** (section 30.2). The volcano list and
    its derivation from plate boundaries, rate and size distributions, the
    RNG stream and manifest record, prescribed historical eruptions, the
    stratospheric tracer, the solar-cycle parameters and jitter, and how
    orbital drift updates `PlanetParameters`. *Blocks the M12 forcing work.*
9.  **ADR-0016: ocean carbonate chemistry** (section 30.3). The carbonate
    system approximation, field ids and precision, the gas-transfer
    parameterisation, deep exchange, and validation ranges. *Blocks M12.*
10. **ADR-0017: commodities, prices and public finance** (sections 30.6,
    30.7). The commodity list including food, the price update and its
    bounds, trade along the network, budget accounts, tax bases on income
    bands and the interest rule. *Blocks S2.*

Decision record required by design v1.2.

11. **ADR-0018: presentation boundary, semantic channels and style packs**
    (sections 31.1--31.3, 31.8). *Blocks R1.*

Decision record required by design v1.3.

12. **ADR-0019: the fog of knowledge** (section 32). Where fog state lives
    and how it is snapshotted (extending ADR-0012), the guessed-map function
    and its parameters, exploration rules, the last-seen record, the social
    estimate model and its RNG streams, and the extended no-effect
    assertion. *Blocks the P1 exploration work; the fog channels are
    reserved in ADR-0018.*

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

### 26.3 Work introduced by design v1.0

| Item | Section | Phase |
|---|---|---|
| Cell-to-region map built with the mesh-derived data | 29.3 | P0 obligation (do not foreclose) |
| `ExposureState` aggregation | 29.4 | P1, S1 |
| Society scheduler slot | 8, 29.5 | P1, S1 |
| Cohorts, demography, migration, education | 29.3, 29.6.1--29.6.2 | P1, S1 |
| Needs, expectations, consumption, adoption | 29.6.3--29.6.4 | P1, S2 |
| Opinion network, salience, memory, misinformation | 29.6.5 | P3, S3 |
| Statements, actors, credibility, promise ledger, implementation | 29.6.6--29.6.7, 29.8--29.10 | P3, S4 |
| Mobilisation, support, elections, migration under stress | 29.6.8--29.6.9 | P3, S5 |
| Climate perception | 29.6.10 | P3, S6 |
| Representative citizens, promise tracker, cohort map | 29.7; design v1.0 §38.13 | P3, presentation |
| Other nations | 29.1 | P4, S7 |

The gameplay prototype (section 25) is the cheap place to test the credibility
mechanic before S4 is built in C++: whether players notice the gap between
their words and deeds, and whether trust asymmetry feels fair rather than
punitive.

### 26.4 Work introduced by design v1.1

Section 30.1 is the complete placement table. The P0 items are:

| Item | Section | Milestone |
|---|---|---|
| Biodiversity index and extinction counter | 30.4 | M10 |
| Solar cycle | 30.2 | M12 |
| Volcanic eruptions and stratospheric sulphate | 30.2 | M12; experiment at M13 |
| DIC, alkalinity, pH and Ω | 30.3 | M12 |
| Orbital change for long scenarios | 30.2 | after M13 |

### 26.5 Work introduced by design v1.2

The rendering track of section 31.10. R1 can start now: it needs only the
existing snapshots and ADR-0018, and it makes every later visual layer
style-independent from the start.

### 26.6 Work introduced by design v1.3

Section 32.1 places each layer. Nothing is P0 work; R1 reserves the five fog
channels so both styles implement them from the start.

## 27. Open questions

-   ~~Does climate mode resolve the seasonal cycle explicitly (12 steps/year)
    or carry seasonal statistics?~~ Position taken in section 8: resolve it
    explicitly, at twelve sub-steps per year. Closed by ADR-0006.
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

New from design v1.0:

-   How many regions and social groups are needed before the population
    feels alive, and where does more detail stop being readable (design v1.0
    §38.2)? Start from 30 regions and 6 groups.
-   How strong should the trust asymmetry be? The default ratio of 4 in
    section 29.6.7 must not make honest but slow policies, such as research,
    unplayable. Prototype question first.
-   Should regime type evolve in the first society release, or stay fixed per
    scenario? Current position: fixed (section 29.13).
-   How are identity groups and value change modelled without caricature, and
    without making one opinion the only rational one? The last row of
    section 29.12 is the first guard; playtests are the second.
-   Should some scenarios lock the player into one government, with an
    election loss as defeat (design v1.0 §19)?

New from design v1.1:

-   How much of the budget does the player manage directly, and how much
    follows from standing policy, so finance stays a constraint rather than
    bookkeeping?
-   How is conflict kept a consequence the player regrets, rather than a
    strategy to optimise?
-   Which indicators make up the end report, and are they weighted at all?
-   What eruption rate and size distribution test the player's science
    without masking the human signal for too long? ADR-0015 proposes; the
    prototype checks.
-   Which reference series does the debrief use, and how is the comparison
    kept honest when the planet is not Earth?

New from design v1.2:

-   How far may a cartoon style simplify before it hides a signal? The
    readability threshold of section 31.8 (mean ΔE 10) is a starting value.
-   Switch styles mid-game or only between games?
-   May community style packs carry scripts, or only data and shaders?
-   How much procedural detail may be added inside a cell before it
    misleads (section 31.5)?

New from design v1.3:

-   How wrong may the guessed map be: wrong enough to surprise, never so
    wrong that an early decision feels like a trick? Amplitudes in section
    32.4 are scenario data so this can be tuned.
-   Should scenarios after the age of exploration start without geographic
    fog, or keep fog over remote interiors and the deep ocean?
-   How visible should the gap between polls and true opinion be, so the
    player learns to distrust polls without feeling cheated?

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

### 28.1 Design v1.0 to this specification

Design v1.0 added section 38, rewrote section 8.4, extended sections 7, 16,
18 and 19, and added a revision note (section 39).

| Design v1.0 | Requirement | Specification |
|---|---|---|
| 8.4 | player is the civilization acting through its government | 29.6.7; 29.9 |
| 7 | cohorts, inequality, demography, urbanization, consumption | 29.3; 29.6.1--29.6.4 |
| 16 | S1--S2 in P1; S3--S6 in P3; S7 in P4 | 29.1; 26.3 |
| 38.1 | cohorts not agents; values slow, opinions fast | 29.3; 29.5 |
| 38.1 | legitimate priorities; fictional identities | 29.12 last row; 29.13 |
| 38.1 | population reads the planet; planet never reads it | 2; 29.2; 29.4 |
| 38.2 | social groups, age bands, regions, income bands | 29.3 |
| 38.2 | representative citizens are presentation only | 29.7 |
| 38.3 | cohort state layers | 29.3 table |
| 38.4 | demographic transition, urbanization, education, health | 29.6.1; 29.6.2 |
| 38.5 | needs, expectations, consumption, adoption, rebound, lock-in | 29.6.3; 29.6.4 |
| 38.6 | Friedkin--Johnsen, bounded confidence, two-step flow | 29.6.5 |
| 38.6 | misinformation, collective memory, identity | 29.6.5 |
| 38.7 | political actors as rule-based agents | 29.8 |
| 38.8 | statements, deeds, visibility, credibility | 29.6.6; 29.6.7; 29.9 |
| 38.8 | implementation gap, promise ledger, trust asymmetry | 29.6.7; 29.10 |
| 38.8 | press freedom and censorship | 29.6.7 (latent ledger) |
| 38.8 | change of government | 29.6.7; 29.6.9 |
| 38.9 | exit, voice, loyalty; thresholds; Easton support | 29.6.2; 29.6.8 |
| 38.9 | elections; regime fixed per scenario; bottom-up adaptation | 29.6.9; 29.13; 18.1 |
| 38.10 | experience, shifting baseline, worry budget, distance, identity | 29.6.10; 29.6.5 |
| 38.11 | other nations | 29.1 (S7) |
| 38.12 | coupling and cadence | 29.4; 29.5 |
| 38.13 | legibility, calibration, balancing hooks | 29.7; 29.11 |
| 38.14 | seven phases | 29.1 |
| 38.15 | stylised-fact validation | 29.12 |
| 18, 19 register and questions | four decisions; four open questions | 26.1; 27 |

### 28.2 Design v1.1 to this specification

| Design v1.1 | Requirement | Specification |
|---|---|---|
| 40.1 | budget, taxes on income bands, debt, no markets | 30.7 |
| 40.2 | diets, livestock, fisheries, fertilizer, storage, waste | 30.6; 18 |
| 40.3 | conflict as a P4 consequence system | 30.8 |
| 40.4 | five indicators, era milestones, self-set goals, collapse as an ending | 30.11 |
| 40.5 | volcanic eruptions; earthquakes and tsunamis | 30.2; 9.9 |
| 40.6 | solar cycle, internal variability, orbital change | 30.2 |
| 40.7 | ocean acidification | 30.3; 9.8 |
| 40.8 | pollution stocks | 30.5; 18 |
| 40.9 | biodiversity index and permanent extinctions | 30.4; M10 |
| 40.10 | voice of the young; future-generations indicator; discounting | 30.9 |
| 40.11 | lifestyle norms | 30.10 |
| 40.12 | learning debrief, isolated from the simulation | 30.12 |
| 40.14 | staging | 30.1; 26.4 |
| 17 | warfare in P4; public finance in scope; debrief without reproduction | 19; 30.8; 30.12 |

### 28.3 Design v1.2 to this specification

| Design v1.2 | Requirement | Specification |
|---|---|---|
| 42.1 | presentation only; truthful detail; information invariant | 31.1; 31.5; 31.8 |
| 42.2 | style packs over semantic channels; two styles first | 31.2; 31.3 |
| 42.3 | readability test snapshots, colour-blind checks | 31.8 |
| 42.4 | visual layers by milestone | 31.2; 31.10 |
| 42.5 | planet, region and local scales | 31.5 |
| 42.6 | civilization footprint | 31.2 (R8 channels); 31.10 |
| 42.7 | smoothing that grows with speed; events unsmoothed | 31.4 |
| 42.8 | event staging, player-adjustable | 31.6 |
| 42.9 | knowledge-gated overlays, uncertainty, society views | 31.7; 31.10 R8 |
| 42.10 | accessibility | 31.7; 31.8 |
| 42.11 | performance | 31.9 |
| 42.12 | rendering track R0--R8 | 31.10 |

### 28.4 Design v1.3 to this specification

| Design v1.3 | Requirement | Specification |
|---|---|---|
| 44.1 | five layers of fog | 32.1 |
| 44.2 | fog changes knowledge, not the world; own land visible; knowledge ages; lifting fog costs | 32.2; 32.6; 32.5 |
| 44.3 | mistaken maps corrected by exploration; satellites end geographic fog | 32.4; 32.5 |
| 44.4 | observation fog on the map | 17; 32.6 |
| 44.5 | understanding fog | 17.1; 17.2 |
| 44.6 | census, polls, press; the dictator's dilemma; truth by events | 32.7 |
| 44.7 | foreign fog and verification | 32.8 |
| 44.8 | fog drawn by every style; readability | 32.9; 31.8 |
| 44.9 | staging | 32.1 |

## 29. Population and society

Design v1.0 section 38 adds a population model grounded in sociology,
political science and climate psychology, and settles the player's identity
(design v1.0 §8.4): the player is the civilization acting through its current
government. This section is the implementation contract for it.

None of it is P0 work (section 19). It is specified now for two reasons:
P1 begins with phases S1 and S2, and P0 must not foreclose them. The exposure
interface (section 29.4), the region map and the scheduler slot (section 29.5)
are the parts P0 has to leave room for.

### 29.1 Phases

| Phase | Content | Gameplay stage | Blocked by |
|---|---|---|---|
| S1 Foundation | Regions, cohorts, demography, urbanization, education, inequality inside cohorts | P1 | ADR-0012, ADR-0013 |
| S2 Economy and behaviour | Needs and expectations, consumption, technology adoption, rebound | P1 | S1; economy of P1 |
| S3 Opinion | Influence network, issues and salience, identity, misinformation, collective memory | P3 | S2 |
| S4 Politics | Statements and media, political actors, promise ledger, implementation gap, credibility | P3 | S3; ADR-0014 |
| S5 Responses | Protest thresholds, support stocks, elections, migration, bottom-up adaptation | P3 | S4 |
| S6 Climate perception | Experience-based risk, shifting baselines, worry budget, health | P3 | S5; knowledge stages of section 17.1 |
| S7 International | Other nations, refugees, trade shocks, climate agreements | P4 | S6; other civilizations |

S2 deliberately comes before any politics: once cohorts consume, the planet
receives real demand-driven fluxes from society (section 18) before opinion
exists. Every phase ends with its acceptance experiments of section 29.12
passing headless.

### 29.2 Module boundaries

``` text
sim/population/   regions, cohorts, demography, migration, education,
                  needs, expectations, consumption, adoption, perception
sim/society/      issues, influence network, opinion dynamics, media,
                  political actors, collective memory, mobilisation
sim/government/   administrations, credibility, promise ledger,
                  implementation capacity, elections, regime state
```

Dependencies, added to the forbidden list of section 2:

``` text
Climate / planetary solvers  -X-> population, society, government
population/society/government -X-> planetary solver arrays
population  ---> ExposureState (read only, section 29.4), economy outputs
society     ---> population
government  ---> society, population; emits commands to economy and
                 infrastructure, which emit the fluxes of section 18
Presentation (representative citizens) -X-> any authoritative state
```

The only path from society to the planet is the civilization flux interface
of section 18. A cohort never writes a planetary field; its consumption
becomes demand, the economy turns demand into activity, and activity becomes
emissions, land use and water withdrawal.

### 29.3 State

**Regions.** A region is a fixed, contiguous set of surface cells defined by
the scenario at construction (30 to 50 for Industrial Dawn). The cell-to-region
map is immutable and stored with the mesh-derived data; region order is the
order of the region's lowest cell id, so it is independent of thread count.

**Cohorts.** `CohortId = (region, social_group, age_band)`, stored densely in
structure-of-arrays order with region outermost. Social groups are scenario
data (design v1.0 §38.2 lists the Industrial Dawn set) and may be appended at
run time when the economy creates a new group; an appended group gets a new
index, never a reused one. Age bands are young (0--19), working (20--59) and
old (60+).

Every cohort carries the following. Population counts are `double` because
they are a conserved budget; the rest is `float`.

| Field | Shape | Units / range | Cadence | Notes |
|---|---|---|---|---|
| `population` | cohort | persons, ≥ 0 | yearly, migration monthly | conserved budget (29.6.1) |
| `income_share` | cohort × 5 quantile bands | sums to 1 | yearly | inequality inside the cohort |
| `mean_income` | cohort | currency per capita per year | quarterly | from the economy |
| `employment_rate` | cohort | 0--1 | quarterly | |
| `literacy` | cohort | 0--1 | yearly | |
| `need_satisfaction` | cohort × band × need | 0--1 | quarterly | needs listed in 29.6.3 |
| `expectation` | cohort × band × need | 0--1.5 | quarterly | may exceed what is available |
| `values` | cohort × 3 axes | −1 to 1 | generational | Schwartz / Inglehart axes |
| `identity_weight` | cohort × identity | 0--1 | generational | fictional identities only |
| `opinion` | cohort × issue | −1 to 1 | monthly | position |
| `salience` | cohort × issue | sums to 1 | monthly | finite pool of worry |
| `anchor_opinion` | cohort × issue | −1 to 1 | generational | Friedkin--Johnsen prior |
| `credibility` | cohort × administration | 0--1 | monthly | current administration only is live |
| `politician_trust` | cohort | 0--1 | monthly | general trust, carried across administrations |
| `specific_support` | cohort | 0--1 | monthly | Easton |
| `diffuse_support` | cohort | 0--1 | yearly | Easton |
| `grievance` | cohort | ≥ 0 | monthly | relative deprivation |
| `organisation` | cohort | 0--1 | yearly | unions, churches, parties |
| `protest_participation` | cohort | 0--1 | monthly | threshold model output |
| `perceived_risk` | cohort × hazard | 0--1 | monthly | |
| `climate_baseline` | cohort × exposure variable | units of the variable | at ageing | set during youth, inherited |
| `adoption` | cohort × technology | 0--1 | yearly | Bass diffusion |
| `memory` | region × group × event slot | weight ≥ 0 | yearly decay | bounded ring of events |

Issues are the priorities of design v1.0 §8.1: economic security and
employment, food and housing, energy prices, health and services, tax,
local environment, climate and adaptation, and order and freedom. The issue
list is scenario data and append-only within a run.

**Partition.** This state is authoritative and must be snapshotted, which
section 6's three planetary partitions do not cover. ADR-0012 decides how:
the proposal is a separate `SocietyState` container outside `PlanetState`,
with its own field-id range in the registry (`0x0100'xxxx` onward), the same
append-only, migration and snapshot rules as planetary fields, and the
assertion of section 23.1's last row extended to it: two runs differing only
in `SocietyState` must be bit-identical in planetary state until a society
command emits a flux.

### 29.4 Exposure interface

The population reads the planet only through `ExposureState`, a derived
per-region aggregate produced at each climate sub-step from `SlowState` and
`Climatology`. It is the read-only face of section 18's "PlanetSim returns
physical consequences".

| Exposure variable | Source | Aggregation |
|---|---|---|
| `crop_yield_index` | crop environment of P1 (temperature, soil water, season) | area-weighted over cultivated cells |
| `flood_fraction` | runoff, discharge, surge on the ADR-0005 hypsometry | population-weighted |
| `drought_index` | soil-moisture anomaly against the cohort's baseline | area-weighted |
| `heat_stress_days` | days above a humid-heat threshold in the sub-step | population-weighted |
| `inundated_fraction` | sea level against hypsometry and protected elevation | population-weighted |
| `air_quality` | aerosol and pollutant load (section 9.9) | population-weighted |
| `extreme_events` | storms, fires, floods detected in the sub-step | list with magnitude and region |
| `heating_degree_days`, `cooling_degree_days` | surface air temperature | population-weighted |

Rules:

-   `ExposureState` is derived. It is not snapshotted; it is recomputed from
    planetary state after load, and a replay must reproduce it bit for bit;
-   the population sees *true* exposure, because people live through the
    real weather. Knowledge (section 17) gates what the government and the
    media can *say about causes*, not what a flood does to a village;
-   a variable that is not yet simulated in the current milestone is absent,
    not faked. S1 runs on temperature and a prescribed yield index until the
    crop model exists.

### 29.5 Cadence

The society step is the climate sub-step of ADR-0006 (twelve per orbital
year), so the scheduler gains one slot after the planetary sub-step and
before the snapshot.

| Process | Runs | Order inside the step |
|---|---|---|
| Exposure aggregation | every sub-step | 1 |
| Migration flows | every sub-step | 2 |
| Media, opinion diffusion, salience | every sub-step | 3 |
| Protest threshold fixed point | every sub-step | 4 |
| Needs, expectations, consumption, prices | every third sub-step (quarterly) | 5 |
| Promise checks | every third sub-step, and at each promise's deadline | 6 |
| Demography, education, adoption, diffuse support, memory decay | sub-step 12 of each year | 7 |
| Ageing, inheritance of baselines and memory | sub-step 12 of each year | 8 |
| Elections | at scheduled ticks, after step 8 | 9 |

The order is fixed and part of the run manifest. Within a process, cohorts
are visited in storage order and every global sum uses
`reduce_deterministic_blocks` (section 26). Randomness uses the counter RNG
keyed as `(world_seed, stream_id, tick, cohort_index, sample_index)` with a
reserved stream id per process.

Performance target: with 1,000 cohorts, 8 issues and about 20 influence
neighbours per cohort, the whole society step must cost under 1 % of the
ADR-0001 climate-mode budget at L5. It is not allowed to become the reason
a performance gate fails.

### 29.6 Model equations

These are the starting forms. Every coefficient is data in the parameter
file of section 29.11, versioned with its provenance like the calibration
constants of section 24. Notation: `c` a cohort, `i` an issue, `k` a need,
`b` an income band, `Δt` the process interval in years.

#### 29.6.1 Demography

Ageing moves a fraction `Δt / band_width` of each band to the next once a
year; births enter the young band; deaths leave each band.

``` text
births_c   = f(D_r) * 0.5 * N_working,c * Δt
f(D)       = f_min + (f_max - f_min) * σ(-k_f * (D - D_f0))
deaths_c   = m_age(band) * h_c * N_c * Δt
h_c        = 1 + a_food * (1 - s_food) + a_san * (1 - s_san)
               + a_heat * heat_stress_days + a_air * (1 - air_quality)
             - a_care * s_health
D_r        = development index: mean of literacy, urban share and
             normalised income in the region
```

Mortality responds before fertility (`D_f0` sits above the level at which
`h` falls), which is what produces the demographic transition rather than
asserting it.

**Budget.** For each region and year:
`N(t+1) = N(t) + births - deaths + in_migration - out_migration`, exactly in
`double`. This is a conservation test like the water and energy budgets.

#### 29.6.2 Migration and urbanization

A gravity flow between regions `r → q`, and between the rural and urban
groups of one region:

``` text
flow_rq = M * N_r * push_r * pull_q / d_rq^β
push_r  = exp(w_wage * (wage_q - wage_r) + w_stress * stress_r
              + w_conflict * unrest_r)
pull_q  = housing_slack_q * (1 + w_diaspora * diaspora_rq)
          * (1 + w_identity * shared_identity_rq)
stress_r = combined exposure anomaly (flood, drought, heat, inundation)
```

Flows are capped by the source population and by destination housing; the
cap is applied in region order so the result does not depend on scheduling.

#### 29.6.3 Needs, expectations and grievance

Needs in priority order: food, water, shelter and heating, employment,
health and sanitation, energy, education, services and transport, consumer
goods, environmental quality. For each cohort, band and need:

``` text
s_ck   = min(1, supply_ck / need_ck)
w_ck   = priority_k * Π_{j<k} s_cj            (lower needs gate higher ones)
E_ck  += α_up * max(0, s_ck - E_ck) * Δt
       - α_down * max(0, E_ck - s_ck) * Δt
       + Σ_promises π_p * audience_pc        (promise-raised expectations)
G_c    = Σ_k w_ck * max(0, E_ck - s_ck)      (relative deprivation)
```

`α_up > α_down` (expectations rise faster than they fall) is what produces
the J-curve: unrest peaks when satisfaction stalls after a rise, not at its
lowest point.

#### 29.6.4 Consumption, adoption and rebound

``` text
service_c,u  = base_u(income_b, climate) * (price_u / efficiency_u)^(-ε_u)
energy_c,u   = service_c,u / efficiency_u
dA_c,τ/dt    = (p_τ + q_τ * A_neighbours,τ) * (1 - A_c,τ) * afford_c,τ
```

`u` is an end use (heating, lighting, transport, goods, food), `τ` a
technology, `climate` the heating and cooling degree days of section 29.4.
A higher efficiency lowers the price of the service, so service rises with
elasticity `ε_u`: that is the rebound, and Jevons at the scale of the economy
if `ε_u > 1`. Energy demand feeds the economy; the economy emits.

#### 29.6.5 Opinion dynamics

For each issue, once per sub-step:

``` text
W_cj   = ω_cj * [ |x_c - x_j| ≤ ε_c ]          (bounded confidence)
x_c'   = λ_c * Σ_j Ŵ_cj x_j + (1 - λ_c) * x_c^0 + Σ_m Δx_cm
ε_c    = ε_0 * (1 - identity_tie_ci)           (identity narrows listening)
s_c'   = softmax(β_s * (G-pressure_ci + agenda_ci + experience_ci))
```

`Ŵ` is `W` row-normalised; a cohort with no neighbour inside its bound
keeps its own opinion. `ω` is the influence network: shared region, shared
group, economic ties, shared identity and media overlap, rebuilt yearly.
`λ_c` is the susceptibility (one minus stubbornness), lower for elites and
the old. Opinion leaders (clergy, union, notable groups) receive media
effects first and pass them on through `ω`, which is the two-step flow.

**Misinformation** is a message like any other, with a truth value the
cohort cannot see. Its uptake is scaled by fit with the cohort's opinion and
by emotional salience, and damped by literacy and by trust in the outlet.

**Collective memory** is a bounded ring of events per region and group
(famine, lethal flood, broken promise, victory), each with a weight that
decays as `exp(-t / τ_mem)` with `τ_mem` of decades. At ageing, a fraction
`ι` of the weight is inherited by the next band. Memory biases the
interpretation of new events of the same kind and lowers `politician_trust`.

#### 29.6.6 Media and statements

A statement `m` has a type (promise, framing, agenda, blame/credit), an
issue, a position or frame value, a channel and an author (an
administration or an actor of section 29.8). Its effect on cohort `c`:

``` text
reach_c,ch  = access_ch(literacy_c, urban_c, technology) * audience_share
Δx_cm       = reach * κ_type * cred_c(author) * [ |x_c - f_m| ≤ ε_c ]
                  * (f_m - x_c)
Δagenda_ci  = reach * κ_agenda * cred_c(author)   (agenda: salience, not position)
```

Framing that ties an issue to an identity raises `identity_tie_ci`, which
narrows `ε` and freezes the cohort on that issue; a player can cause this
by accident.

#### 29.6.7 Credibility, promises and the implementation gap

``` text
cred_c'  = cred_c + a_plus  * (1 - cred_c) * kept_c
                  - a_minus * cred_c       * exposed_broken_c
a_minus / a_plus = asymmetry, default 4 (Slovic); tunable, see section 27
```

A new administration starts with
`cred_c = politician_trust_c * platform_appeal_c`; the old administration's
credibility is discarded, and each of its unfulfilled promises becomes a
memory event that lowers `politician_trust`.

**Implementation.** A decision with nominal effect `e` and nominal delay
`d` is delivered as `e * κ_impl` after `d / κ_impl`, where the
implementation capacity `κ_impl ∈ (0, 1]` grows with administrative
investment, literacy and stability and falls with corruption and unrest.
The shortfall is real: the railway is late, and the population sees that.

**Gap detection.** At each promise check:

``` text
perceived_pc = w_exp * experienced_pc + (1 - w_exp) * reported_pc
reported_pc  = Σ_outlets trust_c,o * report_o(p)
gap_pc       = max(0, target_p - perceived_pc)
exposed_pc   = gap_pc > θ_gap   with probability
               P_expose = 1 - (1 - press_freedom * scrutiny_o)^outlets
```

The exposure draw uses the keyed RNG. Censorship lowers `press_freedom`,
which hides gaps, but every hidden gap accumulates in a latent ledger that
is released at once when press freedom rises or diffuse support collapses.

#### 29.6.8 Mobilisation and support

The threshold model is solved per sub-step as a bounded fixed point:

``` text
P_c^(n+1) = Φ( (G_c + γ * P_neighbours^(n) + trigger_c - μ_c) / σ_c )
μ_c       = μ_0 - η * organisation_c
```

Iterate at most 16 times or until the change is below `1e-4`; the
iteration count is a diagnostic. Small triggers can cascade or fizzle,
depending on the threshold distribution, which is the Granovetter result.

``` text
S_spec'  = S_spec + ρ_s * (f(satisfaction, cred) - S_spec) * Δt
S_diff'  = S_diff + ρ_d * (S_spec - S_diff) * Δt      with ρ_d << ρ_s
regime crisis when the population-weighted S_diff < D_crit
```

#### 29.6.9 Elections

Vote shares are expected values, not samples:

``` text
v_ck = softmax_k( -β_v * Σ_i s_ci * (x_ci - pos_ki)^2
                  + β_inc * [k incumbent] * cred_c
                  + β_mem * memory_ck + noise_k )
```

`noise_k` is one campaign shock per party per election from the keyed RNG,
so two seeds give different but plausible results. Seats follow the
scenario's electoral rule. A lost election creates a new administration
(section 29.6.7) whose platform can disable or reverse player policies; the
game continues.

#### 29.6.10 Climate perception

``` text
anomaly_c,h  = (exposure_h - climate_baseline_c,h) / spread_h
r_c,h'       = r_c,h + φ_up * max(0, anomaly) * (1 - r_c,h)
                     - φ_down * r_c,h * Δt
experience_ci feeds salience (29.6.5) through r
climate_baseline_c,h  set as the mean exposure during the cohort's young band,
                      inherited at ageing    (shifting baseline)
```

Science results (section 17) enter as media messages from the scientists'
actor with the credibility that actor has earned; they change what can be
said, not what cohorts automatically believe.

### 29.7 Representative citizens

Named citizens shown in the newspaper and on the map are generated by the
presentation layer from `(cohort_index, report_interval, sample_index)` with
the keyed RNG and a name table from the scenario. They read cohort state and
own none. They are never snapshotted and never read by the simulation, so
they cannot break determinism or replay.

### 29.8 Political actors

Opposition parties, unions, industrial and landed interests, churches,
scientists, civil-society groups and independent outlets are rule-based
agents. Each has an agenda vector over issues, an influence budget per step,
and a channel mix. Each step it chooses statements greedily by expected
effect on its agenda, with ties broken by actor index. Actors are not
planners and do not search.

### 29.9 Commands

New `PlayerCommand` kinds, validated by PlanetSim and recorded in the
command log like every command (ADR-0003):

| Command | Payload | Effect |
|---|---|---|
| `issue_statement` | type, issue, frame value, channel, audience | media message (29.6.6) |
| `make_promise` | issue, target metric, target value, deadline, audience | ledger entry and expectation rise |
| `set_policy` | policy id, parameters | nominal decision through the implementation gap |
| `build` | project, region | as P1 construction; delayed by capacity |
| `set_research_priority` | field, share | research allocation (section 17) |
| `set_press_freedom` | level | changes exposure and science diffusion |

A command takes effect at the next society step. Validation rejects a
promise without a measurable target metric; vague promises are statements,
not promises.

### 29.10 Promise ledger

``` cpp
struct Promise {
    PromiseId        id;               // append-only within a run
    AdministrationId administration;
    IssueId          issue;
    MetricId         target_metric;    // e.g. regional electrification share
    float            target_value;
    SimulationTick   made_at;
    SimulationTick   deadline;
    CohortMask       audience;
    PromiseStatus    status;           // pending, kept, broken_hidden,
                                       // broken_exposed, void
};
```

The ledger is part of `SocietyState`. A promise is void, not broken, when
its administration has left office; it then contributes to collective
memory instead (29.6.7). The presentation shows each cohort's view of each
promise, not the true status.

### 29.11 Calibration and data

-   Industrial Dawn baselines: population and age structure, literacy,
    urban share and class structure by region, from historical demography,
    with the sources recorded next to the values;
-   the parameter file (`scenarios/<scenario>/society_params.toml`) holds
    every coefficient of section 29.6 with units, range, source and date;
-   parameters are tuned against the stylised facts of section 29.12, never
    against a desired history; every tuning is a recorded change, as in
    section 24.

### 29.12 Acceptance experiments

Headless, fixed seeds, in `tests/scenarios`. Each has a numeric criterion
so it can fail; the thresholds are starting values to be confirmed when the
phase lands.

| Phase | Experiment | Acceptance |
|---|---|---|
| S1 | Population budget | regional and global budgets close exactly every year for 250 years |
| S1 | Demographic transition | with rising development, mortality falls first and fertility falls within 1--3 generations after |
| S1 | Determinism | identical `SocietyState` hashes across thread counts; society off vs on is bit-identical in planetary state before the first society flux |
| S2 | Rebound | an efficiency gain of 30 % lowers energy use by less than 30 % when `ε > 0` |
| S2 | Adoption | S-shaped uptake; time to 50 % falls with income and with neighbours' adoption |
| S3 | Polarisation | fragmented media with strong identity ties ends in at least two separated opinion clusters; a shared channel converges |
| S3 | Memory | a broken promise still lowers trust in the next generation, by less than in the first |
| S4 | Trust asymmetry | after one exposed broken promise, matching kept promises restore less than half the loss |
| S4 | Overpromising | equal delivery: a government that promised more ends with lower support |
| S4 | Censorship | support falls later but further than under a free press |
| S4 | Implementation gap | lower capacity gives later and smaller delivery with the same decision |
| S5 | J-curve | unrest peaks after satisfaction stalls following a rise, not at its minimum |
| S5 | Threshold cascade | the same trigger fizzles at low organisation and cascades at high organisation |
| S5 | Elections | different seeds give different winners in close races; landslides are seed-stable |
| S6 | Experience | perceived risk rises after a local disaster and decays over quiet years |
| S6 | Shifting baseline | under slow steady warming, the youngest cohort's anomaly is smaller than the oldest's |
| S6 | Worry budget | a recession lowers climate salience at constant climate |
| all | Legitimate priorities | no parameter set in the shipped file makes climate concern the dominant issue for every cohort in every scenario |

### 29.13 Non-goals

-   individual citizen agents (representative citizens are presentation
    only, section 29.7);
-   real ethnic or religious groups; identities are fictional (design v1.0
    §38.1);
-   learning or planning AI for political actors;
-   a regime-change mechanic in the first society release; regime type is
    fixed per scenario until section 27's question is answered;
-   any society-side modifier on planetary state.

## 30. Completing the world

Design v1.1 section 40 adds twelve elements that the world still lacked.
Four of them are physical and join P0 milestones: the solar cycle, volcanic
eruptions, ocean carbonate chemistry and the biodiversity index. The rest are
civilization-side and follow the phases of section 29.1.

### 30.1 Placement

| Addition | Design v1.1 | Stage | Milestone or phase | Owner module |
|---|---|---|---|---|
| Solar cycle | 40.6 | P0 | M12 (forcing) | `planet/orbit` |
| Volcanic eruptions | 40.5 | P0 | M12 forcing, M13 experiment | `planet/geology`, `atmosphere/radiation` |
| Ocean carbonate chemistry, pH | 40.7 | P0 | M12 | `carbon`, `ocean` |
| Biodiversity index (state) | 40.9 | P0 | M10 | `biosphere` |
| Orbital change | 40.6 | P0 | after M13 | `planet/orbit` |
| Local pollution stocks | 40.8 | P1 | with S2 | `hydrology`, `biosphere` |
| Food system | 40.2 | P1 | with S2 | `economy`, `biosphere`, `ocean` |
| Government budget, taxes, prices | 40.1 | P1 | with S2 | `government`, `economy` |
| Indicators, era milestones, endings | 40.4 | P1 | first version with S2 | game layer |
| Earthquakes and tsunamis | 40.5 | P2 | hazards | `planet/geology` |
| Learning debrief | 40.12 | P2 | presentation | presentation |
| Debt, future-generations indicator | 40.1, 40.10 | P3 | with S4 | `government` |
| Voice of the young | 40.10 | P3 | with S5 | `society` |
| Lifestyle norms | 40.11 | P3 | with S3 | `society` |
| Biodiversity functions | 40.9 | P2 | after M10 | `biosphere` |
| Conflict and security | 40.3 | P4 | with S7 | `government` |

### 30.2 Natural forcing: solar cycle, eruptions and orbital change

These are the background against which the human signal must be found
(design v1.1 §40.6). They are forcing, not modifiers, and every event is
recorded in the run manifest.

**Solar cycle.** A periodic modulation of the star's luminosity:

``` text
L(t) = L0 * (1 + a_sun * sin(2π (t - t0) / P_sun))
```

`a_sun` (Earth: about 5e-4), `P_sun` (Earth: about 11 years) and `t0` are
`PlanetParameters`; the period may jitter from cycle to cycle by a seeded
draw of up to ±20 %, fixed at scenario construction. The modulation enters
through the existing sub-step mean insolation (ADR-0006 §4.3), so it is
seasonal and conserved like the rest of the forcing.

**Volcanic eruptions.** Eruptions are discrete events on a volcano list
derived from the plate boundaries of the M2 terrain generator.

``` text
onset:      Poisson process per volcano, rate λ_v from the scenario
size:       truncated power law over eruption magnitude (VEI-like classes)
injection:  stratospheric sulphate mass M_SO4 by size class and latitude
burden:     dB/dt = injection - B / τ_strat,  τ_strat ≈ 1 year
spread:     latitude-band transport toward both hemispheres for
            tropical eruptions, one hemisphere otherwise
forcing:    shortwave reflection from the stratospheric optical depth,
            through the aerosol path of section 9.9
```

Rules:

-   the volcano list, rates and size distribution are scenario data; the
    onset and size draws use the keyed RNG with a reserved stream and are
    fixed when drawn, so a replay reproduces every eruption;
-   a scenario may prescribe dated historical eruptions (Industrial Dawn:
    an 1883 Krakatoa-class event) in place of or alongside the random ones;
-   stratospheric aerosol is a separate tracer from tropospheric pollution
    aerosol, with its own lifetime; neither is folded into CO2;
-   tephra and local ash damage are P2 hazards; P0 carries only the forcing.

**Orbital change.** For long scenarios (Ice Age, design §4.2), the
precession angle, obliquity and eccentricity drift with simplified secular
periods. They update `PlanetParameters` at year boundaries through the
ADR-0004 frames; within a year the orbit is fixed. Short scenarios hold
them constant.

**Earthquakes and tsunamis (P2).** Discrete hazard events on the same plate
boundaries, drawn the same way, with no climate effect: infrastructure
damage, casualties and coastal inundation on the ADR-0005 hypsometry.

### 30.3 Ocean carbonate chemistry

The ocean carbon reservoir of M12 carries two prognostic surface fields per
ocean cell, both `double` and in `SlowState`:

``` text
DIC   dissolved inorganic carbon, mol/kg
ALK   total alkalinity, mol/kg
```

pH, the partial pressure of CO2 at the surface and the aragonite saturation
state Ω are diagnosed from DIC, ALK, temperature and salinity with a
standard simplified carbonate system. The air--sea CO2 flux follows from
the partial-pressure difference and a gas-transfer velocity, so the
buffering (the Revelle factor) emerges rather than being prescribed.
Exchange with the deep ocean uses the deep-ocean reservoir of M11.

Acidification recovers only through slow processes (deep mixing, carbonate
dissolution), so no surface process may reset pH. Ω feeds reefs and the
fisheries of section 30.6.

### 30.4 Biodiversity index

From M10, each region carries a species-richness index per biome
(`float`, 0 to 1 of the biome's potential), in `SlowState`:

``` text
dR/dt = r_rec * (R_pot - R) * habitat * (1 - pressure)  -  δ * pressure * R
pressure = w_hab * habitat_loss + w_temp * thermal_stress
         + w_poll * pollution (P1) + w_harv * overharvest (P1)
extinctions += max(0, R_prev - R) * N_species_biome   when R < R_crit
```

`extinctions` is a monotonic counter per region and biome: it never
decreases, and a test asserts it. Below `R_crit` the biome loses functions
(pollination, pest control, fish recruitment, soil formation) in P2;
before P2, the index is state and diagnostics only.

### 30.5 Pollution stocks

Each pollutant is a physical stock with a source, a transport path and a
decay time. The civilization flux interface of section 18 gains the
sources.

| Stock | Where | Transport | Decay | Exposure output |
|---|---|---|---|---|
| Tropospheric aerosol and smog | atmosphere | winds (section 9.9) | days to weeks | `air_quality` |
| River pollutant load | land cells | routed downstream on the ADR-0005 drainage | first-order per reach | `water_quality` |
| Soil degradation | land cells | none (local) | recovers over decades | yield reduction |
| Nutrient load | land, then coastal ocean | runoff to the river mouth | uptake and burial | coastal `hypoxia` when load and warm water exceed a threshold |
| Toxic waste | land cells | slow leaching | decades or none | local health |

`water_quality` and `hypoxia` join the exposure variables of section 29.4.
Salinisation from irrigation is part of soil degradation and must follow
from the water budget of section 9.10, so it emerges where irrigation
exceeds drainage.

### 30.6 Food system

All flows are physical quantities in the P1 economy, per region:

``` text
diet_c           demand vector over grain, vegetables, meat, dairy, fish
                 (kcal per capita), shifting with income and norms (30.10)
herd_r'          herd_r + births - slaughter, limited by feed and pasture
CH4_livestock    herd * emission factor by animal type    -> section 18 flux
pasture_r        land use, competing with crops and forest (section 9.10)
fertilizer_r     N applied -> yield gain, N2O flux, nutrient runoff (30.5)
food_stock_r     storage with spoilage; trade along the transport network
waste_fraction   loss between field and consumer, falling with infrastructure
```

Fish stocks per ocean region follow logistic growth with depensation, so a
stock below a critical fraction collapses even when fishing stops:

``` text
F' = F + g(T, Ω) * F * (1 - F/K) * (F/F_crit - 1) * Δt - catch
catch = q * effort * F
```

`g` falls with temperature beyond the stock's tolerance and with low Ω
(section 30.3). Food prices from section 30.7 feed cohort needs; food-price
shocks reach grievance through section 29.6.3 without any special case.

### 30.7 Public finance and prices

**Prices.** One price per commodity and region, updated each quarter by a
bounded adjustment:

``` text
p' = p * exp(η * clamp((demand - supply) / supply, -1, 1))
```

with trade along the transport network moving supply toward high prices,
limited by capacity and cost. There are no markets, banks or speculation
(design §17).

**Budget.** Each administration holds accounts per year:

``` text
revenue  = Σ_tax rate_tax * Σ_cohort,band base_tax(c, b)
spending = Σ_line allocation_line   (infrastructure, services, research,
           administration, security, adaptation, debt service)
debt'    = debt + spending - revenue
interest = i_0 + k_debt * debt / output + k_cred * (1 - mean credibility)
```

Taxes act on income bands, so their distribution falls out of section
29.3. A tax change is a `set_policy` command (section 29.9) and passes
through the implementation gap. The credibility term links the promise
ledger to the cost of borrowing.

### 30.8 Conflict and security (P4)

Conflict risk between two societies is a hazard rate:

``` text
h_ab = h_0 * exp( w_water * shared_water_scarcity + w_land * land_scarcity
                + w_refugee * refugee_flow_ab + w_griev * grievance
                - w_legit * legitimacy - w_trade * interdependence_ab
                - w_treaty * treaties_ab + w_mem * past_conflict_ab )
```

Onset is a keyed-RNG draw per year. A conflict destroys a fraction of
infrastructure and harvest in the affected regions, displaces cohorts
through the migration model, emits from destruction and rebuilding, consumes
budget, and suspends climate agreements between the parties. Security
spending lowers onset and damage. There is no combat simulation; the
conflict is a state with an intensity and a duration.

### 30.9 Future generations

**Indicator.** A future-generations indicator, computed at each report
interval and shown in every era report:

``` text
FG = debt / output, depleted non-renewable resources, committed warming
     (section 7.2), crossed tipping elements, extinctions (30.4),
     surface pH change (30.3)
```

It is a vector, not a single number; the presentation may show a weighted
summary, but the weights are data and visible.

**Voice of the young.** Young cohorts whose climate salience is high gain
organisation (section 29.6.8) and unlock specific mobilisation forms:
school strikes, youth movements and, once courts exist in the scenario,
climate lawsuits that force a promise check.

**Discounting.** The discount rate the government uses to appraise
investments is a visible policy parameter, not a constant. Political actors
use their own rates.

### 30.10 Lifestyle norms

Each cohort carries a norm vector (car use, meat share, air travel, housing
size, cycling, reuse), each 0 to 1. Norms diffuse through the influence
network of section 29.6.5 with a conformity term:

``` text
n_c' = n_c + (p_norm + q_norm * n_neighbours) * (target(income, infra, price) - n_c) * Δt
```

`target` depends on income, infrastructure (cycle lanes, transit, housing
stock) and prices. Media can nudge `q_norm`; no command sets a norm. Norms
scale the consumption base of section 29.6.4 and the diet vector of section
30.6. A norm tied to identity freezes like an opinion (section 29.6.5).

### 30.11 Indicators, era milestones and endings

Indicators are derived from state at each report interval and stored in the
sidecar analytics file (section 27), never in the snapshot:

| Indicator | Computed from |
|---|---|
| Living conditions | need satisfaction and health, population-weighted with extra weight on the worst-off income band |
| Planetary state | global warming, committed warming, tipping elements crossed, biodiversity, surface pH |
| Future generations | section 30.9 |
| Resilience | loss and recovery time after shocks: disasters, eruptions, price spikes, conflict |
| Legitimacy | diffuse support and share of cohorts in open protest |

Scenario data defines era milestones (dated report points) and optional
self-set goals as predicates over indicators. A collapse ending fires when
an indicator predicate holds for a set duration; it pauses presentation and
offers a history fork (ADR-0003). None of this affects the tick sequence.

### 30.12 Learning debrief

A curated reference dataset, `data/reference/earth.csv`, holds yearly real
series from 1880 (CO2, global temperature anomaly, population, energy mix,
forest cover) with a source and licence for each column. It is read only by
the presentation layer. The simulation never reads it, and a test asserts
that removing it leaves every state hash unchanged.

### 30.13 Acceptance experiments

| Milestone or phase | Experiment | Acceptance |
|---|---|---|
| M12 | Solar cycle | global mean temperature shows a spectral peak at `P_sun` with the expected small amplitude; energy budget closes |
| M12 | Single large tropical eruption | global cooling peaks within two years and decays with the stratospheric lifetime; both hemispheres cool |
| M13 | Eruption masks trend | under steady CO2 rise, a large eruption produces a temporary pause in warming; the trend resumes |
| M12 | Acidification | doubling CO2 lowers surface pH by roughly 0.3; Ω falls; cooling alone does not restore pH |
| M12 | Revelle factor | emerges in the expected range at preindustrial conditions without being prescribed |
| M10 | Extinction counter | never decreases in any run; the index recovers after pressure ends but the counter does not |
| S2 | Livestock | doubling the meat share of diets raises pasture area and methane flux in proportion |
| S2 | Fishery collapse | sustained overfishing below the critical fraction collapses the stock; it does not recover within decades after fishing stops |
| S2 | Salinisation | irrigation beyond drainage lowers yield over decades; with drainage it does not |
| S2 | Food-price shock | a regional drought raises food prices and grievance in that region without any scripted link |
| S2 | Budget closure | revenue, spending and debt balance exactly every year |
| S4 | Borrowing cost | equal debt, lower credibility gives a higher interest rate |
| S5 | Youth mobilisation | young cohorts mobilise first once climate salience is high |
| S3 | Norm diffusion | a norm with infrastructure support spreads faster than one with media support alone |
| S7 | Conflict risk | shared water scarcity raises onset frequency over many seeds; treaties lower it |
| P2 | Debrief isolation | removing the reference dataset leaves every state hash unchanged |

## 31. Rendering and presentation

Design v1.2 section 42 makes the art direction swappable and requires every
visual style to carry the same information. This section is the contract
for the presentation side: what it may read, how styles plug in, how
readability is tested, and the rendering track that runs alongside P0.

### 31.1 Boundary

``` text
PlanetSim (authoritative state)
     |
     v
StateSnapshot / TerrainSnapshot           (existing, read-oriented)
     |
     v
sim/presentation   ->  VisualFrame        (semantic channels, events)
     |
     v
godot/gdextension  ->  active style pack  ->  screen
```

-   `sim/presentation/` is a C++ library with no Godot dependency. It turns
    snapshots into a `VisualFrame` of **semantic channels** (section 31.2),
    so the mapping from physics to meaning is unit-tested headless;
-   it depends on snapshots only, never on `PlanetState` or a solver;
-   the Godot bridge (today `PlanetMeshNode`) consumes `VisualFrame` and
    hands it to the active style pack. Its existing view modes (terrain,
    plates, crust age, insolation, drainage) become climate-lab overlays
    under section 31.7;
-   nothing in `sim/presentation/` or Godot may write authoritative state;
    a test asserts that rendering a run leaves every state hash unchanged.

Forbidden dependencies, added to section 2:

``` text
sim/presentation -X-> PlanetState, solvers, SocietyState internals
Style packs      -X-> raw snapshot fields (semantic channels only)
```

### 31.2 Semantic channels

Each channel is one value per cell (corners interpolated by the bridge),
normalised to a documented range and meaning. Styles see only these.

| Channel | Range | Meaning | Source fields | From |
|---|---|---|---|---|
| `relief` | m | exaggeration-free elevation | terrain | R0 |
| `surface_class` | weights over water, rock, sand, soil, ice | dominant surface mix | terrain, cryosphere, vegetation | R0 |
| `daylight` | 0--1 | insolation relative to the cell's annual maximum | insolation | R0 |
| `snow_cover` | 0--1 | fraction snow covered | snow water equivalent | R2 |
| `sea_ice` | 0--1 | ice fraction of the ocean tile | sea ice | R2 |
| `temperature_anomaly` | −1 to 1 | against the cell's reference climatology; overlays only | surface temperature | R2 |
| `atmosphere_density` | 0--1 | scattering strength | atmospheric state | R3 |
| `wind` | vector, m/s | for particles and cloud motion | winds | R3 |
| `cloud_cover` | 0--1 | cloud fraction | cloud fields | R4 |
| `cloud_thickness` | 0--1 | optical depth, normalised | cloud fields | R4 |
| `precipitation` | 0--1 | rate, normalised | precipitation | R4 |
| `water_extent` | 0--1 | inundated fraction of the cell, incl. floods and sea level | hydrology, hypsometry | R5 |
| `vegetation_vigour` | 0--1 | leaf area relative to potential | vegetation | R5 |
| `dryness` | 0--1 | soil-moisture deficit | soil moisture | R5 |
| `ocean_tint` | weights over clear, turbid, bloom, hypoxic | water colour class | ocean, nutrients | R6 |
| `current` | vector, m/s | surface current | ocean | R6 |
| `haze` | 0--1 | tropospheric aerosol optical depth | aerosols | R7 |
| `stratospheric_veil` | 0--1 | volcanic optical depth | stratospheric sulphate | R7 |
| `fire` | 0--1 | active fire intensity; burn scar decays separately | fire | R7 |
| `urban`, `farmland`, `mining` | 0--1 | land-use fractions | civilization | R8 |
| `night_lights` | 0--1 | electrified urban activity | civilization, energy | R8 |

Rules:

-   the channel list is append-only and versioned like the field registry;
    a style declares the channel-set version it implements;
-   a channel absent in the current milestone is reported absent, never
    faked; styles must render its absence neutrally;
-   **events** travel separately as a list (kind, position, magnitude, start
    and end tick): eruptions, cyclones, floods, fires, tipping events,
    protests, elections. They are not smoothed (section 31.4).

### 31.3 Style packs

A style pack is a directory `godot/styles/<name>/` with a manifest:

``` text
style.tres
  name, version, author, licence
  channel_set_version           (section 31.2)
  shaders: surface, ocean, atmosphere, clouds, effects
  ramps:   one colour ramp or LUT per scalar channel it uses
  assets:  detail meshes and textures by camera scale
  ui_skin: icons, fonts, panels       (never overlay scales)
  motion:  easing, relief exaggeration, cloud motion speed
  cost_tier: low | medium | high
```

-   loading validates the manifest: a style that does not use every
    required channel of its channel-set version is refused, with the missing
    channels named;
-   switching styles swaps materials and assets only; the bridge keeps the
    current `VisualFrame`, so a switch is instant and has no simulation
    effect;
-   the first two styles are `stylised` (primary) and `map` (flat, symbolic),
    built together in R1 so the abstraction is proven by two very different
    implementations;
-   style packs contain data and shaders only. Scripts in community packs
    are an open question (section 27).

### 31.4 Temporal smoothing

Each scalar channel is smoothed on the presentation side with a display time
constant that grows with the simulation speed:

``` text
c_display' = c_display + (c_frame - c_display) * (1 - exp(-Δt_wall / τ))
τ = max(τ_min, k_ch * simulated_years_per_wall_second)
```

`k_ch` is per channel: snow and vegetation are smoothed more than clouds.
Events bypass smoothing and are drawn at their true time and place. Like all
pacing (section 8), smoothing is presentation and never changes the tick
sequence.

### 31.5 Camera scales and procedural detail

| Scale | Geometry | Detail |
|---|---|---|
| Planet | the cell mesh at the shipped level, corners interpolated | none beyond channels |
| Region | the same mesh, refined in view | procedural detail from channels |
| Local | a generated patch for a few cells | procedural detail and assets |

Procedural detail uses a presentation RNG stream keyed by
`(world_seed, cell_id, detail_kind)`, separate from every simulation stream,
so the same place always looks the same. Detail must respect its cell:
tree density follows `vegetation_vigour`, snow follows `snow_cover`,
buildings follow `urban`. A test samples generated detail and asserts it
never contradicts the channel values beyond a tolerance.

### 31.6 Event staging

The presentation may move the camera, slow presentation time, or pause on an
event, and open the newspaper. Under section 8, any change of simulation
pacing is requested through the existing state-only rules; staging itself is
presentation. Staging intensity is a player setting, including off.

### 31.7 Overlays and knowledge

-   In normal play, data overlays read `EstimatedState` and
    `ObservationState` (sections 6, 17), not `SlowState`. Uncertainty is
    drawn from the per-field uncertainty: fading, hatching or blur, chosen by
    the overlay, not by the style;
-   true-state overlays and the existing debug view modes are available only
    in the climate lab and debug builds;
-   overlay colour scales are fixed, colour-blind-safe and shared by all
    styles; every overlay has a text reading on hover or selection.

### 31.8 Readability test harness

`tests/presentation/` holds the test snapshots of design v1.2 §42.3: for each
signal, a baseline snapshot and a signal snapshot of the same region. For
each style:

1.  render both offscreen at a fixed camera, resolution and lighting;
2.  compute the mean perceptual colour difference (CIEDE2000) over the
    signal region;
3.  repeat on simulated deuteranopia, protanopia and tritanopia, and on
    luminance only;
4.  pass if every signal exceeds the threshold in every variant.

The threshold starts at a mean ΔE of 10 and is recorded with the harness.
The harness runs in CI under a software renderer (for example Mesa llvmpipe
in a virtual framebuffer); results are written to the sidecar analytics
format so differences can be inspected. A style that fails is not shipped.

The channel mapping of section 31.2 has its own headless unit tests in
`sim/presentation`: for each test snapshot, the expected channel moves in the
expected direction by more than a threshold.

### 31.9 Performance budget

-   PC target: 60 frames per second at 1080p on a mid-range graphics card at
    L6, for a `medium` cost-tier style; `high` styles state their own target;
-   per tick, only changed channels are uploaded, as float textures indexed
    by cell, as `PlanetMeshNode` already does for insolation;
-   the render thread never waits for the simulation (section 8); if a frame
    arrives late, the previous `VisualFrame` is drawn;
-   Android gets a `low` cost-tier style, not a reduced simulation.

### 31.10 Rendering track

M14 is replaced by a rendering track that runs alongside the physics. Each
step lands with or after the milestone that provides its fields.

| Track | Content | Needs | Acceptance |
|---|---|---|---|
| R0 | Existing terrain, plates, crust age, insolation, drainage views | M0--M3 | done |
| R1 | `sim/presentation`, `VisualFrame`, channels R0--R2, style packs, `stylised` and `map` styles, switching, readability harness, render-does-not-change-state test | ADR-0018 | both styles pass the R1 signals; switching is instant |
| R2 | Snow, sea ice, temperature-anomaly overlay | M4 | cold and warm years distinguishable in both styles |
| R3 | Atmosphere scattering, wind particles | M5, M6 | wind patterns visible; no frame waits on the simulation |
| R4 | Clouds, precipitation | M7, M8 | cloud bands and a cyclone distinguishable; clouds sit where the fields put them |
| R5 | Rivers, floods, moving coastlines, vegetation, dryness | M9, M10 | drought, flood and deforestation signals pass |
| R6 | Ocean tint, currents | M11 | current patterns and a bloom are distinguishable |
| R7 | Haze, volcanic veil, fire, event staging | M12--M15 | smog, eruption and fire signals pass; staging never changes ticks |
| R8 | Civilization footprint, camera scales, society views, newspaper, reports, debrief | P1 onward | footprint signals pass; detail never contradicts channels |

The old M14 obligation ("render authoritative temperature, wind, currents,
precipitation, soil moisture, vegetation, snow/ice, clouds, SST and energy
imbalance") is met by R2--R7 in the climate lab, and by the overlays of
section 31.7.

### 31.11 Decision record

ADR-0018 (section 26.1) decides the `sim/presentation` library, the channel
registry and its versioning, the style manifest and its validation, the
presentation RNG stream, and the readability threshold. It blocks R1.

### 31.12 Non-goals

-   a photorealistic style before P1;
-   any gameplay difference between styles;
-   scripts in style packs, until section 27's question is answered;
-   presentation-side physics: a style may animate, never simulate.

## 32. The fog of knowledge

Design v1.3 section 44 extends the observation architecture of section 17
into five layers of fog: geographic, observation, understanding, social and
foreign. This section specifies the state behind them, how each layer lifts,
and how fog reaches the screen.

### 32.1 Placement

| Layer | Design v1.3 | Stage | State | Built on |
|---|---|---|---|---|
| Geographic | 44.3 | P1 | `FogState.explored`, guessed map | terrain, transport, budget |
| Observation | 44.4 | P2 | `ObservationState` (section 17), last-seen record | instruments |
| Understanding | 44.5 | P2 | `KnowledgeState` (section 17.1) | knowledge stages |
| Social | 44.6 | P3 | `SocialEstimate` | section 29 |
| Foreign | 44.7 | P4 | `ForeignEstimate` | other civilizations |
| Rendering | 44.8 | R1 (channels), R8 (drawing) | section 31 channels | style packs |

### 32.2 Invariants

-   **Fog has no physical or social effect.** Fog state is consumed by the
    civilization's decisions and by presentation only. Two runs that differ
    only in fog state are bit-identical in `SlowState` and in
    `SocietyState`, until a player or AI decision based on the fog is
    issued as a command. This extends the assertion of section 17.2;
-   **own land is visible.** Natural-view channels (vegetation, snow, water,
    smoke, haze, urban) are never fogged inside the civilization's territory
    or within sight of its settlements. What fog hides there is the
    measured overlay, the history and the cause;
-   **fog is generated deterministically.** Guesses, exploration outcomes and
    estimate errors use the keyed RNG with reserved streams, and a replay
    reproduces them;
-   the climate lab bypasses fog through the separate path of section 17.2.

### 32.3 State

Fog state belongs to a civilization and is snapshotted with the
civilization-side state (ADR-0012, extended by ADR-0019).

``` text
FogState (per civilization)
  explored            per cell, 0..1           geographic knowledge
  last_seen_tick      per cell, SimulationTick last natural observation
  last_seen           per cell, a reduced record of the natural channels
                      at last_seen_tick: snow_cover, vegetation_vigour,
                      water_extent, urban, sea_ice (5 x float)
  territory_mask      per cell, derived from settlements and borders
SocialEstimate (per civilization, per own cohort)
  opinion_estimate, salience_estimate, support_estimate, need_estimate
  estimate_sigma      per quantity
  census_tick, statistics_capacity, polling_capacity
ForeignEstimate (per pair of civilizations)
  emissions_estimate, land_use_estimate, military_estimate, sigma
  monitoring_level    0..1, raised by satellites and agreements
```

At L6 the per-cell part is about 40,962 cells × (1 + 1 + 5) values, under
2 MB per civilization. The guessed map is **not** stored: it is a pure
function of `(world_seed, cell)` (section 32.4), so it costs nothing in a
snapshot and cannot drift.

### 32.4 Guessed map

For unexplored cells, presentation draws a guess instead of the truth:

``` text
guess_elevation(cell) = true_elevation(cell)
                      + A_large * N_large(cell)        (low-order noise on
                                                        the sphere, seeded)
                      + feature_edits(cell)
feature_edits: a small seeded set per continent, drawn from
               {phantom inland sea, coastline shift, merged or split
                mountain ranges, missing river, phantom river}
```

-   amplitudes and feature counts are scenario data;
-   guesses are plausible: the guessed land fraction of each continent stays
    within a set tolerance of the truth, and no phantom feature is placed
    within the civilization's starting territory;
-   when a cell becomes explored, presentation shows the correction (the
    guess fading into the truth) and records a discovery event for the
    newspaper.

### 32.5 Exploration

Exploration is a P1 command and a set of passive sources:

| Source | Reveals | Rate | Risk |
|---|---|---|---|
| `send_expedition(kind, route)` by sea | coast cells along the route, inland to a radius | per sub-step along the route | weather and distance, keyed draw |
| `send_expedition(kind, route)` by land | cells along the route, a radius by terrain | slower in mountains, desert, ice | terrain, supply, keyed draw |
| Trade routes | keeps `explored` and `last_seen` current along the route | continuous | none |
| Settlements and territory | all cells in sight | continuous | none |
| Aerial survey (technology) | wide swaths per flight | fast | small |
| Satellites (knowledge stage, section 17.1) | every cell's geography | complete | none |

Expeditions cost budget (section 30.7) and take simulated time. A lost
expedition reveals nothing beyond where it was lost. `explored` never
decreases; what ages is `last_seen`.

### 32.6 Aging and the last-seen record

Each time a cell is naturally observed (by territory, route, expedition or
satellite imaging), `last_seen` and `last_seen_tick` are refreshed. Outside
current observation, presentation draws `last_seen`, not the truth, faded by
age:

``` text
knowledge_age(cell) = clamp((tick - last_seen_tick) / T_age, 0, 1)
```

So an old map can be dangerously out of date: a coastline mapped before the
sea rose, a forest that has since burned. Measured overlays follow
`ObservationState` and `KnowledgeState` exactly as in section 17.

### 32.7 Social estimates

The government's view of its own cohorts is an estimate, refreshed by its
statistical and polling capacity:

``` text
reported_ci  = x_ci + r_c * (gov_position_i - x_ci)      (preference
                                                          falsification)
r_c          = ρ_censor * censorship + ρ_repr * repression
poll_ci      = reported_ci + bias_reach_c + ε,
               ε ~ N(0, σ_poll / sqrt(sample_c))         (keyed draw)
sample_c     ∝ polling_capacity * reachability_c
reachability = f(urban share, literacy, technology)
census       : population, income and need estimates refreshed every
               census_interval, error σ_census / statistics_capacity
```

-   censorship and repression make people report what the government wants
    to hear, so the estimate worsens exactly when unrest is building (the
    dictator's dilemma of design v1.3 §44.6);
-   election results, protest sizes and revolts are observed exactly when
    they happen; they are how truth arrives;
-   the cohort map, promise tracker and opinion overlays of section 31.7
    show these estimates with their `estimate_sigma`, never `SocietyState`
    itself, outside the climate lab;
-   AI governments and political actors use the same estimates, not the
    truth, so they can be surprised too.

### 32.8 Foreign estimates

Each civilization holds estimates of the others' emissions, land use and
military strength, with an error that shrinks with trade volume, embassies,
intelligence spending and `monitoring_level`. Satellite monitoring of
emissions and forests (from the satellite knowledge stage) raises
`monitoring_level`, which is what makes a climate agreement verifiable: an
agreement's compliance check (phase S7, section 29.1) uses the estimate, not the
truth.

### 32.9 Fog channels

Appended to the semantic channels of section 31.2 (channel-set version
bump); prepared in R1 so styles implement them from the start, drawn in R8:

| Channel | Range | Meaning |
|---|---|---|
| `known` | 0--1 | explored fraction of the cell |
| `guessed` | 0--1 | weight of the guessed map in what is drawn |
| `knowledge_age` | 0--1 | age of the last natural observation |
| `observation_uncertainty` | 0--1 | from `ObservationState`, for overlays |
| `estimate_uncertainty` | 0--1 | per region, from `SocialEstimate`, for society views |

The readability harness of section 31.8 adds five fog signals: unexplored
and guessed, explored but not observed, observed long ago, observed but
uncertain, and observed and understood. Each must be distinguishable from
the others in every style.

### 32.10 Acceptance experiments

| Stage | Experiment | Acceptance |
|---|---|---|
| P1 | No physical effect | runs differing only in fog state are bit-identical in `SlowState` and `SocietyState` before any fog-based command |
| P1 | Own land | natural channels are never fogged inside territory, over a full run |
| P1 | Guess plausibility | for many seeds, guessed land fraction per continent within tolerance; at least one feature edit per continent; none in starting territory |
| P1 | Guess determinism | the same seed gives the same guessed map without any stored state |
| P1 | Exploration | `explored` never decreases; a lost expedition reveals nothing past its loss point |
| P1 | Aging | after a sea-level rise, a coast last seen before the rise is drawn at its old position until revisited |
| P3 | Polling error | estimate error falls with polling capacity, roughly as one over the square root of the sample |
| P3 | Dictator's dilemma | at equal true grievance, higher censorship gives a larger gap between estimated and true support |
| P3 | Truth by events | the election result matches the true vote exactly, whatever the polls said |
| P4 | Verification | with low monitoring, agreement compliance checks miss violations more often; satellites reduce the miss rate |
| R8 | Fog readability | the five fog signals pass in both styles, including colour-blind variants |
