# ADR-0022 — Player placement, zoning and the three camera scales

- **Status:** Proposed
- **Date:** 2026-10-10
- **Milestone:** none in P0. The presentation parts may be prototyped on the rendering track (R5, R8) against synthetic frames; the command and state parts must not be implemented before ADR-0012 (society state and regions) and ADR-0014 (player commands) are accepted.
- **Context documents:** Design Record v1.4 §8.4 (the player's identity), §42.5 (camera scales), §42.6 (civilization on the planet), §42.8 (staging events), §46.2 (what we avoid from Civ), §46.8 (player levers as government); `docs/DEVELOPMENT_SPEC_v0_8.md` §9.10, §18.1, §26.1 items 5 and 7, §31.2, §31.5–§31.7
- **Related:** ADR-0005 (sub-cell hypsometry), ADR-0012 and ADR-0014 (reserved, not yet written), ADR-0018 (presentation boundary, channels, presentation RNG), ADR-0019 (reserved: fog of knowledge)
- **Amends on acceptance:** ADR-0018 §4.1 (`VisualFrame` gains a site list, §4.5 below); the presentation `TerrainSnapshot` gains the per-cell hypsometry (§4.6). Neither PSNAP nor any solver changes.

## 1. Context

The design record makes the player a government (design §8.4) that acts
through words and deeds (§46.8). One deed family is **investment**: power
plants, grids, rail, dikes, cities. One regulation family is **zoning**:
forest protection, managed retreat. The record also rules out "tile-stacking
puzzles" (§46.2) and asks that players see their own footprint, with its
consequences, on the planet (§42.6). It names three camera scales (§42.5) and
staged events (§42.8), but neither the design record nor the specification
says what the player may *do* at each scale, nor where a placed thing sits
inside a cell.

On 2026-10-10 the owner decided the grain of interaction: **the player places
major investments and paints zoning; fields, farms and city growth are not
placed by hand.** They emerge from the economy and society under the zoning
and incentives the player sets. The owner also asked for a watch-only close
view that shows the consequences of floods and droughts on cities, crops and
forests. This record turns that decision into a contract.

### 1.1 What the repository has (audit, 2026-10-10, at `b465ba5`)

- **The mesh** at the shipped level L6 has 40,962 cells, about 110 km
  across. A city, a plant or a dike is smaller than a cell; a field is far
  smaller.
- **Sub-cell hypsometry** (ADR-0005 §4.1): nine elevation quantiles per cell
  at area fractions 0, 1/8, …, 1. Specification §18.1 already expresses sea
  walls and dikes as "a protected elevation within the cell's curve", and
  §29.4 lists an `inundated_fraction` exposure computed against it.
- **No civilization state.** The land-use channels `urban`, `farmland` and
  `mining` are registered for R8 (§31.2) and absent.
- **`VisualFrame`** carries channels and an unpopulated `VisualEvent` list
  (kind, cell, magnitude, begin and end tick). It has no notion of a placed
  structure.
- **`TerrainSnapshot`** carries the mean elevation and land fraction per cell,
  not the hypsometry quantiles.
- **Camera scales** (§31.5): planet, region, local, with detail "procedural
  from channels" at region and local scale and a test that detail never
  contradicts its channel. No interaction rules.
- **The 0.5 prototype** placed buildings directly on cells. That is the
  behaviour this record replaces.

## 2. Decision drivers

1. **Governing, not micromanaging.** The player's choices are slow,
   expensive, political and spatial: *where* to put a plant or a wall, *what*
   land may become. Placing fields one by one turns the game into a
   city-builder and pulls attention away from the governing loop.
2. **No false precision.** The simulation has no geometry below a cell
   except the hypsometric curve. A placed thing may carry only what the
   physics can use.
3. **Consequences from state.** Every close view of damage must be drawn from
   simulated state (§18.1: "no damage multipliers"; §31.2: absent channels are
   never faked). A canned flood animation would contradict the readability
   invariant of design §42.3.
4. **Determinism and replay.** Placement and zoning are commands in the input
   log; every sub-cell position that is not state comes from the presentation
   RNG of ADR-0018 §4.8, so the same place always looks the same.
5. **Bounded cost.** The local view is generated, not authored; it must not
   become a second game with its own rules.

## 3. Options considered

### 3.1 What the player places

- **A. Every building and field by hand.** Maximum control; contradicts
  driver 1 and design §46.2; the simulation would need per-field state it
  cannot resolve.
- **B. Major investments, plus zoning (chosen).** Matches the Investment and
  Regulation lever families of design §46.8. Land use stays an outcome.
- **C. Policy only, no map placement.** Simplest; loses the spatial
  trade-offs that make adaptation interesting (which coast to wall, which
  valley to dam) and the sense of owning a footprint.

### 3.2 Where a placed investment sits inside its cell

- **A. A free 2D position stored in state.** The physics cannot use it, and
  it invites rules that depend on geometry the model does not have.
- **B. A cell plus a hypsometric band (chosen).** The band is the slice of
  the cell's area between two of its elevation quantiles. It is physically
  meaningful (floodplain against high ground) and uses only ADR-0005 data.
- **C. The cell only.** Cannot tell a plant on a river terrace from one on a
  ridge, which §18.1's flood boundary needs.

### 3.3 The close view

- **A. An interactive local scale.** Scope of a second game; breaks driver 5.
- **B. Authored cinematics per event type.** Cheap to make look good; shows
  damage the state did not produce.
- **C. A generated, watch-only patch (chosen).** Built from the cell's
  hypsometry, channels and sites, seeded per cell.

## 4. Decision

### 4.1 What the player does at each scale

| Scale | Shows | The player can | Detail from |
|---|---|---|---|
| Planet | ice caps, desert belts, cloud bands, the spread of cities, night lights | read trends; open overlays (§31.7) | channels only, as colour and texture |
| Region (a basin or a coast, tens of cells) | towns, fields, plants, dikes, a river in flood, a drying valley | **place investments** (§4.2); **paint zoning** (§4.3); open lever previews (design §46.8) | channels; instanced assets (§31.5); sites (§4.5) |
| Local (one city or valley) | flooded streets, failed crops, burnt forest, smog | **watch only**: opened by event staging (§31.6) or by the player on any of their cities | a generated patch (§4.6) |

No lever is reachable from the local view. Every command is issued at region
scale (or from panels), never by clicking inside a generated patch.

### 4.2 Investments

An investment is a player command (ADR-0014) with:

``` text
kind        power_plant(fuel) | industry | dike | rail_link | city_zone
cell        CellId
band        0..7     the area slice between hypsometry quantiles band and band+1
            (dike: replaced by protected_elevation_m; rail_link: a second CellId)
capacity    kind-specific, SI units
```

- **Validation** at command time: the band's lower quantile is above the
  current sea level (or the protected elevation behind an existing dike);
  kind-specific rules (a hydro plant needs catchment area, a dike needs a
  coastal or river cell). A refused command names the rule.
- **Lifecycle:** planned → under construction → operating → damaged →
  abandoned or decommissioned. Construction time and cost belong to the
  economy (ADR-0017), not to this record.
- **Physical effect only through existing paths:** emissions and waste heat
  tagged at source (§9.10; design §46.2), land-use fractions, and for a dike the
  protected elevation of §18.1. An investment adds no damage multiplier.
- **Exposure from the band:** an investment in band `b` is flooded when the
  water level in its cell exceeds quantile `b`. Damage follows from that, not
  from a probability attached to the kind.
- **A `city_zone`** marks where urban growth is allowed and encouraged; it
  does not create population. Growth comes from PopSim's urbanization
  (§29.6.2).

### 4.3 Zoning

Zoning is painted per cell at region scale. A zone is a constraint and an
incentive on land-use change, never a land-use fraction:

| Zone | Effect on land-use change |
|---|---|
| `farm` | cropland expansion allowed and subsidised at the rate the budget sets |
| `urban_growth` | urban expansion allowed (a `city_zone` investment raises it further) |
| `protected_forest` | clearing forbidden; compliance per design §46.8 ("low compliance") |
| `managed_retreat(elevation_m)` | no new development below the elevation; existing development relocates over the policy's horizon (§18.1) |

Unzoned cells follow the economy's default rules. Zones are civilization
state in ADR-0012's field-id range; zoning commands are in the input log.
Non-compliance is state too, so illegal clearing inside a protected zone is
visible and reportable.

### 4.4 Land use is drawn, never placed

Fields, farms, pasture, forest and the built-up area of cities are drawn from
the `farmland`, `urban`, `mining` and (R5) `vegetation_vigour` channels by the
procedural detail rules of §31.5. The player sees them change in response to
zoning and prices; they cannot drag one.

### 4.5 Sites in the `VisualFrame`

`VisualFrame` gains a list beside `events`:

``` text
VisualSite { kind, cell, band, state, capacity_norm, protected_elevation_m }
```

- Sites are part of the presentation contract like channels: absent before
  P1, never faked.
- At region scale a site is drawn as a landmark asset. Its 2D position within
  the cell is chosen by the presentation RNG keyed by
  `(world_seed, cell_id, detail_kind = site, site index)` among points of the
  region's detail terrain whose elevation lies in the site's band. The
  position is presentation, never state.
- Zoning is drawn as a player overlay (§31.7), not by the style. It is the
  player's own decision and is not gated by knowledge.

### 4.6 The local view

The local view is a patch of a few kilometres generated on demand from:

- the cell's **hypsometry quantiles**: `TerrainSnapshot` gains `hypsometry_m`
  (nine floats per cell) for presentation. This is a copy of slow-state data
  already snapshotted; PSNAP does not change;
- the cell's channels, and its neighbours' for the edges;
- the sites in the cell;
- the presentation seed `(world_seed, cell_id, detail_kind = local)`.

Rules:

1. **Terrain matches the cell.** The patch's area–elevation distribution
   reproduces the cell's nine quantiles within a tolerance (§5, V4).
2. **Water from state.** The water level is the elevation at which the
   cell's hypsometric fraction equals `water_extent` (R5); before R5, the sea
   level alone. A dike holds water below its protected elevation; if the
   level exceeds it, the wall is overtopped in the picture because it was in
   the state.
3. **Sites in their bands.** Each site stands on ground in its band, so a
   plant built on the floodplain floods in the patch when its band floods in
   the state.
4. **Fields, forest, fire from channels.** Field count follows `farmland`;
   the share browned, stunted or failed follows `dryness` and
   `vegetation_vigour`; burn scars follow `fire`; streets and blocks follow
   `urban`.
5. **Same seed, same place.** Revisiting a cell decades later keeps its
   layout. The view may scrub between stored snapshots for before-and-after,
   which is presentation time only.
6. **Watch-only.** Opening, closing, scrubbing or capturing the view changes
   no state and no tick (§31.6, §8).
7. **Absent is neutral.** A channel absent at the current milestone shows
   its neutral state in the patch; the patch never invents a flood, a drought
   or a fire.

### 4.7 Boundaries

- Nothing at region or local scale writes authoritative state; only commands
  do, through ADR-0014's validation and the input log.
- The band is state; the position within a cell is not.
- Generated detail obeys §31.5's consistency test at both scales.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Rendering at region and local scale leaves every state hash unchanged | regression test (extends ADR-0018 V6) |
| V2 | Commands at region scale are recorded and replay bit-identically; no command can be issued from the local view | command tests (ADR-0014) |
| V3 | An investment refused by validation names the failed rule | unit test |
| V4 | Local terrain reproduces the cell's hypsometry: each generated quantile within 2 % of the cell's elevation range | headless generator test |
| V5 | Flooded area of the patch equals `water_extent` within 2 %; a site in band `b` is wet exactly when the state's water level exceeds quantile `b` | headless generator test |
| V6 | Field count and failed-field share stay within tolerance of `farmland` and `dryness` | headless generator test (§31.5) |
| V7 | Same `(world_seed, cell_id)` gives the same patch and the same site positions across runs and platforms | determinism test |
| V8 | Readability: city dry vs flooded, and crops normal vs drought, pass ADR-0018 §4.9 in the local view of both styles | readability harness |

V4–V7 run headless on synthetic snapshots before any civilization state
exists, so the generator can be built and tested on the rendering track.

## 6. Consequences

- The player's attention stays on governing: where to invest, what land may
  become, what to protect. The footprint still reads as theirs.
- Floods and droughts get a close, honest consequence view that is generated
  from the same state as the numbers, so the picture and the reports cannot
  disagree.
- The hypsometric band gives investments real flood exposure with no new
  physics; dikes and managed retreat use the boundary §18.1 already defines.
- The local view needs a terrain generator constrained by quantiles, and an
  asset set per style. That is the largest new presentation cost, and it is
  paid once for every event type.
- `VisualFrame` grows a site list; at the expected scale (hundreds to low
  thousands of sites) this is small next to the channels.

## 7. Milestone mapping

| Track | Uses this record for |
|---|---|
| R5 | forest and dryness assets at region scale; flood water in the local view from `water_extent` |
| R8 | land-use assets, sites at region scale, the local view for cities, staging into it |
| P1 (S-track) | investment and zoning commands, after ADR-0012 and ADR-0014 |
| P2 | dikes and managed retreat (§18.1) drawn and staged |

## 8. Open questions

1. Are eight hypsometric bands enough to separate floodplain from terrace in
   steep coastal cells, or should a site carry a continuous area fraction?
2. River floods: until M9 provides a water level, `water_extent` is a
   fraction; this record converts it to a level through the inverse
   hypsometry. Is that accurate enough for river valleys, where water is not
   a level surface across the cell?
3. May the player open the local view on another civilization's city, and
   what does fog (ADR-0019) show there?
4. Should a `rail_link` be a site pair or a new edge-based list?
