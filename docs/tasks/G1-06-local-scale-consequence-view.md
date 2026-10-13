# Task G1-06 — Watch-only local scale (stretch)

- **Track:** gameplay G1 (`docs/VERTICAL_SLICE.md` §2, §7 step 7)
- **Status:** proposed (2026-10-11); stretch goal, needs G1-05 and a
  decision on ADR-0022 (PR #4)
- **Scope:** replace the consequence card's still with a short
  watch-only local view generated from the event cell's terrain and state.
- **Governing decisions:** ADR-0022 (proposed); specification §31.5
- **Must not touch:** slice state; the view reads it only

## 1. What to build

For a flood, a drought, a dead bay or a dam filling: a generated patch of
the event cells, its terrain from the prototype's elevation, its land use
from the slice state, and an animation of the event's physical quantity
(water level, dry soil colour, water oxygen colour) over a few seconds.
No authored scenes: the same event on another seed looks different
because the terrain is different.

## 2. Acceptance and report

- The view never shows a building, field or water level the state does
  not contain (the truthfulness rule of specification §31.5).
- Report whether playtesters who saw the local view named causes more
  often than those who saw the still (Q1, Q5). If not, drop it from the
  alpha.
