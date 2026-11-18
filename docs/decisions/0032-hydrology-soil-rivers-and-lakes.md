# ADR-0032 — Hydrology: soil water, groundwater, rivers and lakes

- **Status:** Proposed (2026-11-18). The plan and its four decisions (§4)
  are for the user's review; nothing is implemented.
- **Date:** 2026-11-18
- **Milestone:** P0 / M9 (hydrology, rivers and lakes), started with the
  user 2026-11-18
- **Context documents:** `docs/DEVELOPMENT_SPEC_v0_8.md` §9.10 (water as
  stocks), §12 (the field example `soil_moisture`), §13 M9 and M10, drainage
  preparation (`downstream`, `basin_id`, `catchment_area_m2`), the cadence
  list (surface/hydrology: hours), R5 (`water_extent`, `dryness`)
- **Related:** ADR-0003 (snapshot migration), ADR-0005 (drainage on the
  cell mesh; §4.2 depressions, §7 M9's part), ADR-0008 (snow, rain and snow
  at the surface), ADR-0021 §4.3 (evaporation and the bucket), ADR-0023 §17
  (V8 at L4), ADR-0029 §7 (M9 on climate mode's own land P − E)

## 1. Context: what M9 starts from (audit, 2026-11-18)

- **The routing graph** (M2-03, ADR-0005 §4.2, `sim/planet/terrain/drainage.hpp`):
  every land cell has one `downstream` neighbour (steepest descent on the
  filled surface), a `basin_id` (its ocean outlet, or the no-ocean sink)
  and a `catchment_area_m2`; filled depressions carry a `depression_id`,
  a spill cell, a spill level and a maximum fill depth. It is built at
  terrain generation (`terrain_generator.cpp`) and kept in the terrain
  snapshot (`make_terrain_snapshot`); nothing routes water along it yet.
  ADR-0005 leaves to M9 whether a depression holds a lake, spills or is
  endorheic.
- **The land's water** (ADR-0021 §4.3, `surface_energy.cpp`): a Manabe
  bucket W, W_max = 150 mm (`water.hpp`). Rain and snowmelt fill it,
  evaporation empties it (β = min(1, W / (0.75 W_max))), and the overflow
  above W_max runs off to the ocean at once (`ForcingState::runoff_kg_m2_s`,
  the budget's `bucket_runoff_kg`). ADR-0021: "M9 replaces the instant
  runoff with rivers."
- **The water budget** closes over vapour, cloud water, bucket, snow and
  sea ice (`water_residual_kg`); new stocks join it.
- **Snapshots:** PSNAP schema 7; new persisted stocks need schema 8 and a
  migration (ADR-0003).
- **Constraints:** water as stocks (soil, snowpack, rivers, lakes,
  groundwater; specification §9.10); budgets closed; deterministic and
  worker-independent; no scripted modifiers. ADR-0029 §7.2: M9 builds on
  climate mode's own land P − E (about a tenth of reference mode's at L4),
  is validated also on reference mode's land P − E at L4, and does not
  gate on absolute discharge.

## 2. Decision drivers

- Every stock and flux booked, so that M10's irrigation, drought and
  downwind drying act on real stores.
- One scheme for both modes: a 10-minute step in reference mode and a
  monthly one in climate mode, stable and exact for either.
- Built on ADR-0005's graph without changing it.
- Lakes must not open the energy budget.

## 3. The plan

| Step | Content | Gate |
|---|---|---|
| **ADR-0032** (this) | the decisions of §4, accepted with the user | — |
| **M9-01 Soil and groundwater** | the bucket becomes the root-zone soil moisture; above capacity, surface runoff; slow drainage to one groundwater store with a baseflow time scale | water closure (1e-12 relative, as today); worker independence; replay |
| **M9-02 River routing** | each cell's river store a linear reservoir draining to `downstream`, solved exactly for any step in the graph's topological order; discharge booked at the ocean outlets | closure; routing conserves water; at steady state an outlet's discharge equals its catchment's runoff |
| **M9-03 Lakes and endorheic basins** | depressions hold water up to their spill volume, the lake's area from the hypsometry; full lakes spill downstream; arid ones stay endorheic; lakes evaporate as open water in the land tile | closure; a depression fills and spills; an arid one stays endorheic; energy closure unchanged |
| **M9-04 Snowmelt seasonality and validation** | seasonal discharge from the snowmelt; routing and lakes checked also on reference mode's land P − E at L4 (ADR-0029 §7.2); performance | a melt-season discharge peak; V11's budgets |
| **M9-05 Snapshot and channels** | PSNAP schema 8 with its migration; discharge, `water_extent` and `dryness` for the presentation (R5) | migration round trip; schema-7 snapshots load |

## 4. Decisions proposed (for the user's review)

1. **Groundwater in P0: yes, as one slow store per land cell**, filled by
   drainage from the soil and emptied by baseflow to the cell's river.
   The specification lists it among the stocks, and M10's drought and
   irrigation need it.
2. **Lakes are open water inside the land tile**, not a third tile: over a
   lake's area the land tile evaporates at β = 1 and its surface is water,
   so the energy budget stays closed without new surface physics.
3. **River speed: one constant velocity** (about 1 m/s) sets each cell's
   residence time from its spacing (τ = distance / v). Simple, and it
   scales with the mesh level.
4. **Cadence: hydrology runs in the surface step**, every 10 minutes in
   reference mode and monthly in climate mode. The linear reservoirs'
   exact (exponential) solutions keep the monthly step stable and the
   water exact.

## 5. Not decided here

- The soil and groundwater time scales and capacities: set in M9-01 from
  the literature, recorded with their sources.
- Floods (`flood_fraction`), reservoirs, irrigation and withdrawal: later
  milestones (M10 and P1) on these stores.
- Rivers' heat and the lakes' ice: not in P0 unless M9-03 finds lakes'
  freezing needed for closure.

## 6. Status

Proposed. On acceptance: M9-01's task document is written and becomes the
current task.
