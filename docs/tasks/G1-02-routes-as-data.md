# Task G1-02 — Capabilities and routes as data

- **Track:** gameplay G1 (`docs/VERTICAL_SLICE.md` §4)
- **Status:** proposed (2026-10-11); needs G1-01
- **Scope:** two capabilities and three routes (coal steam with the coal
  gas grid, algae engines, river dams) loaded from `data/routes.json`,
  with planet conditions, discovery by exposure, physical counterweights
  and climate sensitivities.
- **Governing decisions:** `docs/VERTICAL_SLICE.md` §4; Systems
  Architecture doc, "Research follows the planet" and "Every route has a
  counterweight"
- **Must not touch:** the climate model's equations (read its fields;
  write only the existing `veg_use` and `local_forcing` hooks)

## 1. Data

`data/routes.json` holds one record per route:

``` text
id, capability, known_at_start
planet_condition      predicate over cell fields (see §2)
discovery             exposure source, rate, research direction multiplier
buildings             id, cost, upkeep, output, energy share consumed
fluxes                co2, soot, heat, veg, shelf_use, nutrient_load, flood_cells
sensitivity           field, threshold, effect on output
counterweights        list of { state, cohort, description } for the UI
```

Nothing route-specific may be hard-coded in `SliceState`; a test loads a
modified copy of the file and checks the change takes effect.

## 2. Planet conditions

- **Coal basin:** at least one region land cell is marked as a coal
  basin at setup, drawn from the seed (the fossil trap is always present).
- **Shelf:** a coastal sea cell is shelf when its depth, from
  `SEA_LEVEL_R − planet.radius_at(pos)` converted to metres with
  `ClimateModel.ELEV_SCALE` (or the bathymetry scale of `planet.gd`, if it
  uses a different one), is under 200 m. Algae needs ≥ 4 shelf cells in the region
  whose annual mean temperature exceeds 14 °C.
- **Dam site:** a region land cell whose rain exceeds 900 mm/yr and whose
  elevation exceeds a downstream region neighbour by ≥ 300 m. The
  downstream neighbour is the valley that floods.

## 3. New state

Per region cell: `coast_health` (0–1, sea cells), `air` (0–1, land cells),
`fish` (0–1, shelf cells). Coast health falls with the nutrient load of
algae farms and with sea temperature above the bloom threshold, and
recovers slowly; fish follow coast health and the shelf area taken by
farms; air falls with soot in a cell and its neighbours and recovers in
one turn.

## 4. Discovery and lock-in

Exposure accumulates per route from the activity named in its record
(fishing on shelf cells for algae, mills on rivers for dams). Each turn
the discovery chance is `1 − exp(−a·exposure·m)`, with `m` the research
direction multiplier, drawn from the seeded RNG. Research spending on a
capability raises the output of its routes in proportion to their
installed share (lock-in).

## 5. Acceptance and report

- A seed sweep (`tests/test_slice.gd -- --sweep`) over 50 seeds reports,
  per seed, which routes the planet supports. Report the share of seeds
  with algae possible, dams possible, both, neither; the slice needs at
  least one seed of each kind, listed in the report for playtests.
- On a seed with algae possible, an algae-seeking scripted strategy
  discovers algae before 1930 in at least 8 of 10 runs, and causes at
  least one bloom event before 1980.
- On a seed with dams possible, a dam-seeking strategy floods a valley
  once and loses dam output in at least one drought turn.
- Coal is the cheapest energy per unit of money in every turn of every
  strategy (`VERTICAL_SLICE.md` §4.3 rule 5).
- No route appears on a seed whose planet does not support it.
