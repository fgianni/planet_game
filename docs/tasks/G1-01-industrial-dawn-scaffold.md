# Task G1-01 — Industrial Dawn scaffold for the slice

- **Track:** gameplay G1 (specification §25; `docs/VERTICAL_SLICE.md`),
  in parallel with P0
- **Status:** proposed (2026-10-11)
- **Scope:** a new slice scene and state in the GDScript prototype that
  starts in 1880 and runs 20 five-year turns, reusing the prototype's
  climate model, region selection, shadow planet and newspaper.
- **Governing decisions:** `docs/VERTICAL_SLICE.md` §2, §3, §11;
  specification §16
- **Must not touch:** `sim/`, `godot/`, PSNAP; the prototype's
  `game.tscn`, `game_state.gd` and `tests/test_game.gd` (the oracle)

## 1. What to build

- `scripts/slice_state.gd`: a `SliceState` class, pure logic, no nodes,
  seeded and replayable from `(seed, actions)`. Start by composing or
  copying the parts of `GameState` it needs (region selection, flows,
  turn resolution, fingerprint, newspaper); do not change `GameState`.
- `slice.tscn` and `scripts/slice.gd`: the scene, initially a copy of the
  `game.tscn` flow pointed at `SliceState`.
- Start state: spin up at 280 ppm, run the historical CO2 fit to 1880
  (≈290 ppm), store the baseline, then start. Starting buildings: one coal
  mill, one gasworks, two farms, housing for a small population; the rest
  of the land in forest.
- Rest of the world: a historical CO2 curve for 1880–1980 scaled by
  `1 + k·(player per-capita emissions / historical per-capita − 1)`, with
  `k` a constant (start at 0.5). Replace the 2025 game's flat
  `world_co2_per_turn`.
- Calendar: `START_YEAR = 1880`, `MAX_TURNS = 20`, end in 1980.
- Logging: `export_log()` with seed, actions and per-turn state, in the
  same shape as `GameState.export_log()`, plus a `slice_version` field.

## 2. Acceptance and report

- `godot --headless --path . -s tests/test_slice.gd` plays 20 turns with
  a do-nothing strategy and a coal-expansion strategy on seed 7 and prints
  one line per turn; neither crashes, and the same seed gives the same
  output twice.
- With the do-nothing strategy, global warming at 1980 relative to 1880 is
  between +0.2 and +0.5 °C (the historical order of magnitude).
- `tests/test_game.gd` output is unchanged.
- Report: the 1980 warming for both strategies, run time per turn, and any
  part of `GameState` that had to be copied rather than reused.
