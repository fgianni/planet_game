# Task R4-06 — Render capture

- **Track:** rendering R4 (specification §31.8, §31.10), in parallel with
  P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** make actual Godot output directly inspectable in interactive use
  and CI, including a water-enabled precipitation frame.
- **Governing decisions:** ADR-0018 §4.9 and V8; specification §31.8
- **Must not touch:** simulation state, solvers, snapshots or M7 calibration

## 1. Capture contract

`F12` captures after Godot's next completed draw and saves a PNG in the
user-data directory, printing its absolute path. The command-line
`--screenshot=PATH` uses the same path. For a live run it waits until a
snapshot with a positive simulation tick has reached the render thread;
`--quit-after-screenshot=true` makes automated capture terminate cleanly.
If the display backend does not complete a draw, an internal 15-second
deadline reports the failure and exits non-zero instead of hanging.

The CI software-rendering job launches an L4 water-enabled View 9, waits for
its first authoritative monthly snapshot, and stores
`live-precipitation.png` with the readability analytics. A 60-second outer
timeout turns failure to publish, draw or exit into an explicit CI failure.

## 2. Acceptance and report

- Capture occurs after `RenderingServer.frame_post_draw`, not from an
  unrendered or partially updated frame.
- Parent directories are created, save failures are reported, and automated
  capture can exit without user input.
- The screenshot path and timing are presentation inputs only. Capturing does
  not advance, mutate or hash authoritative state.
