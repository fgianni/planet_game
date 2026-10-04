# Planetary Civilization Simulator

The repository has completed **P0 / M5 — Atmosphere and pressure**, on top
of M4's snow, sea ice and diffusive heat transport, M3's surface energy
columns and M2's geological planet, terrain and ocean basins. It contains a
standalone C++20 `PlanetSim` library; headless mesh, solar, terrain,
drainage, calendar, thermal and atmosphere diagnostics; recorded and
replayable runs; tests; and an optional Godot 4 presentation adapter.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler (GCC 11+, Clang 14+, or a comparable compiler)
- Godot 4 and a matching `godot-cpp` checkout only for the optional visual
  demo

The default configuration has no Godot or third-party test dependency.

## Build and test

```bash
cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Warnings are errors by default. To run the same suite with AddressSanitizer
and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitize \
  -DPLANETSIM_BUILD_TESTS=ON \
  -DPLANETSIM_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

On Linux 6.x kernels with 32-bit mmap randomization, sanitizer binaries built
by Clang 14–17 can hang at startup. Run them without address-space
randomization, for example `setarch -R ctest --test-dir build-sanitize`.

## Continuous integration

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push to
`main` and every pull request:

- GCC and Clang, each in Debug and Release: build with warnings as errors and
  run the full `ctest` suite; Release also prints L6 mesh and solstice solar
  diagnostics;
- Clang with AddressSanitizer and UndefinedBehaviorSanitizer;
- an L2–L6 operator validation report whose per-cell error map is uploaded as
  the `operator-error-map` artifact;
- the ADR-0002 floating-point policy check, which fails if any translation
  unit lacks an effective `-ffp-contract=off` or carries a fast-math flag:

```bash
python3 tools/ci/check_fp_flags.py build
```

## Headless mesh diagnostics

```bash
./build/planet_cli mesh --subdivision 6 --radius 6371000
```

The command reports dual-cell, corner, and shared-edge counts; construction
time; spherical area totals and error; cell-area, edge-length, and
centroid-distance ranges; basis normalization; adjacency checks; memory use;
and non-finite geometry. M0 partitions exact spherical-triangle area among the
dual polygons and accepts a global relative area error no larger than `5e-14`
in the reference CLI and conservation test.

Supported reference test levels are L0 through L6:

| Level | Dual cells | Dual corners |
|------:|-----------:|-------------:|
| 0 | 12 | 20 |
| 1 | 42 | 80 |
| 2 | 162 | 320 |
| 3 | 642 | 1,280 |
| 4 | 2,562 | 5,120 |
| 5 | 10,242 | 20,480 |
| 6 | 40,962 | 81,920 |

Every level has exactly twelve pentagons; all other cells are hexagons. Measured
relative area closure at Earth radius is:

| Level | Relative area error |
|------:|--------------------:|
| 0 | 1.2253352946929809e-16 |
| 1 | 0 |
| 2 | 1.2253352946929809e-16 |
| 3 | 1.2253352946929809e-16 |
| 4 | 1.2253352946929809e-16 |
| 5 | 1.2253352946929809e-16 |
| 6 | 1.2253352946929809e-16 |

All levels remain below the `5e-14` test gate. The triangular icosphere
remains private construction scaffolding. Its face
circumcentres become the stored dual corners, and 20 Lloyd iterations per
subdivision level make the cell centres a spherical centroidal Voronoi
tessellation (ADR-0002 §4.1).

To run the ADR-0002 V7 ordered-versus-naive neighbor-sweep benchmark in an
optimized build:

```bash
./build-release/planet_cli mesh --subdivision 6 --benchmark-layout \
  --benchmark-iterations 1000
```

## Headless solar diagnostics

```bash
./build/planet_cli solar --subdivision 5 --time-days 0
./build/planet_cli solar --subdivision 5 --time-days 91.31055
```

The first command evaluates the default epoch, defined as northern vernal
equinox and periapsis; the second is near northern summer solstice. Requested
days are rounded to the nearest authoritative one-minute simulation tick. The
report includes mean/eccentric/true anomaly, orbital distance, inverse-square
incident stellar flux, solar declination, body-fixed sun direction,
illuminated/night cell counts, area-weighted incoming power, `S(d)/4`
quadrature error, non-finite values, and night-side leakage.

Insolation is sampled at cell centers. The L5 annual-balance test permits a
conservative relative quadrature error of `5e-4`; the current 48-sample
annual result is approximately `6.0e-7`, while individual sampled phases
remain below approximately `2.1e-5`.

Climate mode is forced by the **sub-step mean** instead (ADR-0006 §4.3):

```bash
./build/planet_cli solar --subdivision 5 --substep 3
```

prints the sub-step's span and its global, zonal (10° bands) and extreme mean
insolation. The diurnal cycle is averaged analytically (daily-mean insolation
from the sunrise hour angle) and the month's motion along the orbit by
16-point Gauss-Legendre quadrature in time. Over a year the twelve sub-steps
carry the analytic annual mean energy to 1.6e-8 relative; the L5 mesh
reproduces it to 2.4e-8.

## Headless operator validation

```bash
./build/planet_cli operators --min-subdivision 2 --max-subdivision 6 \
  --error-map operator_error_map.csv
