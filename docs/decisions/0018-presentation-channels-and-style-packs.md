# ADR-0018 — Presentation boundary, semantic channels and style packs

- **Status:** Accepted
- **Date:** 2026-10-04
- **Accepted:** 2026-10-04
- **Milestone:** rendering track R1 (specification §31.10), in parallel with P0 / M6
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_8.md` §2, §3, §7, §8 (pacing is presentation), §17.2, §31, §32.9; Planetary Civilization Simulator — Design Record v1.3, §3.4, §21.4, §42, §44.8
- **Related:** ADR-0002 (mesh, cell and corner layout), ADR-0003 (snapshots, history store, determinism scope), ADR-0005 (land fraction, hypsometry), ADR-0008 (snow and sea-ice cover), ADR-0012 and ADR-0019 (not yet written; this record reserves their channels)
- **Amends on acceptance:** the presentation `StateSnapshot` (schema 2 → 3); the Godot bridge's view modes become climate-lab overlays

## 1. Context

Design v1.2 §42 makes the art direction swappable: realistic, stylised,
cartoon or map-like versions of the same planet, chosen in the settings,
with every style required to carry the same information. Specification §31
turns that into a contract: a Godot-free `sim/presentation` library, a
`VisualFrame` of semantic channels, style packs that see only those
channels, and a readability harness in CI. It replaces M14 with a
rendering track R0–R8 that runs alongside the physics.

### 1.1 What the repository has (audit, 2026-10-04, at `1c62824`)

- **`StateSnapshot`** (`sim/core/serialization/state_snapshot.hpp`,
  schema 2) carries the clock, orbit and Sun geometry and one per-cell
  field, the top-of-atmosphere insolation.
- **`TerrainSnapshot`** (schema 2) carries per-cell mean elevation, land
  fraction, plates, crust and drainage, built once per generated planet.
- **`PlanetMeshNode`** (`godot/gdextension/`) builds the cell mesh once,
  colours it on the CPU for five view modes (terrain, plates, crust age,
  insolation, drainage) and uploads one float texture per tick: the
  insolation, one texel per cell and per corner. One embedded shader
  draws all views.
- **The bridge does not run the climate.** `advance_simulation_ticks`
  moves the clock and recomputes insolation; surface temperature, snow,
  sea ice and the atmosphere are never shown. The full climate runs only
  in `planet_cli run`, which can write persistent snapshots, and in the
  history store of ADR-0003 (M4-05).
- **Cover fractions are defined inside the physics.** Snow cover is
  `m / (m + m_mask)` inside `snow_covered_albedo` (ADR-0008); sea-ice cover
  is `min(1, m / (ρ_i h_r))` inside the ocean tile (ADR-0008 §10). Neither
  is exposed as a function.

## 2. Decision drivers

1. **Presentation only.** Nothing drawn may reach authoritative state
   (specification §2, §31.1). Rendering a run must leave every state hash
   unchanged.
2. **Testable without Godot.** The meaning of every channel must be unit
   tested headless, in the existing CI, like the physics.
3. **Swappable art.** A style must be replaceable without touching C++,
   and switching must be instant.
4. **Readability is a gate, not a hope.** A style that hides a signal is
   not shipped (design §42.3).
5. **One definition per quantity.** What the screen calls "snow cover"
   must be what the albedo uses, or the picture and the physics disagree.
6. **Grow with the physics.** Channels appear milestone by milestone; a
   style must render a missing channel neutrally rather than fake it.
7. **Do not block M6.** R1 must not change any solver, field, or the
   persistent snapshot format.

## 3. Options considered

### 3.1 Where the physics-to-meaning mapping lives

- **In shaders** (as today). Fast to start; untestable headless; each style
  would re-derive meaning from raw fields, so two styles could disagree
  about what "dry" is.
- **In the Godot bridge (C++).** Testable only with Godot built; the
  default CI does not build the extension.
- **In a Godot-free `sim/presentation` library** (chosen). Unit-tested in
  the existing CI; one mapping shared by every style; the bridge becomes a
  thin uploader.

### 3.2 What a style may see

- **Raw snapshot fields.** Maximum freedom; breaks driver 3 and 5: every
  style would carry physics knowledge.
- **Semantic channels only** (chosen). Normalised, documented, versioned;
  the style decides only appearance.

### 3.3 How a style is packaged

- **A GDScript class per style.** Flexible; scripts in packs are an open
  question (specification §27) and make validation hard.
- **A data manifest plus shaders** (chosen). Validated at load; no code.

### 3.4 How R1 gets climate data to show

- **Run the climate inside the bridge.** Natural eventually; couples R1 to
  the scheduler while M6 is changing it.
- **Play back recorded runs** (chosen for R1). `planet_cli run` already
  records; the bridge loads snapshots from a run and steps through them.
  Live stepping in the bridge is a later task (R2 or R3), once M6-04 lands.

## 4. Decision

### 4.1 The library

`sim/presentation/` is a static library `PlanetSimPresentation`, linked by
the Godot extension and by the tests, with no Godot dependency. It depends
on `sim/core` and on the snapshot types only. It must not include
`planet_state.hpp` or any solver header; a CI include check enforces this
(the same style as the floating-point policy check).

```cpp
struct VisualFrame {
    std::uint32_t channel_set_version;
    SimulationTick tick;
    // One entry per channel; absent channels have an empty value vector.
    std::array<ChannelData, channel_count> channels;  // cell-major float32
    std::vector<VisualEvent> events;                   // empty in R1
};

