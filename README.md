# Planetary Civilization Simulator

The repository is currently at **P0 / M0 — Icosphere and simulation
skeleton**. It contains a standalone C++20 `PlanetSim` library, a headless
mesh-diagnostics CLI, tests, and an optional Godot 4 presentation adapter.
No M1 orbit or solar physics is implemented.

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

The command reports counts, construction time, spherical area totals and
error, cell-area and edge-length ranges, center normalization, adjacency
checks, and non-finite geometry. M0 uses exact spherical-triangle area and
accepts a global relative area error no larger than `5e-14` in the reference
CLI and conservation test.

Supported reference test levels are L0 through L6:

| Level | Triangular cells | Indexed vertices |
|------:|-----------------:|-----------------:|
| 0 | 20 | 12 |
| 1 | 80 | 42 |
| 2 | 320 | 162 |
| 3 | 1,280 | 642 |
| 4 | 5,120 | 2,562 |
| 5 | 20,480 | 10,242 |
| 6 | 81,920 | 40,962 |

## Optional Godot preview

The default build does not inspect or require Godot. To build the adapter,
use a `godot-cpp` checkout compatible with the installed Godot 4 release:

```bash
cmake -S . -B build-godot \
  -DPLANETSIM_BUILD_TESTS=ON \
  -DPLANETSIM_BUILD_GODOT_EXTENSION=ON \
  -DGODOT_CPP_PATH=/absolute/path/to/godot-cpp
cmake --build build-godot --parallel
godot4 --path godot
```

The extension registers `PlanetMeshNode`. The included scene builds an L4
preview from immutable `PlanetMesh` geometry and colors each triangular cell
from a read-only demo `StateSnapshot` scalar. The visual sphere is unit-scale;
the authoritative mesh geometry and diagnostics retain the requested SI radius.

## M0 architecture

- `PlanetSim` has no Godot dependency.
- `PlanetMesh` owns immutable indexed vertices and triangular cell geometry.
- Evolving fields are separate dense `Field<T>` arrays indexed by
  `CellId`.
- `PlanetState` retains a shared immutable mesh handle and owns evolving
  fields.
- `StateSnapshot` copies read-oriented presentation data across the client
  boundary.
- The Godot target depends on `PlanetSim`; the dependency never points in the
  other direction.

The mesh representation and geometry conventions are recorded in
[`docs/decisions/0001-primal-triangular-icosphere.md`](docs/decisions/0001-primal-triangular-icosphere.md).

## Current limitations

M0 provides topology and simulation scaffolding only. It has no timestepped
physical process, persistence format, terrain, atmosphere, orbit, insolation,
or other M1+ system. The optional Godot adapter must be compiled against an
external matching `godot-cpp` checkout and is not part of the default
headless CI path.