```

The command validates the finite-volume operators of
`sim/planet/operators/finite_volume.hpp` (least-squares gradient, divergence of
edge fluxes, and two-point Laplacian) against analytic spherical-harmonic
fields at each level. It reports relative L2 and maximum errors with the twelve
pentagons excluded, convergence orders between levels, pentagon errors, seam
versus interior maxima along the icosahedron's edges, the ADR-0002 V4
non-divergent-flux and global-balance residuals, and the solution error of a
discrete Poisson problem, which is the Laplacian's accuracy measure. The
optional CSV holds per-cell normalized errors at the finest level for error
maps. At L5→L6 the gradient, divergence and Poisson solution converge at
second order in L2; see ADR-0002 §9 for the full record.

The same command validates the C-grid of ADR-0011
(`sim/planet/operators/c_grid.hpp`, task M6-01): velocities normal to the
edges, vorticity at the corners, the TRiSK tangential velocity and kinetic
energy, and the Perot reconstruction of cell vectors. It prints the discrete
identities (antisymmetric weights, a Coriolis term that does no work, the
steady-geostrophic-mode condition), all at rounding, and each operator's
errors and orders. The reconstruction and gradients converge at second
order; TRiSK's tangential velocity and kinetic energy keep a bounded error in
the ring around each pentagon, and the vorticity is first order
(ADR-0011 §12).

## Shallow-water core

```bash
./build/planet_cli shallow-water --case 2 --subdivision 5 --days 5 --workers 4
./build/planet_cli shallow-water --case 5 --subdivision 5 --days 15 \
    --reference-subdivision 6
```

The horizontal core of the winds (ADR-0011, task M6-02) is TRiSK's
shallow-water model on the C-grid, stepped by RK3. It runs Williamson et
al.'s (1992) test 2 (steady geostrophic flow about a tilted axis, against
its exact solution) and test 5 (flow over an isolated mountain, against a
run on a finer mesh). It reports the step count, mass, energy and
potential-enstrophy changes, and the thickness errors. Mass is exact, test 2
converges at second order in L2 (3.5e-5 at L6 after 5 days), and the energy
error is RK3's alone. `--damping-hours H` adds ∇⁴ hyperviscosity that damps
the grid-scale mode in about H hours.

## Winds

```bash
./build/planet_cli dynamics --test held-suarez --subdivision 5 --layers 5 \
    --days 1000 --average-days 800 --damping-hours 8
./build/planet_cli dynamics --test rest --subdivision 5 --lapse-rate 6.5 \
    --orography-step 800
