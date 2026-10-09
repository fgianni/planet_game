# Task R4-04 — Live water preview

- **Track:** rendering R4 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** allow an explicit Godot live run to select the existing
  authoritative `Scenario::water_cycle`, so View 9 can be tested with model
  precipitation before M7-07 decides the preset default.
- **Governing decisions:** ADR-0018 §4.7; specification §31.1
- **Must not touch:** solver behavior, preset defaults, registered fields,
  PSNAP, or M7 calibration

## 1. Contract

`--water=true` is parsed by the Godot presentation script and passed as a
scenario choice when it starts the background `PlanetRun`. The default stays
false. Regeneration and stopping/restarting live mode preserve the explicit
choice. The HUD labels a water-enabled run.

This is input, not presentation inference: precipitation still comes only
from schema 5 snapshots produced after authoritative scheduler steps. No
Godot code writes humidity, evaporation, condensation or precipitation.

## 2. Acceptance and report

- The combined Godot regression starts one resolved L4 step with the water
  cycle enabled and requires at least one cell with precipitation above zero.
- The channel remains finite and normalized, selecting View 9 does not rebuild
  geometry, and rendering leaves the live state hash unchanged.
- A run without the option retains the current dry/default scenario. M7-07
  alone owns calibration and any later change to preset defaults.
