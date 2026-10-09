# Task R1-02 — Style packs, playback and switching

- **Track:** rendering R1 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** the Godot half of R1: consume `VisualFrame`, play a recorded
  `PFRAME01` run, validate data-only style manifests, ship the `stylised` and
  `map` styles, switch them without rebuilding geometry, and keep the R0 views
  as climate-lab overlays. The readability harness is R1-03.
- **Governing decisions:** ADR-0018 §4.5–4.7, V7 and V9; specification
  §31.1–31.7, §31.9–31.10
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Decisions already made

1. The bridge links `PlanetSim::Presentation` and styles receive semantic
   channel textures only. Raw snapshot fields remain behind the bridge.
2. A style is a `PlanetStyle` Godot resource plus shader assets. It contains
   no script and declares its channel-set version and required channels.
3. The R1 scalar channels use one `FORMAT_RF` texture each. The five
   `surface_class` weights use two `FORMAT_RGBAF` textures; the second texture
   carries the fifth (ice) weight in its red component.
4. A `PFRAME01` record is loaded only after terrain has been generated with
   matching cell count. `PresentationReference` is built from all frames and
   frame selection never advances PlanetSim.
5. Style switching replaces the shader and rebinds existing textures. It
   does not rebuild geometry or re-read the recording.
6. With no recording, the existing orbit-only preview remains available and
   produces a snapshot-bound `VisualFrame`; recorded playback is the R1
   climate path.

## 2. Acceptance

| Check | Criterion |
|---|---|
| V7 | A style missing any required R1 channel is refused and the error names it |
| V9 | Switching between `stylised` and `map` keeps the geometry revision and loaded frame count unchanged |
| Build | Default headless build/tests remain Godot-free; the optional extension links `PlanetSim::Presentation` |
| Playback | Loading a matching `PFRAME01` record displays its semantic frame and permits indexed/animated playback |

## 3. Report

- **Build.** The default headless build remains independent of Godot. The
  optional extension builds and links against the official `godot-cpp` 4.2
  branch with `PlanetSim::Presentation` as its PlanetSim dependency.
- **Playback.** A one-year L2 run produced twelve `PFRAME01` frames. Godot
  4.2.2 loaded the matching terrain and record, built the reference, and
  played the `map` style headlessly without errors.
- **V7.** `godot/tests/test_style_switch.gd` loads an intentionally
  incomplete manifest. It is refused with
  `missing required channel surface_class`.
- **V9.** The same regression switches `map` to `stylised` and verifies that
  both the geometry revision and the loaded frame count are unchanged.
- **Textures.** Scalar channels are `FORMAT_RF`; `surface_class` uses two
  `FORMAT_RGBAF` textures. Cell values are averaged to dual corners in the
  bridge. Uploads are skipped when a channel vector is unchanged.
- **Smoothing.** Snow and sea ice interpolate toward the newly selected
  frame with presentation-only time constants. All other R1 channels update
  immediately; events remain unsmoothed.
- **Limitations.** R1-03 still owns quantitative CIEDE2000 readability under
  normal and simulated colour-deficient vision. The orbit-only preview uses
  a 48-snapshot annual reference; authoritative climate visuals use recorded
  playback.
