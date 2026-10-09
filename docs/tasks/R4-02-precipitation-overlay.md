# Task R4-02 — Precipitation overlay

- **Track:** rendering R4 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** render R4-01's normalized model-precipitation rate in Godot and
  expose the physical rate in the selected-cell readout. Cloud cover and
  thickness remain absent until M8 supplies real cloud state.
- **Governing decisions:** ADR-0018 §4.5–4.6 and §7; specification
  §31.1–31.2
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Presentation contract

View 9 is a shared, self-lit climate-lab overlay. It maps zero precipitation
to near-black, light precipitation to blue and the fixed 50 mm/day endpoint
to cyan. Active cells pulse with a deterministic cell phase in the shader;
this provides motion without CPU work, snapshot mutation or invented fall
direction. Both style packs therefore present the same scale.

The cell readout converts the normalized value back to mm/day below the
channel's saturation endpoint. Values displayed as 50 mm/day may mean 50 or
more; the legend communicates that endpoint. An absent channel stays dark
and the readout reports it unavailable.

## 2. Acceptance and report

- The optional extension consumes the precipitation texture through the
  shared overlay shader; no style reads raw snapshot fields.
- The Godot live regression verifies a finite value in [0, 1], selects view
  9 without rebuilding geometry, and confirms presentation operations leave
  the authoritative state hash unchanged.
- Animation uses Godot `TIME` only in the shader. No presentation timing or
  value feeds back into PlanetSim.
- Cloud cover and thickness remain absent rather than inferred from humidity
  or precipitation. They wait for M8's physical cloud fields.
