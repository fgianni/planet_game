# Task R2-01 — Temperature overlay and cell readout

- **Track:** rendering R2 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** finish the snapshot-playback half of R2: draw the existing
  `temperature_anomaly` semantic channel in the shared climate-lab overlay,
  expose semantic values for a selected cell, and extend the readability
  gate. Live `PlanetRun` ownership is deliberately a separate R2 task.
- **Governing decisions:** ADR-0018 §4.2, §4.5–4.7 and §7; specification
  §31.2, §31.7–31.10
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Decisions

1. View 7 is a style-independent, self-lit climate-lab overlay. Its fixed
   ramp is navy at −1, pale neutral at zero and amber at +1. The endpoints
   represent ±3 local climatological standard deviations, exactly matching
   the semantic channel definition.
2. The bridge uploads `temperature_anomaly` as the same `FORMAT_RF`
   cell-and-corner texture layout used by other scalar channels. Missing
   climatology produces an absent channel and a neutral texture; the HUD says
   unavailable rather than inventing a value.
3. `find_cell`, `has_channel` and `get_channel_value` are read-only bridge
   APIs. They expose semantic `VisualFrame` values only. Right-click ray/sphere
   selection is presentation state and never reaches PlanetSim.
4. The shared climate-lab shader is a Godot asset rather than embedded C++ so
   the production overlay can be rendered directly by the readability harness.

## 2. Acceptance and report

- The optional Godot extension builds against the pinned 4.2 bindings.
- A recorded L2 climate frame exposes a finite anomaly in [−1, 1]; the
  orbit-only preview reports it absent.
- Direction picking resolves a valid cell, and selecting the overlay does not
  change the geometry revision.
- The offscreen cold-to-warm overlay pair passes normal vision,
  deuteranopia, protanopia, tritanopia and luminance-only checks. Its weakest
  mean CIEDE2000 result is **56.29**, above the fixed threshold of 10.
- Snow and sea ice remain part of both natural styles and their selected-cell
  values are displayed as percentages.

## 3. Limitation / next slice

R2-02 owns live climate stepping in the bridge. R2-01 intentionally keeps the
existing `PFRAME01` playback path because `PlanetRun` is actively changing in
M7; recorded playback already exercises authoritative climate snapshots
without coupling rendering to those solver changes.
