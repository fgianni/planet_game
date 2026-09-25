# Planetary Civilization Simulator

The repository has completed **P0 / M1 — Orbit, sun, day/night and
seasons**, including the ADR-0002/0003 conformance migration. It contains a
standalone C++20 `PlanetSim` library, headless mesh and solar diagnostics,
tests, and an optional Godot 4 presentation adapter. M2 terrain and ocean-mask
work has not started.

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

Every level has exactly twelve pentagons; all other cells are hexagons. The
triangular icosphere remains private construction scaffolding and its face
centroids become the stored dual corners.

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
annual result is approximately `6.2e-7`, while individual sampled phases
remain below approximately `3.1e-5`.

## Optional Godot preview

The default build does not inspect or require Godot. To build the adapter,
use a `godot-cpp` checkout compatible with the installed Godot 4 release:

```bash
cmake -S . -B build-godot \
  -DPLANETSIM_BUILD_TESTS=ON \
  -DPLANETSIM_BUILD_GODOT_EXTENSION=ON \
  -DGODOT_CPP_PATH=/absolute/path/to/godot-cpp
cmake --build build-godot --parallel
godot4 --headless --editor --quit --path godot
godot4 --path godot
```

The one-time headless editor command imports the project and registers the
GDExtension before the scene is run. The extension registers
`PlanetMeshNode`. The included scene builds an L4 preview from immutable
`PlanetMesh` geometry, advances the authoritative integer-tick
`SimulationClock`, and colors each dual polygon from a read-only, versioned
`StateSnapshot.top_of_atmosphere_insolation_W_m2` field. The default preview
advances two simulated hours per wall-clock second and publishes twelve
snapshots per second; both values are exported in `scripts/main.gd`.

The visual sphere is unit-scale; authoritative geometry, time, and forcing use
SI units. The unshaded color ramp is presentation-only—day/night and seasonal
geometry come from PlanetSim.

## M1 architecture

- `PlanetSim` has no Godot dependency.
- `PlanetMesh` owns immutable dual polygon cells, shared edges, CSR adjacency,
  dual corners, local tangent bases, and fixed 256-cell logical blocks. The
  primal triangles exist only while constructing the mesh.
- Evolving fields are separate 64-byte-aligned `Field2D<T>`, layer-major
  `Field3D<T>`, or `EdgeField<T>` arrays indexed by strong IDs.
- `PlanetState` retains a shared immutable mesh handle and owns evolving
  fields.
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
- The Godot target depends on `PlanetSim`; the dependency never points in the
  other direction.

The main decisions are recorded in:

- [simulation modes and performance](docs/decisions/0001-simulation-modes-and-performance.md);
- [mesh topology, resolution, and field layout](adrs/ADR-0002-mesh-and-field-layout.md);
- [determinism, snapshots, and migration](adrs/ADR-0003-determinism-snapshots-migration.md);
- [Keplerian orbit and coordinate frames](docs/decisions/0004-keplerian-orbit-and-coordinate-frames.md).

The precise M1 coordinate and validation conventions are in
[`docs/M1_TECHNICAL_SPEC.md`](docs/M1_TECHNICAL_SPEC.md).

## Current limitations

M1 computes top-of-atmosphere incoming solar only. It does not yet compute
albedo, absorbed shortwave, surface temperature, atmosphere, terrain,
orbital precession or perturbations, persistence, or any M2+ system. The
optional Godot adapter must be compiled against an external matching
`godot-cpp` checkout and is not part of the default headless CI path.
