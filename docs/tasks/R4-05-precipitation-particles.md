# Task R4-05 — Precipitation particles

- **Track:** rendering R4 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** add spatial precipitation motion to View 9 using only R4's
  semantic precipitation channel.
- **Governing decisions:** ADR-0018 §4.6–4.8; specification §31.1–31.2
- **Must not touch:** solvers, snapshot meaning, registered fields, PSNAP,
  cloud inference or M7 calibration

## 1. Presentation contract

For each visibly raining cell, the bridge constructs two crossed transparent
shafts aligned with the cell's immutable east/north basis and the physical
radial fall direction. Width, length and colour follow normalized
precipitation intensity. Crossed planes remain legible around the globe
without camera-facing CPU updates.

Wet cells are selected deterministically and uniformly, with at most 2,048
cells represented. Geometry changes only when the semantic precipitation
channel changes. Godot `TIME` moves the shafts radially in the vertex shader;
no per-render-frame CPU work or authoritative state mutation occurs.

## 2. Acceptance and report

- The water-enabled L4 Godot regression requires a non-empty particle set no
  larger than 2,048 after a physically raining snapshot.
- Selecting View 9 still leaves planet geometry and the live state hash
  unchanged.
- A missing or all-dry precipitation channel produces no shafts. Cloud cover
  and thickness remain absent until M8.