./build/planet_cli reference --subdivision 4 --days 365 --average-days 300
```

Reference mode resolves the winds (ADR-0011, task M6-03).
- **The core.** The hydrostatic primitive equations on the atmosphere's σ
  layers and the C-grid, with surface pressure evolving and RK3 steps
  within each 10-minute step. Its vertical discretisation conserves total
  energy exactly in space.
- **What it sees.** The winds see the terrain smoothed until neighbouring
  cells differ by at most 800 m; the column physics keeps the true heights.
  They feel bulk surface drag and grid-scale hyperviscosity, whose kinetic
  energy returns as heat.
- **State.** They live in the fast state, the first field there. Climate
  mode releases them, and they are never saved.
- **`dynamics`** runs the dry benchmarks. Held–Suarez at L5 with five layers
  gives 35 m/s jets at 47° over trades, mid-latitude westerlies of 8 m/s
  and polar easterlies, none of them imposed.
- **`reference`** runs the Earth-like planet with its physics. Mass is
  exact, and once spun up the energy drifts by 4e-8 per year.

Climate mode still carries heat by diffusion; its balanced circulation is
the next task (M6-04).

## Persistent snapshots

```bash
./build/planet_cli snapshot write --subdivision 0 --out planet.psnap
./build/planet_cli snapshot inspect planet.psnap
./build/planet_cli history --subdivision 6 --decades 10
```

The `PSNAP` format (schema v5: M3 added the surface temperatures and the
`double` ocean mixed layer, M4 the snow and sea-ice reservoirs, M5 the
atmosphere, whose layer count each file states) stores only
authoritative slow state, in stable
field-ID order and layer-major/cell-major order within each field. Its fixed
little-endian representation, canonical manifest, per-field CRC-32C checksums,
and strict reader make equal states byte-identical and reject corrupt or
incompatible files before mutating the destination state. The manifest
records the mesh generator version and a checksum of the cell centres, so a
snapshot never loads onto different mesh geometry, and writes go through a
`.partial` file renamed into place. Older files load through an ordered
chain of named migration steps, one per schema version (ADR-0003 §3.6); the
reader reports the steps it applied. `tests/data/golden/` holds one snapshot
per schema version and one per codec. Chunks are compressed with zstd after
grouping each float's bytes (`shuffle-zstd`; the system `libzstd` is the
project's one third-party library); uncompressed files still load. A
*history* (ADR-0003 §3.5) is a directory of snapshots in which most are
deltas: only the fields that changed since the parent, as compressed XOR, so
the static terrain is never repeated; a full snapshot is rewritten every
eight deltas, and a fork is simply a save from an earlier snapshot. At L6 a
full snapshot is about 1.9 MB and writes in about 8 ms, a decade delta about
a third of that. This persistent format is distinct from the small
in-process `StateSnapshot` used by the presentation adapter.

## Procedural terrain

```bash
./build/planet_cli terrain --subdivision 5 --seed 20260928
./build/planet_cli terrain --subdivision 6 --seed 20260928 \
    --map terrain_L6.csv --snapshot terrain_L6.psnap
