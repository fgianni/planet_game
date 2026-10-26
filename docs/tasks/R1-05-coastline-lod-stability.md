# Proposed R1-05 — Coastline-stable terrain LOD

- **Track:** rendering, following R1-04 (ADR-0022 §4.8–4.9)
- **Status:** proposed, not implemented (2026-10-24)
- **Decision gate:** amend ADR-0022 before changing the terrain LOD contract.
  No PlanetSim solver, terrain field meaning or PSNAP change is permitted.

## Measured problem

`tools/render/coast_lod_probe.cpp` compares the existing filtered mean-height
surface with the detailed rank/hypsometry surface in coastal L4 cells, seed
20260928. The first detailed cube tile is level 2. Of 532 sampled points,
100 (**18.8 %**) change land/water classification at the handoff; the mean
height difference is **365 m**, maximum **2467 m**. This is a change in the
presentation surface, not in the authoritative land fraction or sea level.
It is too large to hide with a thinner shader edge or a colour adjustment.

Build and run the diagnostic from the repository root:

```bash
c++ -std=c++20 -O3 -ffp-contract=off -I . \
  tools/render/coast_lod_probe.cpp build-godot/libPlanetSimPresentation.a \
  build-godot/libPlanetSim.a -lzstd -pthread -o build-godot/coast_lod_probe
./build-godot/coast_lod_probe
```

## Decision needed

Two credible routes affect the already accepted R1-04 tile contract:

1. A level-independent, spatially referenced coast mask and compatible
   geometry at all visible tile resolutions. This could keep the shore fixed
   but costs additional memory, generation and a rule for where the mask and
   hypsometric rank meet.
2. A band-limited hierarchy of the *same* relief with geomorphing at LOD
   transitions. This avoids a sudden mean-to-detail jump but must preserve
   neighbouring tile seams, the V4 hypsometry bound, deterministic tile
   generation and the existing tile budget.

Do not choose one by changing `tile_filtered_cells` alone: moving the abrupt
switch does not remove it, and sampling all high-frequency relief on coarse
tiles aliases the coast. The ADR must specify the coastline error metric,
transition, cost ceiling and whether the rendered coast is defined by the
full-rank field or by a separately filtered representation.

## Acceptance after the decision

- A scripted zoom holds the same coastal focus, freezes simulation and
  compares georeferenced water/land classification across every tile handoff;
  the same feature does not jump as it crosses an LOD edge. Report maximum
  shoreline displacement in metres and screen pixels at orbit, region and
  local altitude, with a numeric gate selected by the ADR.
- No cracks, holes, wet land or dry ocean at mixed-level tile seams; V4
  hypsometry, V9 flight, V10 tile determinism and V13 precision still pass.
- The existing semantic water extent and the authoritative cell fractions
  remain unchanged, as do all simulation state hashes. Both styles agree on
  the coast, and the L6 60 fps/tile-memory gate remains in force.
