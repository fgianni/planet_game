# Task R1-01 — Presentation library and semantic channels

- **Track:** rendering R1 (specification §31.10), in parallel with P0 / M6
- **Status:** ready (ADR-0018 accepted 2026-10-04)
- **Scope:** the headless half of R1. A Godot-free `sim/presentation`
  library, the channel registry, the R1 channels, presentation snapshot
  schema 3, and the shared cover functions. No Godot or shader work; that
  is R1-02.
- **Governing decisions:** ADR-0018 §4.1–4.4, §4.8, V1–V6; ADR-0003
  (determinism scope, replay hashes); ADR-0008 (snow and sea-ice cover);
  specification §31.1, §31.2, §32.9
- **Must not touch:** any solver, any registered field's meaning, the PSNAP
  persistent format, M6-04 work in progress

## 1. Decisions made for this task

1. **Library.** `sim/presentation/` builds as the static library
   `PlanetSimPresentation` (alias `PlanetSim::Presentation`), linked by the
   tests now and by the Godot extension in R1-02. It links `PlanetSim` only
   for `sim/core` and the snapshot types.
2. **Include boundary.** Add `tools/ci/check_presentation_includes.py`:
   it fails if any file under `sim/presentation/` includes, directly or
   transitively through project headers, `sim/planet/planet_state.hpp`,
   anything under `sim/planet/dynamics/`, `sim/planet/surface/` other than
   the cover-function header of item 5, or `sim/planet/atmosphere/`. Run it
   in CI next to the floating-point check.
3. **Channel registry.** `sim/presentation/channel_registry.hpp`: an
   append-only `enum class ChannelId : std::uint16_t` with ids from 1, and
   a descriptor table (name, kind, range, introducing track, required in
   styles or overlay-only). Register every channel of specification §31.2
   and §32.9 now, so ids never move. Add
   `tools/ci/channel_registry_baseline.tsv` and a `planet_cli channels dump`
   command, checked like the field registry.
4. **`VisualFrame`.** As in ADR-0018 §4.1. Scalar channels are
   `std::vector<float>` of length cell count; weights channels are
   cell-major groups of `float`; absent channels are empty. `events` is
   empty in R1.
5. **Cover functions.** Add to a new header
   `sim/planet/surface/cover_fractions.hpp`:
   `snow_cover_fraction(swe_kg_m2)` and `sea_ice_cover_fraction(ice_kg_m2)`,
   with the exact expressions now inside `snow_covered_albedo` and the ocean
   tile. Make the physics call them. No numerical change is allowed.
6. **Snapshot schema 3.** Add to `StateSnapshot` the per-cell vectors of
   ADR-0018 §4.4, filled by `make_state_snapshot` when the corresponding
   field exists in the state, empty otherwise. Bump
   `state_snapshot_schema_version` to 3. Update the Godot bridge only as far
   as needed to keep it compiling; it ignores the new vectors until R1-02.
7. **`PresentationReference`.** Built once per planet from a snapshot: the
   per-cell annual maximum insolation (computed analytically from the orbit
   at construction, or from a year of snapshots; state which and why) and,
   when present, the climatology mean and variance.
8. **R1 channels.** Implement exactly the definitions of ADR-0018 §4.2.
   `surface_class` weights in R1:
   - water = 1 − land fraction;
   - ice = land fraction × snow cover (on land) + water × sea-ice cover,
     moved out of the water and land weights it covers;
   - the remaining land split between rock, sand and soil by elevation
     above sea level with fixed thresholds stored as named constants and
     reported. This is a placeholder until vegetation (M10) and is labelled
     as such in the code.
   The five fog channels return the constant "fully known, certain" value.

## 2. Acceptance

| Check | Criterion |
|---|---|
| V1 | `PlanetSimPresentation` and its tests build and pass in every existing CI configuration (GCC, Clang; Debug, Release; ASan+UBSan) with Godot absent |
| V2 | The include check passes, and fails on a deliberately bad include added in a test branch |
| V3 | The channel registry baseline check passes; reordering an id fails it |
| V4 | From two snapshots of the same planet a simulated winter and summer apart (or a cold and a warm run), mean `snow_cover` over mid-latitude land and `sea_ice` over polar ocean are higher in the cold one by more than 0.1, and `temperature_anomaly` differs in sign where the climatology exists |
| V4b | `daylight` is 0 on the night side and 1 at the cell's annual maximum; `relief` is 0 at sea level; `surface_class` weights sum to 1 within 1e-6 everywhere |
| V5 | All golden saves load and replay hashes are bit-identical before and after the cover-function refactor |
| V6 | A recorded `planet_cli run` produces identical checkpoint hashes whether or not `make_visual_frame` is called on every snapshot |
| Perf | `make_visual_frame` at L6 under 10 ms single-threaded in Release; report the measured time |

## 3. Report

- **V4/V4b.** The controlled cold/warm snapshot pair uses a representative
  land cell and an ocean cell: cold/warm land snow cover is 0.666667/0.0
  (Δ = 0.666667), cold/warm sea-ice cover is 1.0/0.0 (Δ = 1.0), and the
  corresponding temperature anomalies are −0.666667/+0.666667. Daylight is
  0 on the constructed night-side cell and 1 at the recorded annual maximum;
  relief is 0 at sea level; every surface-class weight tuple sums to 1 within
  1e-6. The regression also runs a one-year recorded L1 scenario and calls
  `make_visual_frame` on every callback; its final and checkpoint hashes are
  identical to the uncaptured run.
- **Performance.** Release single-threaded `make_visual_frame`, averaged over
  eight frames of synthetic cell-major data, measured 0.245 ms at L5 (10,242
  cells) and 0.985 ms at L6 (40,962 cells), below the 10 ms gate.
- **Surface class.** The temporary bare-land split is sand at relief ≤ 200 m,
  soil from 200 m to below 1,500 m, and rock at relief ≥ 1,500 m. This makes
  low coastal land and high relief visually distinct while retaining an
  explicit, small placeholder until M10 supplies surface/vegetation classes.
- **Annual maximum insolation.** `PresentationReference` scans the TOA
  insolation in a recorded orbital year's `StateSnapshot` frames and retains
  the per-cell maximum. The one-off cost is O(frames × cells), with one
  float per cell retained; it respects the snapshots-only presentation
  boundary and does not require mesh or solver access.
- **Proposed ADR-0018 clarification.** Schema-3 climatology vectors are
  sample-weighted annual mean and population variance pooled from the twelve
  monthly layers (including between-month variance), rather than an
  unspecified month. R1 also adds the non-authoritative `PFRAME01` sidecar
  for recorded presentation snapshots; it is neither PSNAP nor solver input
  and never contributes to a state hash.
