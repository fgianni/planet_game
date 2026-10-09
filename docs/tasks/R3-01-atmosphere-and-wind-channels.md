# Task R3-01 — Atmosphere and wind channels

- **Track:** rendering R3 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** implement the Godot-free semantic-channel half of R3 from the
  pressure and surface-wind fields already present in presentation snapshot
  schema 4. Godot scattering and wind particles are R3-02.
- **Governing decisions:** ADR-0018 §4.1–4.2 and §7; specification §31.1–31.2
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Channel definitions

`atmosphere_density` is scattering column density, not local volumetric air
density:

```text
atmosphere_density = clamp(sea_level_pressure / 101325 Pa, 0, 1)
```

At fixed gravity, surface pressure is proportional to atmospheric column
mass, the physical quantity that sets clear-air scattering strength. The
Earth reference gives the documented normalized range; atmospheres denser
than that saturate the R3 visual strength rather than changing simulation.

`wind` is cell-major `(eastward, northward)` surface wind in m/s, with two
float32 components per cell. It retains authoritative sign and magnitude;
the Godot bridge reconstructs a world-space tangent vector from the immutable
mesh basis.

Both channels are absent when any required source vector is absent. A style
or overlay must render that absence neutrally.

## 2. Acceptance and report

- The presentation unit test verifies 101325 Pa → 1, half that pressure →
  0.5, and zero → 0.
- It verifies exact signed east/north component ordering across cells.
- A snapshot lacking pressure and winds produces two absent channels.
- `sim/presentation` remains snapshot-only and Godot-free; no solver,
  registered field, persistent snapshot, or state hash changed.