[[nodiscard]] VisualFrame make_visual_frame(const StateSnapshot&,
                                            const TerrainSnapshot&,
                                            const PresentationReference&);
```

`PresentationReference` holds per-planet constants the channels need (for
example the climatology a temperature anomaly is measured against),
built once from a snapshot, never from `PlanetState`.

### 4.2 The channel registry

`sim/presentation/channel_registry.hpp` lists every channel of
specification §31.2 and §32.9 with an append-only numeric id, a name, a
kind (scalar, weights, vector), a range and the track that introduces it,
like the field registry. The channel-set version is the highest id
present. A CI check diffs the registry against a baseline, as for fields.

R1 implements these channels; the others are registered and reported
absent:

| Channel | Definition in R1 |
|---|---|
| `relief` | terrain mean elevation minus sea level, m |
| `surface_class` | weights over water, rock, sand, soil, ice from land fraction, elevation, snow and ice |
| `daylight` | insolation over the cell's annual maximum, clamped to 0..1 |
| `snow_cover` | `snow_cover_fraction(swe)`, section 4.3 |
| `sea_ice` | `sea_ice_cover_fraction(mass)`, section 4.3 |
| `temperature_anomaly` | `(T − T_clim) / (3 σ_clim)`, clamped to −1..1; overlays only |
| `known`, `guessed`, `knowledge_age`, `observation_uncertainty`, `estimate_uncertainty` | registered; constant "fully known, certain" in R1 |

### 4.3 One definition per quantity

Two pure functions move into `sim/planet/surface/` and are used by both
the physics and the presentation:

```cpp
[[nodiscard]] double snow_cover_fraction(double swe_kg_m2) noexcept;     // m / (m + m_mask)
[[nodiscard]] double sea_ice_cover_fraction(double ice_kg_m2) noexcept;  // min(1, m / (ρ_i h_r))
```

`snow_covered_albedo` and the ocean tile call them. This is a refactor with
no numerical change: the golden saves and replay hashes must be unchanged.

### 4.4 The presentation snapshot, schema 3

`StateSnapshot` gains, per cell, as `float32`:
`surface_temperature_K`, `land_snow_water_equivalent_kg_m2`,
`sea_ice_mass_kg_m2`, and, when the climatology exists,
`climatology_surface_temperature_mean_K` and
`climatology_surface_temperature_variance_K2`. Fields that the run has not
produced are empty vectors. This is the presentation snapshot, not the
persistent PSNAP format, which is unchanged.

### 4.5 Style packs

A style pack is `godot/styles/<name>/style.tres` plus its shaders and
assets, as in specification §31.3. The manifest names the channel-set
version it implements and, for each channel it uses, a ramp or LUT. At
load, the bridge refuses a style that does not use every channel
**required** at that version, naming the missing ones. Required in R1:
`relief`, `surface_class`, `daylight`, `snow_cover`, `sea_ice`, `known`,
`guessed`, `knowledge_age`. (`temperature_anomaly` and the uncertainty
channels are overlay-only and drawn by the shared overlay layer, not by
styles.)

R1 ships two styles, `stylised` and `map`, built together.

Switching swaps materials and assets only; the bridge keeps the current
`VisualFrame` and textures.

### 4.6 Upload

Each scalar channel is a float texture with the existing layout (one texel
per cell, then one per corner, corners averaged from their cells). Weights
channels pack into RGBA. Only channels whose values changed since the last
frame are uploaded. The current view modes become climate-lab overlays
drawn by a shared overlay layer above the style.

### 4.7 Smoothing

The bridge smooths each scalar channel with the rule of specification
§31.4. R1 sets `k_ch` for `snow_cover` and `sea_ice` and leaves the others
unsmoothed. Smoothing is presentation state, never snapshotted.

### 4.8 Presentation randomness

Procedural detail uses a presentation stream keyed by
`(world_seed, cell_id, detail_kind)` through the counter RNG, with stream
ids in a reserved presentation range that no simulation stream may use.
R1 uses it only if a style needs per-cell variation.

### 4.9 Readability threshold

The harness of specification §31.8 starts with a mean CIEDE2000 ΔE of 10
over the signal region, in normal vision and in simulated deuteranopia,
protanopia and tritanopia, and a luminance-only contrast check. The
threshold is recorded in the harness and changed only by amendment.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | `sim/presentation` builds and its tests run with Godot absent | default CI |
| V2 | Include check: no `planet_state.hpp` or solver header reachable from `sim/presentation` | CI script |
| V3 | Channel registry append-only against a baseline | CI script |
| V4 | Each R1 channel moves the right way: warm vs cold snapshot changes `snow_cover`, `sea_ice` and `temperature_anomaly` in the expected direction by more than a threshold | unit tests |
| V5 | Cover functions: the refactor of §4.3 leaves golden saves and replay hashes bit-identical | existing regression tests |
| V6 | Rendering a recorded run through the bridge leaves the run's state hashes unchanged | regression test |
| V7 | A style missing a required channel is refused, naming the channel | bridge test (Godot job) |
| V8 | Readability: cold year vs warm year, and land vs ocean, pass §4.9 in both styles | readability job (Godot, software renderer) |
| V9 | Switching styles on a loaded frame does not rebuild the mesh or re-read the snapshot | bridge test |

V7–V9 run in a separate CI job that builds the extension against a pinned
`godot-cpp` and renders offscreen under a software renderer. If that job
cannot be made reliable in R1, V8 runs locally with recorded results until
it can, and the ADR is amended to say so.

## 6. Consequences

- Every later visual layer (R2–R8) is a channel plus ramps in two styles,
  testable headless before any shader is written.
- The bridge gets thinner: colouring moves from CPU code in
  `planet_mesh_node.cpp` to style shaders reading channel textures.
- Art can be explored without C++: a new style is a directory.
- Two styles from the start roughly doubles the shader work of each
  visual layer. This is the price of proving the abstraction; it falls
  once the channel set settles.
- Presentation snapshot schema 3 widens what Godot receives per tick; at
  L6 the R1 channels add about 2 MB per frame before the changed-only rule.

## 7. Milestone mapping

| Track | Uses this record for |
|---|---|
| R1 | everything above |
| R2 | live stepping in the bridge; `temperature_anomaly` overlay polish |
| R3–R7 | new channels and ramps; events (§31.2) |
| R8 | fog channels become live (ADR-0019) |

## 8. Open questions

1. Can offscreen rendering under a software renderer run reliably in CI
   for V7–V9 (specification §31.8)? If not, what replaces it?
2. Should `surface_class` weights be computed in `sim/presentation` or come
   from a future surface-type field once vegetation exists (M10)?
3. Is mean ΔE 10 the right starting threshold? Measure both styles first.

## 9. Proposed tasks

1. **R1-01 — Presentation library and channels** (headless): §4.1–4.4, V1–V6.
2. **R1-02 — Style packs, two styles and switching** (Godot): §4.5–4.7, V7, V9;
   playback of a recorded run in the bridge.
3. **R1-03 — Readability harness**: §4.9, V8, in CI or recorded locally.

## 10. References

- Design Record v1.2 §42 (rendering and presentation), v1.3 §44.8 (fog).
- Specification v0.8 §31 (rendering contract), §32.9 (fog channels).
- Sharma, Wu and Dalal (2005), *The CIEDE2000 colour-difference formula*.
- Machado, Oliveira and Fernandes (2009), *A physiologically-based model
  for simulation of color vision deficiency*.
