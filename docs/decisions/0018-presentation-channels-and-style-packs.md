# ADR-0018 — Presentation boundary, semantic channels and style packs

- **Status:** Accepted
- **Date:** 2026-10-04
- **Accepted:** 2026-10-04
- **Amended:** 2026-10-12 — §11: asset sourcing, licences and the public/restricted split (proposed; revised 2026-10-14 after review); 2026-10-13 — §4.1 and §4.8 by ADR-0022 (accepted): the site list, detail keyed by position
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
    std::vector<VisualSite> sites;                     // ADR-0022 §4.5; added with the first sites (P1)
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

Procedural detail uses a presentation stream through the counter RNG, with
stream ids in a reserved presentation range that no simulation stream may
use. R1 uses it only if a style needs per-cell variation.

*Amended by ADR-0022 §4.9 (2026-10-13):* a spatial detail field is keyed by
`(world_seed, detail_kind)` and sampled by position on the sphere, so it
has no seam at cell edges and does not depend on a tile or its level. The
key `(world_seed, cell_id, detail_kind)` remains for per-cell choices that
are not spatial fields, such as a site's position among its candidates.

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

## 11. Amendment: asset sourcing, licences and the public/restricted split (proposed 2026-10-12, revised 2026-10-14)

### 11.1 Context

The owner wants to use free third-party 3D assets, starting with free
listings on Epic's Fab marketplace, as region- and local-band detail
(ADR-0022 §4.8). This record already says a style pack is a directory of
shaders and assets (§4.5), but not where assets may come from, under which
licence, or where they may be stored. Two facts force a rule:

- **The repository is public.** Fab's Standard License (summary read
  2026-10-12, which is explicitly non-binding) allows use "with any
  compatible tool", modification, commercial use, no attribution, and
  sharing with collaborators through a private repository; it forbids
  "reselling or freely redistributing resources on their own". Committing a
  Fab asset to `fgianni/planet_game` would publish it on its own, so it
  would very likely breach that licence. Other licences on the same
  marketplace (CC-BY, engine-restricted listings, `.uasset`-only downloads)
  differ again.
- **CI and coding agents must work without paid or restricted content.**
  The readability gates (§4.9, V8), the render capture (R4-06) and every
  agent working from a fresh clone run on the public tree.

### 11.2 Decision

**Three tiers.** Every asset file used by the client belongs to exactly one:

| Tier | Licences | Stored in | Example sources |
|---|---|---|---|
| `public` | CC0; CC-BY 4.0; MIT, BSD or Apache-2.0 for code-like assets; our own work | this repository, `godot/assets/public/` | Kenney, Poly Haven, ambientCG, assets we make |
| `restricted` | licences that allow use in a shipped game but not redistribution of the files: Fab Standard License, other marketplace EULAs | a private repository, `fgianni/planet_game_assets`, checked out to `godot/assets/restricted/`, which is git-ignored here | free Fab listings |
| refused | anything engine-restricted to a different engine; `.uasset` or other formats only one engine can open; non-commercial (NC), no-derivatives (ND) or share-alike (SA) licences; Fab's Personal - Reference Only tier (no source files); code plugins; editorial-only; unknown or unclear licence | nowhere | — |

A free price is not a licence: each listing's licence is read and recorded
at download, and an asset whose licence cannot be determined is refused. A
Fab listing offered under an open licence (an "Alternative License", Fab
EULA §2(c)), such as CC0 or CC-BY, is governed by that licence and belongs
to the `public` tier.

**What the Fab EULA requires** (full text of 1 October 2024 read
2026-10-14; this is a reading, not legal advice):

- *Sharing* (§5(a)): only with collaborators developing the game, directly
  or through a private repository; they may not redistribute it and must
  delete it when no longer needed, and the owner is liable for them. The
  private repository is shared with no one else; CI and cloud-hosted coding
  agents work on the public tree only.
