# Task R3-02 — Atmosphere scattering and wind

- **Track:** rendering R3 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** consume the R3-01 semantic channels in Godot, adding clear-air
  scattering to both production styles and a shared animated surface-wind
  climate-lab overlay.
- **Governing decisions:** ADR-0018 §4.5–4.6 and §7; specification
  §31.1–31.2
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Presentation contract

Both production style shaders read `atmosphere_density` and add a blue,
view-dependent limb term. An absent channel binds the bridge's neutral zero
texture, so orbit-only preview does not invent an atmosphere. The style packs
use different artistic strengths while consuming the same physical channel.

View 8 reconstructs each surface-wind vector from the cell's immutable east
and north tangent basis. It displays direction and speed using animated
tangent streaks: cyan at low speed, grading to amber at 60 m/s. The bridge
deterministically samples no more than 2,048 cells. It rebuilds this bounded
mesh only when the wind channel changes; the shader animates phase per frame.
The selected-cell readout reports speed and signed east/north components in
m/s.

## 2. Acceptance and report

- The optional extension builds against the pinned Godot 4.2 bindings and
  both style shaders import without errors.
- The Godot regression advances a resolved L4 climate step and verifies that
  atmospheric density is finite and normalized, wind components are finite,
  and the streak count is non-zero and at most 2,048.
- Selecting the wind view does not rebuild the planet geometry. Atmosphere,
  wind and style operations leave the authoritative state hash unchanged.
- The existing L2 twelve-month regression remains the fast temperature and
  asynchronous-live gate; only one L4 step is required for R3 because its
  36-band circulation resolves at that mesh density.
- The default headless build remains Godot-free. No solver, field meaning,
  persistent snapshot format or simulation hash changes in this task.