./build/planet_cli terrain --subdivision 4 --preset aqua_planet
```

`terrain` builds a planet from a seed (tasks M2-02 and M2-03): plates grown
from spaced seed cells, Euler-pole motion, convergent/divergent/transform
boundaries, continental and oceanic crust, crust age and an age--depth ocean
floor, mountain belts, trenches, arcs, rifts and passive margins, roughness
and a diffusive erosion approximation. The result is the ADR-0005 slow state:
nine sub-cell elevation quantiles per cell and a global sea level solved for
the target land fraction, with land and ocean fractions derived from them.
Elevations are then re-datumed so that the generated sea level is 0 m and
every stored elevation is a height above sea level.
The world ocean is the below-sea-level region connected to the deepest cell;
other below-sea-level regions stay dry land until hydrology (M9). It then
derives a deterministic static drainage graph by priority-filling depressions,
routing slopes and flats, assigning terminal basins, and accumulating land
area into catchments. This topology does not yet simulate water flow.

Options: `--preset earth_like|aqua_planet|dead_rock` (default `earth_like`),
`--land-fraction F` (default 0.29), `--plates P` (2--40, default 12),
`--workers W` (default: all cores; results are identical for any count),
`--map FILE.csv` (one row per cell: position, plate, crust type and age,
nearest boundary class and distance, mean, lowest, highest and drainage
elevation, filled elevation, land fraction, downstream cell, basin,
depression and catchment area) and `--snapshot FILE.psnap` (the slow state).
The printed
diagnostics include plate areas, boundary lengths by class, crust-age ranges,
elevation percentiles, the achieved land fraction and sea level, shelf and
abyssal ocean fractions, drainage outlets, basins and depressions, catchment
closure, maximum fill depth, graph validity, and generation timings.
Generator constants live in
`GeologyParameters` (`sim/planet/geology/geology_parameters.hpp`); they are
starting values, not calibrated physics. The plate and crust structure
(`GeologyState`) is kept in memory and is not yet persisted.

## Simulation modes and the climate calendar

```bash
./build/planet_cli calendar --year 0
./build/planet_cli calendar --from-tick 500000
```

Climate mode, the normal mode of play, steps twelve times per orbital year
(ADR-0006). Sub-step *k* begins at the first one-minute tick at or after the
orbit reaches mean anomaly 2πk/12 from periapsis, so steps are 43,829 or
43,830 ticks long for the default Earth, never drift, and mean the same part
of the orbit in every scenario. The command prints twelve sub-steps: index,
month, begin and end tick, length and begin day.

`Scheduler` (`sim/core/scheduler/scheduler.hpp`) advances the clock one step
at a time in the current `SimulationMode`: to the next sub-step boundary in
climate mode, or by the ten-tick reference step. Registered processes run in
registration order, only in their mode, reference processes at a fixed
cadence. Mode changes are requested, take effect at the next step and are
logged; `run_until` never splits a step, so pacing cannot change the tick
sequence. Weather windows are not implemented yet (ADR-0001 §8).

## Surface energy

```bash
./build/planet_cli thermal --preset earth_like
./build/planet_cli thermal --preset dead_rock --years 20
./build/planet_cli thermal --calibrate 288 --calibrate-gradient 42
```

Every cell has a land tile and an ocean tile, each a two-layer column
(surface and ground, or mixed layer and deep ocean) that absorbs `(1 − α) Q`
and radiates `ε σ T⁴`: to space through an optional grey layer of
emissivity `g` (ADR-0007) on the planets without air, or into the cell's
atmospheric column (ADR-0010, below). Each step is backward Euler, stable for a whole
climate sub-step, and its budget closes to rounding. The surface is the first
scheduler process: climate steps use the sub-step mean insolation, reference
steps the instantaneous insolation. The command spins a generated planet up
from radiative equilibrium and prints the last year's global, land and ocean
mean temperature, energy balance, transport, zonal means and the cost per
sub-step.

Until the winds of M6, heat moves by diffusion (ADR-0009), on the mesh one
level coarser, solved implicitly (Newton with a multigrid-preconditioned
conjugate-gradient solve) so that energy closes exactly and the monthly
step stays stable. On the Earth-like planet it diffuses the atmospheric
columns' mean potential temperature and enters their bottom layer. Dead rock
(no tilt) and the aqua planet, which have no atmosphere, are experiments A
and B of the specification.

Snow lies on the land tile (ADR-0008). Until the atmosphere supplies
moisture, precipitation is a prescribed forcing, zero by default:

```bash
./build/planet_cli thermal --precipitation 1e-5
```

falls as snow where the land surface is at or below 0 °C, raises the tile's
albedo with snow cover, and melts with latent heat when the surface would
warm past 0 °C. Energy (with the latent term) and water close to rounding.
With the transport, snow now clears seasonally from about 15 % of land
cells, while high latitudes still accumulate it.

Sea ice forms on the ocean tile when the mixed layer would cool below
−1.8 °C (ADR-0008): zero-heat-capacity floes whose surface balances
sunlight, emission, heat conducted from the freezing water below and the
air, melting at 0 °C at the top and growing or melting at its base. Ice
thinner than half a metre covers part of the tile as floes of that
thickness, with leads of open water between them that freeze or melt ice,
so the tile responds continuously as the last ice goes. The mass is solved
implicitly, so a month-long step stays stable. `planet_cli thermal` prints
the year's sea-ice extent by hemisphere: on the Earth-like planet northern
sea ice spans about 6–9.5 million km² over the year; the cycle is stationary
but not periodic. The scheduler accumulates a monthly
climatology (means and variances per month) over a run.

## Atmosphere

```bash
./build/planet_cli atmosphere --subdivision 5 --layers 3
./build/planet_cli thermal --layers 5
```

The Earth-like planet has an atmosphere of three equal-mass layers per cell
(ADR-0010; five also work, and the layer count is a scenario entry recorded
in the run manifest and in every snapshot). Each column starts at
hydrostatic rest, its surface pressure the reference sea-level pressure
reduced to the cell's height, so high ground carries less air (98.9 kPa
mean, 45 kPa on a 6 km plateau). The layers exchange grey longwave with
each other, the surface and space, with an optical depth that falls with
pressure, receive sensible heat from the surface, and mix convectively
wherever they cool faster than 6.5 K/km. The column and its land and ocean
tiles are solved together, implicitly, every step, and the energy budget of
surface and air closes to rounding. Nothing imposes a lapse rate on the
surface, yet a dome 4 km high cools at about 6 K/km.

The optical depth `τ₀ = 1.3581` and the transport coefficient
`D = 0.6371 W/m²/K` are calibration constants fitted together, with sea ice
active, to a 288 K global mean and Earth's 42 K equator-to-pole difference;
the poleward transport they imply peaks at 3.8 PW, about 70 % of Earth's.
Sunlight still passes through the air unabsorbed, and there is no water
vapour, cloud or wind yet (M6–M8).

## Recorded runs and replay

```bash
./build/planet_cli run --subdivision 5 --years 10 \
    --command 600000,set_solar_luminosity_factor,1.02 --manifest run.prun