- *No copyleft* (§6(a)): Standard-licence content may not be combined with
  GPL, LGPL (except dynamic linking) or CC-BY-SA material. The `public` tier
  therefore refuses share-alike licences, and the game's own licence may
  not be copyleft.
- *Shipping* (§4(c)): a build may carry restricted assets in object code,
  but must restrict end users from extracting or using them outside the
  game: the game's end-user terms forbid it, and builds carrying restricted
  assets encrypt their pack.
- *No editors* (§6(b)(iii)): restricted assets are never offered in a
  scenario, level or style editor that can export works, nor in mod kits.
- *Terms fixed per acquisition* (§2(a), §2(d), §7(a)): the Personal tier
  needs under $100,000 of revenue in the twelve months before the
  acquisition; the terms in force when an asset was acquired stay its
  terms, so the manifest records the tier and the EULA's date.
- *No generative-AI training* on content tagged "NoAI" (§6(b)(vii),
  §16(l)); the manifest records the tag.
- Screenshots and videos may be distributed freely (§4(b)); credit is not
  required for Standard-licence content.

**The manifest.** `godot/assets/manifest.json`, in the public repository,
lists every asset of both tiers:

``` text
id              stable asset id used by style packs
tier            public | restricted
path            relative to godot/assets/<tier>/
source_url      the listing or download page
author          as credited by the source
licence         SPDX id where one exists (CC0-1.0, CC-BY-4.0), else
                "Fab-Standard" or the marketplace's licence name
licence_url     the licence text
attribution     the credit line, required for CC-BY
modified        whether we changed the file; CC-BY requires saying so,
                and the credit line does
fab_tier        Personal | Professional, for Fab Standard-licence assets
licence_version the EULA's date as acquired (Fab: "2024-10-01")
no_ai           whether the listing is tagged NoAI
retrieved       date of download
sha256          of the file as committed or stored
fallback        id of a public asset used when this one is absent
                (required for restricted assets)
uses            style packs and bands that use it
```

Listing a restricted asset in the public manifest reveals only metadata,
never the file.

**References by id.** Style packs and scenes refer to an asset only by its
manifest `id`, never by a `res://` path or a Godot `uid`; the style loader
maps an id to its file, or to its fallback when the file is absent. A
tracked resource that named a restricted file directly would fail to load
on a fresh clone and in public CI before any fallback could apply. No
tracked file may mention `res://assets/restricted/`.

**Styles list their assets.** `PlanetStyle` gains the `assets` entry that
specification §31.3 already names: for each band of ADR-0022 §4.8, the
manifest ids the style draws there. The style loader (the directory scan of
`godot/styles/`) resolves them when it loads the pack.

**Formats.** Meshes are imported from glTF 2.0, FBX or OBJ and stored as
glTF 2.0 (`.glb`); textures and materials as PNG or KTX2, HDR images and
sky maps as EXR; fonts as TTF or OTF. Godot imports all of them directly,
and a future client could too. Godot's `.import` sidecar of each file is
kept with it (tracked here for `public` assets, in the private repository
for `restricted` ones), so import settings are reproducible.

**Size.** No single `public` file exceeds 10 MB, and `godot/assets/public/`
stays under 200 MB in total, so a clone and a CI checkout stay small.
Anything larger, or moving binary assets to Git LFS, is decided by
amendment.

**Fallbacks.** Every restricted asset names a public fallback with the same
role and footprint (a generic tree, rock or crate). The style loader
resolves a restricted asset to its fallback when `godot/assets/restricted/`
is absent, and logs once that it did. Gameplay, picking and every channel
are unaffected, because assets are detail only (§4.5,
specification §31.12: no gameplay difference between styles).

**Which styles may use which tier.**

- `stylised` and `map`, and any style that a readability gate in public CI
  depends on, use `public` assets only.
