# Task G1-04 — Levers, the promise ledger and the first knowledge stage

- **Track:** gameplay G1 (`docs/VERTICAL_SLICE.md` §6, §7)
- **Status:** proposed (2026-10-11); needs G1-03
- **Scope:** the four levers and the zoning brush as logged actions; the
  promise ledger; the *local records* and *climatology* stages.
- **Governing decisions:** `docs/VERTICAL_SLICE.md` §6; specification
  §17.1–17.2, §29.10; ADR-0022 (proposed) for placement and zoning
- **Must not touch:** the climate model; knowledge must never reach it

## 1. Levers

Every lever is an action in the log, replayable:

- `speech(promise_id | none)`: promises in `data/cohorts.json`, each with
  a cohort, a measurable target over state (for example mean `air` in the
  mill towns ≥ 0.6) and a due turn two turns later.
- `budget(works, research, relief)`: shares summing to 1.
- `build(cell, building)`: at most two per turn, only discovered routes.
- `research(direction)`: mines, sea, rivers or sky.
- `zone(cell, protected | open)`: protected cells refuse new route
  buildings; existing ones stay.

## 2. Promise ledger

At the due turn the target is evaluated. Kept: trust of the cohort
`+δ`. Broken: `−r·δ` with `r = 4` by default (specification §29.6.7),
exposed as a constant for playtests. A promise left unmentioned does not
expire silently: the newspaper reports it either way.

## 3. Knowledge

- *Local records* from the start: the region's mean temperature and rain
  per turn since 1880, with each value dropped with a probability that
  falls as "sky" research rises (gaps are real, not a blur).
- *Climatology* unlocks when cumulative "sky" research passes a threshold:
  the regional anomaly against the 1880–1910 mean, and a global mean whose
  displayed error bar comes from the number of station cells (one per
  region cell with a building, sampled from the true field).
- A test runs two games identical except for research direction and
  asserts the climate model's state is bit-identical at every turn
  (specification §17.2: science reveals, it never changes the physics).

## 4. Acceptance and report

- Replaying an exported log reproduces the game exactly.
- The promise ledger's kept/broken outcomes match the targets evaluated
  from logged state.
- The bit-identical test of §3 passes.
- Report how many turns "sky" research needs to reach climatology at 10 %,
  20 % and 40 % of the budget.