./build/planet_cli replay run.prun
./build/planet_cli run --subdivision 5 --years 250 \
    --min-years-per-minute 20 --max-seconds 240
```

`run` builds a scenario (`--preset`, `--seed`, `--subdivision`,
`--spin-up-years`, `--initial-mode`), steps it on the scheduler for whole
orbital years and records it in a `PRUNv1` run manifest (ADR-0003 §3.3): the
build, the scenario and its hash, every command with the tick at which it
took effect, and an XXH3 hash of the slow state at tick 0 and at the start
of each orbital year. Commands are `TICK,TYPE,PAYLOAD`: `set_mode` with
`climate` or `reference`, and `set_solar_luminosity_factor` with a multiple
of the scenario's luminosity. A command takes effect at the next step
boundary; in climate mode that is the next monthly sub-step. `replay`
re-simulates from the manifest alone, on any worker count, and reports
either `replay matched` or the first tick at which the state diverged. The
same build replays bit for bit; another build is only statistically
equivalent, and `replay` says so. `run` also prints the stepping rate in
simulated years per minute; `--min-years-per-minute` and `--max-seconds` turn
it into the ADR-0001 performance gate that CI applies to 250-year runs at L5
and L6.

## Optional Godot preview

The default build does not inspect or require Godot. To build the adapter,
use a `godot-cpp` checkout and bindings generated from the installed Godot 4
release (godot-cpp has no branch per 4.x minor release after 4.5):

```bash
git clone --depth 1 --recursive https://github.com/godotengine/godot-cpp.git ../godot-cpp
(cd ../godot-cpp && /path/to/godot4 --headless --dump-extension-api)
cmake -S . -B build-godot \
  -DPLANETSIM_BUILD_GODOT_EXTENSION=ON \
  -DGODOT_CPP_PATH=$PWD/../godot-cpp \
  -DGODOTCPP_CUSTOM_API_FILE=$PWD/../godot-cpp/extension_api.json