- A style pack may use `restricted` assets only if it declares
  `requires_restricted = true` in its manifest. The loader refuses a style
  that names a `restricted` id without declaring it, naming the asset, as it
  refuses a style missing a required channel (V7). Its readability results
  are recorded locally against the full asset set, as §5 already allows for
  jobs CI cannot run; public CI checks it with fallbacks only.
- Community style packs (specification §27) may use `public` assets only.

**Where third-party assets fit.** Third-party assets are for natural and
generic detail at region and local bands: vegetation, rocks, ground
materials, water-side and street props, vehicles of a generic kind.
Buildings are assembled from parts kits (Systems Architecture doc,
"procedural cities") so that a society's technology route shows in its
architecture; a third-party building may supply parts to a kit, but not
stand in for a route. A restricted asset never carries information the
public fallback does not, so the readability invariant (design §42.3)
holds in every tier.

**Credits.** The game's credits screen and the repository's
`godot/assets/CREDITS.md` are generated from the manifest; every CC-BY
asset appears with its attribution line, its licence and, if `modified`,
a note that it was changed.

**Shipping.** Exported builds may contain restricted assets, packed into
the game's resources and encrypted, under end-user terms that forbid
extracting them (Fab EULA §4(c)). Source releases, mod kits and any
exporting editor contain public assets only.

### 11.3 Validation

| ID | Check | Where |
|---|---|---|
| V10 | Every file under `godot/assets/public/` has a manifest entry with an allowed `public` licence and a matching `sha256`, and the size limits hold | CI script |
| V11 | No file under `godot/assets/restricted/` is tracked by git, the path is git-ignored, and no tracked file mentions `res://assets/restricted/` | CI script |
| V12 | Every `restricted` entry names a `public` fallback that exists | CI script |
| V13 | With `godot/assets/restricted/` absent, every style loads, the R1–R4 readability gates pass and the render capture runs | readability job (public CI) |
| V14 | `CREDITS.md` is up to date with the manifest | CI script |
| V15 | A style naming a `restricted` asset without `requires_restricted = true` is refused, naming the asset | bridge test (Godot job) |

These checks extend this record's V1–V9. ADR-0022 numbers its own checks
from V1, so a reference from elsewhere says "ADR-0018 V12".

### 11.4 Consequences

- Free marketplace assets can be used without putting the repository's
  licence or the owner's Fab account at risk.
- Contributors and agents without access to the private repository get a
  complete, if plainer, game; nothing breaks.
- Each restricted asset costs a fallback and a manifest entry. That is
  cheap compared with untangling an unlicensed file from git history after
  the fact.
- The realistic style pack, already a non-goal before P1
  (specification §31.12), is the natural first user of `restricted` assets.

### 11.5 Open questions

1. ~~Read the full Fab EULA before the first restricted asset.~~ Answered
   2026-10-14 (EULA of 1 October 2024): the tiers stand; its requirements
   are recorded in §11.2 ("What the Fab EULA requires") and in the
   "Shipping" paragraph.
2. If the game's source is ever released under an open licence, are
   restricted assets dropped from the release, or is the realistic style
   shipped as a separate download?
3. The repository is public but has no licence. Under which licence are
   our own assets (the `public` tier's "our own work") published: CC0,
   CC-BY 4.0, or the repository's licence once one is chosen? Whatever is
   chosen may not be copyleft (Fab EULA §6(a)).
4. Do SIL OFL 1.1 for fonts (the `ui_skin` of specification §31.3 carries
   fonts) and CC-BY 3.0 (common for older assets) join the `public` tier?
   (CC-BY-SA is refused, §11.2.)

### 11.6 Proposed task

**R1-05 — Asset manifest and tiers**: create `godot/assets/` with the
`public` tree, the manifest schema and an empty manifest, the git-ignore
rule, a `tools/fetch_restricted_assets.sh` that clones the private
repository when the user has access, the `assets` entry in `PlanetStyle`,
resolution of ids to files or fallbacks in the style loader (the directory
scan of `godot/styles/`), `CREDITS.md` generation, and checks V10–V15.
Needs only R1-02.
