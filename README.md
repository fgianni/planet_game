# Planetary Civilization Simulator

The repository has completed **P0 / M1 — Orbit, sun, day/night and
seasons**, including the ADR-0002/0003 conformance migration. It contains a
standalone C++20 `PlanetSim` library, headless mesh and solar diagnostics,
tests, and an optional Godot 4 presentation adapter. M2 is in progress: the
finite-volume operators (G2-M2) and the state-partition/persistent-snapshot
foundation are complete; geological terrain and ocean-basin work has not
started.

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

## Persistent snapshots

```bash
./build/planet_cli snapshot write --subdivision 0 --out planet.psnap
./build/planet_cli snapshot inspect planet.psnap
```

The `PSNAP` schema-v1 format stores only authoritative slow state, in stable
field-ID order and layer-major/cell-major order within each field. Its fixed
little-endian representation, canonical manifest, per-field CRC-32C checksums,
and strict reader make equal states byte-identical and reject corrupt or
incompatible files before mutating the destination state. The manifest
records the mesh generator version and a checksum of the cell centres, so a
snapshot never loads onto different mesh geometry, and writes go through a
`.partial` file renamed into place. `tests/data/golden/` holds one snapshot
per schema version. M2 currently writes
uncompressed chunks (`"none"`); compression is deliberately deferred until the
M4 size/ratio measurements in ADR-0003. This persistent format is distinct
from the small in-process `StateSnapshot` used by the presentation adapter.

## Procedural terrain

```bash
./build/planet_cli terrain --subdivision 5 --seed 20260928
./build/planet_cli terrain --subdivision 6 --seed 20260928 \
    --map terrain_L6.csv --snapshot terrain_L6.psnap
./build/planet_cli terrain --subdivision 4 --preset aqua_planet
```

`terrain` builds a planet from a seed (task M2-02): plates grown from spaced
seed cells, Euler-pole motion, convergent/divergent/transform boundaries,
continental and oceanic crust, crust age and an age--depth ocean floor,
mountain belts, trenches, arcs, rifts and passive margins, roughness and a
diffusive erosion approximation. The result is the ADR-0005 slow state: nine
sub-cell elevation quantiles per cell and a global sea level solved for the
target land fraction, with land and ocean fractions derived from them. The
world ocean is the below-sea-level region connected to the deepest cell;
other below-sea-level regions stay dry land until hydrology (M9).

Options: `--preset earth_like|aqua_planet|dead_rock` (default `earth_like`),
`--land-fraction F` (default 0.29), `--plates P` (2--40, default 12),
`--workers W` (default: all cores; results are identical for any count),
`--map FILE.csv` (one row per cell: position, plate, crust type and age,
nearest boundary class and distance, mean, lowest and highest elevation, land
fraction) and `--snapshot FILE.psnap` (the slow state). The printed
diagnostics include plate areas, boundary lengths by class, crust-age ranges,
elevation percentiles, the achieved land fraction and sea level, shelf and
abyssal ocean fractions, and the generation time. Generator constants live in
`GeologyParameters` (`sim/planet/geology/geology_parameters.hpp`); they are
starting values, not calibrated physics. The plate and crust structure
(`GeologyState`) is kept in memory and is not yet persisted.

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
grey), `4` top-of-atmosphere insolation. The simulated day/night is laid over
views 1--3 (`N` toggles it). Drag to rotate, wheel to zoom, `[`/`]` relief
exaggeration, `R` new seed, `P` next preset, `PgUp`/`PgDn` resolution, `Space`
pause, `+`/`-` simulation speed. The same settings can be passed on the
command line, for example `godot4 --path godot -- --seed=42 --subdivision=6
--view=1`.

The visual sphere is unit-scale; authoritative geometry, time, terrain and
forcing use SI units. Colour ramps, relief exaggeration, smooth normals and
the corner averaging that smooths the terrain view are presentation only;
zooming in shows the model's real resolution, with no invented detail.
Geometry is built once per planet or view; each update uploads only the
snapshot's per-cell insolation to a texture the shader reads, which keeps L7
(2.9 million vertices) at the display refresh rate.

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
  `SlowState` currently owns hypsometry and global sea level; `FastState` is
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
[`docs/DEVELOPMENT_SPEC_v0_4.md`](docs/DEVELOPMENT_SPEC_v0_4.md) and the design
document is
[`docs/planetary_civilization_simulator_design_v0_9.docx`](docs/planetary_civilization_simulator_design_v0_9.docx),
whose figures are also in [`docs/pngs/`](docs/pngs/).
Accepted decision records take precedence over the specification where they
conflict.

The main decisions are recorded in:

- [simulation modes and performance](docs/decisions/0001-time-acceleration.md);
- [mesh topology, resolution, and field layout](docs/decisions/0002-mesh-and-field-layout.md);
- [determinism, snapshots, and migration](docs/decisions/0003-determinism-snapshots-migration.md);
- [Keplerian orbit and coordinate frames](docs/decisions/0004-keplerian-orbit-and-coordinate-frames.md);
- [coastlines and drainage](docs/decisions/0005-coastlines-and-drainage.md).

The precise M1 coordinate and validation conventions are in
[`docs/M1_TECHNICAL_SPEC.md`](docs/M1_TECHNICAL_SPEC.md).

## Current limitations

M1 computes top-of-atmosphere incoming solar only. M2 now provides the
finite-volume operators, state partitions, base persistent snapshots and
procedural plate-scale terrain with sea level, but does not yet compute
drainage (depression filling, routing, basins; task M2-03), albedo, absorbed
shortwave, surface temperature, atmosphere, orbital precession or
perturbations. Geology is generated once and is not time-evolving, and
`GeologyState` is not persisted. Simulation-mode
scheduling, run manifests and replay, compressed/delta snapshots, autosaves,
and schema migrations remain assigned to later tasks and milestones. Tracer
advection and the placement of vector fields (cell centres or edge normals)
are left to the first milestone that transports them. The two-point
Laplacian's pointwise truncation error does not converge next to the pentagons
or along the icosahedron's edges, although discrete solutions do (ADR-0002
§9). The
optional Godot adapter must be compiled against an external matching
`godot-cpp` checkout and is not part of the default headless CI path.