cmake --build build-godot --parallel
godot4 --headless --editor --quit --path godot
godot4 --path godot
```

The one-time headless editor command imports the project and registers the
GDExtension. The extension registers `PlanetMeshNode`, which generates a
planet with PlanetSim (default L6, about 112 km cells; `PgUp` reaches L7 at
about 56 km; seed 20260928, `earth_like`), reads the
generated terrain once through a versioned `TerrainSnapshot` and the orbit and
insolation every update through `StateSnapshot`, and draws the dual cells
with exaggerated relief (the sea surface is flat at the solved sea level).
Views: `1` terrain (elevation and land/ocean from the ADR-0005 land
fraction), `2` plates with convergent (red), divergent (blue) and transform
(green) boundaries, `3` crust age (young ocean red, old blue; continents
grey), `4` top-of-atmosphere insolation, and `5` static drainage. The drainage
view colours land by logarithmically scaled upstream catchment area, marks
filled depressions in magenta, shows coastal outlets in pale cyan, and draws
each downstream edge above the surface. These are potential routing paths,
not simulated rivers. The simulated day/night is laid over views 1--3
(`N` toggles it); data views 4--5 are self-lit. Drag to rotate, wheel to zoom,
`[`/`]` relief exaggeration, `R` new seed, `P` next preset,
`PgUp`/`PgDn` resolution, `Space` pause, `+`/`-` simulation speed. The same
settings can be passed on the command line with zero-based view indices, for
example `godot4 --path godot -- --seed=42 --subdivision=6 --view=4` opens the
drainage view.

The visual sphere is unit-scale; authoritative geometry, time, terrain and
forcing use SI units. Colour ramps, relief exaggeration, smooth normals and
the corner averaging that smooths the terrain view are presentation only;
zooming in shows the model's real resolution, with no invented detail.
Geometry is built once per planet or view; each update uploads only the
snapshot's per-cell insolation to a texture the shader reads, which keeps L7
(2.9 million vertices) at the display refresh rate.

The versioned terrain snapshot also carries derived downstream, basin,
depression and catchment values for the drainage view. It remains an
in-process presentation object: these values are not registered fields and do
not alter persistent `PSNAP` files.

## Architecture

- `PlanetSim` has no Godot dependency.
- `PlanetMesh` owns immutable dual polygon cells, shared edges, CSR adjacency,
  dual corners, local tangent bases, and fixed 256-cell logical blocks. The
  primal triangles exist only while constructing the mesh.
- Finite-volume operators (divergence of edge fluxes, least-squares
  gradient, two-point Laplacian) run over the fixed logical blocks and are
  bit-identical for any worker count.
- Evolving fields are separate 64-byte-aligned `Field2D<T>`, layer-major
  `Field3D<T>`, or `EdgeField<T>` arrays indexed by strong IDs.
- `PlanetState` retains a shared immutable mesh handle. Its authoritative
  `SlowState` owns hypsometry, global sea level, the four surface
  temperatures, snow and sea-ice mass, and the atmosphere's surface
  pressure and layer temperatures; `FastState` is
  optional and lazily allocated, `Climatology` is derived, and forcing remains
  outside the persistent partition.
- The mesh is body-fixed with geographic north on `+Z`; physical rotation and
  a fixed Keplerian orbit are derived from simulation time and planet/star
  parameters.
- Latitude, longitude, and stable right-handed local East/North/Up bases are
  derived from authoritative 3D surface normals.
- `ForcingState` owns cell-centered top-of-atmosphere insolation in `W/m²`.
- The authoritative clock is a signed 64-bit count of one-minute ticks;
  physical seconds are derived, never accumulated.
- Fixed logical blocks, fixed-order reductions, keyed random streams, and
  disabled floating-point contraction support same-build thread-count
  determinism.
- `StateSnapshot` has an explicit schema version and stable field ID, and
  copies read-oriented orbital and forcing data across the client boundary.
- `SnapshotFile` writes and validates the separate persistent `PSNAP` schema,
  whose current payload is exactly the registered slow-state fields.
- The Godot target depends on `PlanetSim`; the dependency never points in the
  other direction.

The development specification is
[`docs/DEVELOPMENT_SPEC_v0_7.md`](docs/DEVELOPMENT_SPEC_v0_7.md) and the design
document is
[`docs/planetary_civilization_simulator_design_v1_2.docx`](docs/planetary_civilization_simulator_design_v1_2.docx),
whose figures are also in [`docs/pngs/`](docs/pngs/).
Accepted decision records take precedence over the specification where they
conflict.

The main decisions are recorded in:

- [simulation modes and performance](docs/decisions/0001-time-acceleration.md);
- [mesh topology, resolution, and field layout](docs/decisions/0002-mesh-and-field-layout.md);
- [determinism, snapshots, and migration](docs/decisions/0003-determinism-snapshots-migration.md);
- [Keplerian orbit and coordinate frames](docs/decisions/0004-keplerian-orbit-and-coordinate-frames.md);
- [coastlines and drainage](docs/decisions/0005-coastlines-and-drainage.md);
- [seasonal climate-mode steps](docs/decisions/0006-seasonal-climate-steps.md);
- [surface energy columns](docs/decisions/0007-surface-energy-columns.md);
- [snow, sea ice and the ice–albedo feedback](docs/decisions/0008-snow-and-sea-ice.md);
- [diffusive horizontal heat transport](docs/decisions/0009-diffusive-heat-transport.md);
- [the layered atmosphere](docs/decisions/0010-layered-atmosphere.md).

The precise M1 coordinate and validation conventions are in
[`docs/M1_TECHNICAL_SPEC.md`](docs/M1_TECHNICAL_SPEC.md).

## Current limitations

M2 provides the finite-volume operators, state partitions, persistent
snapshots, procedural plate-scale terrain with sea level, and static drainage
topology; M3 surface temperatures from radiative columns, with recorded,
replayable runs; M4 snow, sea ice and their albedo feedback; M5 a layered
atmosphere with pressure, grey longwave and convection; M6 (in progress)
winds in reference mode. Climate mode has no winds yet, so heat there still
moves by a calibrated diffusion. There is no water vapour or cloud: sunlight
reaches the surface unabsorbed, and precipitation is a prescribed forcing. Dynamic runoff, discharge and lake water balance,
orbital precession and perturbations are not yet computed. Geology and
drainage are generated once and are not time-evolving, and `GeologyState` is
not persisted. Autosaves remain assigned to a later milestone. Velocities
live normal to the edges (ADR-0011); tracer advection arrives with
humidity (M7). The two-point
Laplacian's pointwise truncation error does not converge next to the pentagons
or along the icosahedron's edges, although discrete solutions do (ADR-0002
§9). The
optional Godot adapter must be compiled against an external matching
`godot-cpp` checkout and is not part of the default headless CI path.
