# Task R2-02 — Asynchronous live climate bridge

- **Track:** rendering R2 (specification §31.10), in parallel with P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** replace the orbit-only preview as the only live option with an
  explicit `PlanetRun` mode that publishes authoritative climate snapshots
  without making the render thread wait or giving presentation write access
  to simulation state.
- **Governing decisions:** ADR-0018 §3.4, §4.1 and §7; specification §8,
  §31.1, §31.4 and §31.9
- **Must not touch:** solvers, registered field meanings, PSNAP, or M7 work

## 1. Decisions

1. Live mode is explicit (`--live=true` or `L`). Recorded playback and live
   mode are mutually exclusive. The cheap orbit-only preview remains the
   default so opening the Godot project does not synchronously construct a
   second L6 climate run.
2. Starting live mode constructs a matching `PlanetRun` from the generated
   preset, seed and subdivision. The immutable presentation terrain remains
   the bridge's existing deterministic generation; both runs use the same
   mesh generator and keyed terrain inputs.
3. A dedicated `std::jthread` owns all live stepping. Godot only adds bounded
   requests (at most two waiting monthly steps) and polls the newest
   `StateSnapshot`; if presentation falls behind, an older pending visual
   frame is replaced rather than blocking rendering.
4. The worker never calls Godot. It publishes a snapshot, state hash and any
   error under standard C++ synchronization. Texture creation and every
   Godot API call remain on the main thread.
5. Initial live state resets presentation smoothing so newly available snow
   and sea-ice channels cannot remain absent. Later snapshots use the R1
   smoothing rule. The temperature reference is frozen after the first
   complete live orbital year; before then, anomaly remains honestly absent.
6. Starting and stopping may synchronously construct or join the run. Normal
   frame processing never waits for a climate step.

## 2. Acceptance

| Check | Criterion |
|---|---|
| Boundary | Live display is built only from `StateSnapshot` and `TerrainSnapshot` |
| Scheduler | One request lands exactly on the next scheduler boundary |
| Responsiveness | Requesting work returns immediately; stepping happens off the render thread |
| Backpressure | No more than two unprocessed monthly requests are retained |
| Isolation | Style changes, overlay changes and cell reads leave the live state hash unchanged |
| Lifecycle | Rebuild, recorded playback and node destruction stop and join the worker safely |

## 3. Report

The Godot regression starts an L2 climate run with one worker, verifies that
the queue call returns in under 50 ms, and polls twelve published monthly
frames. It observes advancing ticks and slow-state hashes, confirms that the
temperature anomaly appears only once the full reference year exists, then
switches styles and overlays and verifies the final hash remains identical.
The worker stops cleanly before the node is freed.

The default Godot mode remains orbit-only, and `PFRAME01` remains the preferred
path for inspecting long or calibrated runs. Live mode currently advances
climate steps only; command submission and reference/weather mode controls are
later presentation/input work. The bridge also rejects a custom preview radius
in live mode because `Scenario` does not carry radius; silently pairing
different simulation and presentation meshes would violate the snapshot
contract.
