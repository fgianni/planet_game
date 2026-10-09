# Task R4-01 — Precipitation channel

- **Track:** rendering R4 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** expose M7's model precipitation through the read-only snapshot
  boundary and map it to R4's semantic `precipitation` channel. Cloud cover
  and thickness remain absent until M8 provides physical cloud state.
- **Governing decisions:** ADR-0018 §4.1–4.2 and §7; specification
  §31.1–31.2
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Channel definition

Presentation snapshot schema 5 appends `precipitation_kg_m2_s`, copied from
the registered derived field after a completed step. This is an in-process
and `PFRAME01` presentation field only; it does not alter PSNAP.

The semantic channel uses a fixed linear scale:

```text
precipitation = clamp(rate / (50 kg m^-2 day^-1), 0, 1)
```

One kg/m² of water is one millimetre, so the upper endpoint is 50 mm/day.
Keeping the mapping linear makes it invertible below saturation, independent
of the active style and suitable for both intensity ramps and particle rate.
A missing source vector produces an absent channel rather than invented rain.

## 2. Acceptance and report

- The presentation test verifies 0, 25 and 100 mm/day map to 0, 0.5 and 1.
- Schema 5 recordings round-trip precipitation. Schema 4 circulation records
  and schema 3 records remain readable with precipitation absent.
- A live `StateSnapshot` copies the registered derived precipitation field;
  this does not change its values or feed anything back into PlanetSim.
- `test_presentation` and `test_climate_circulation` pass. The Godot rain
  layer is a later R4 task, and both cloud channels remain honestly absent.
