# Task R1-03 — Readability harness

- **Track:** rendering R1 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** render the two R1 acceptance signals through both shipped style
  shaders, measure them under normal and colour-deficient vision, retain
  inspectable results, and make the gate part of CI.
- **Governing decisions:** ADR-0018 §4.9 and V8; specification §31.8
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Decisions

1. `godot/tests/render_readability.gd` renders controlled semantic-channel
   inputs at 128×128 with a fixed orthographic camera, ambient lighting and
   day/night shading disabled. It loads the production surface shaders; it
   does not duplicate their colour logic.
2. The R1 signals are `cold_vs_warm` (snow and ice increase over a land
   region) and `land_vs_ocean` (soil replaces deep water). Each pair changes
   semantic channels only and keeps camera, lighting and region fixed.
3. `tools/ci/check_readability.py` computes mean CIEDE2000 over the central
   96×96 signal region. It checks normal vision, full-severity deuteranopia,
   protanopia and tritanopia using the Machado et al. (2009) matrices, plus
   luminance-only Lab. Every value must be at least ΔE00 10.
4. The analyzer writes `PlanetSim.readability.v1` JSON. PNG inputs and the
   JSON report are retained as CI artifacts; PPM is the dependency-free
   interchange format used by the analyzer.
5. The Godot job is pinned to Godot 4.2.2 and a specific `godot-cpp` 4.2
   commit and runs through Xvfb with Mesa software rendering.

## 2. Acceptance

| Check | Criterion |
|---|---|
| V8 | Both R1 signals in both styles have mean ΔE00 ≥ 10 in all five variants |
| Reproducibility | Fixed semantic inputs, camera, image size, lighting and threshold |
| Inspection | Rendered PNGs and versioned JSON analytics are uploaded by CI |
| Math | Sharma et al. reference pair 1 evaluates to 2.0425 ± 0.0001 |

## 3. Report

The local offscreen run passed all 20 measurements. The CI job repeats the
same fixed inputs with Mesa software rendering. The weakest
variant of each style/signal pair was:

| Style | Signal | Minimum mean ΔE00 |
|---|---|---:|
| stylised | cold versus warm | 22.56 |
| stylised | land versus ocean | 25.90 |
| map | cold versus warm | 16.85 |
| map | land versus ocean | 32.99 |

The initial measurement found that land and ocean relied too heavily on hue,
and that the map style's cold signal was marginal in luminance and
tritanopia. The shipped palettes now carry those distinctions in luminance
as well. No simulation or snapshot code changed.
