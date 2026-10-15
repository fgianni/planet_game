# ADR-0022 — Player placement, zoning and a continuous Earth-viewer camera

- **Status:** Accepted
- **Date:** 2026-10-10; revised 2026-10-11 (the three camera scales become altitude bands of one continuous view, §4.8); revised 2026-10-13 after review (detail is a function of position, §4.9; the history slider's data, §4.8)
- **Accepted:** 2026-10-13 (the presentation parts may start now, as task R1-04; the command and state parts still wait for ADR-0012 and ADR-0014)
- **Amended:** 2026-10-14 — §4.8 precision: camera-relative rendering, from the R1-04 step 0 spike; 2026-10-15 — §4.9 terrain and V4: curves hand over to the corners, so the terrain has no steps at cell edges
- **Milestone:** none in P0. The presentation parts may be prototyped on the rendering track (R5, R8) against synthetic frames; the command and state parts must not be implemented before ADR-0012 (society state and regions) and ADR-0014 (player commands) are accepted.
- **Context documents:** Design Record v1.4 §8.4 (the player's identity), §42.5 (camera scales), §42.6 (civilization on the planet), §42.8 (staging events), §46.2 (what we avoid from Civ), §46.8 (player levers as government); `docs/DEVELOPMENT_SPEC_v0_8.md` §9.10, §18.1, §26.1 items 5 and 7, §31.2, §31.5–§31.7
- **Related:** ADR-0005 (sub-cell hypsometry), ADR-0012 and ADR-0014 (reserved, not yet written), ADR-0018 (presentation boundary, channels, presentation RNG), ADR-0019 (reserved: fog of knowledge)
- **Amends on acceptance:** ADR-0018 §4.1 (`VisualFrame` gains a site list, §4.5 below) and §4.8 (procedural detail is keyed by `(world_seed, detail_kind)` and sampled by position, not keyed by cell, §4.9); the presentation `TerrainSnapshot` gains the per-cell hypsometry (§4.6); specification §31.5 (the camera-scale table becomes the altitude bands of §4.8, and its presentation RNG key changes as in ADR-0018 §4.8) and §31.10 (the camera part of R8 moves to task R1-04, which needs only M2 terrain). Neither PSNAP nor any solver changes.

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

On 2026-10-11 the owner added a second decision: **the view should work like
Google Earth**, one globe the player zooms continuously from orbit down to a
valley, with the camera tilting towards the horizon as it descends, rather
than three separate camera modes. The planet, region and local scales of
design §42.5 and specification §31.5 survive as *altitude bands* of that one
view (§4.8); what the player may do in each band is unchanged.

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
- **The Godot client** (`godot/`) builds one `ArrayMesh` for the whole planet
  at a fixed subdivision (`PlanetMeshNode::rebuild`, default L5; L6 since
  the land rendering survey) and orbits a
  camera at 1.3 to 10 planet radii (`scripts/main.gd`). There is no level of
  detail, no tilt and no terrain below cell resolution: a continuous view
  needs all three.

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
5. **Bounded cost.** The local band is generated, not authored; it must not
   become a second game with its own rules.
6. **One continuous view.** The player never leaves the globe to reach a
   region or a valley; they zoom there. Detail appears as they descend and
   is the same whichever path they took to reach a place.

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

### 3.4 How the scales relate

- **A. Three camera modes with transitions (this record's 2026-10-10
  draft).** Simple to build; each scale can have its own renderer. Breaks
  driver 6: entering the local view is a cut, and the player loses where
  they are.
- **B. One continuous view with altitude bands (chosen, 2026-10-11).** The
  Earth-viewer model: streamed terrain tiles whose detail rises as the camera
  descends, a tilt that follows altitude, fly-to for event staging. Costs a
  level-of-detail terrain system in the client; gains the sense of one real
  planet, and makes the snapshot history scrubbable like Google Earth's
  historical imagery.

## 4. Decision

### 4.1 What the player does in each altitude band

The three scales are bands of one continuous camera (§4.8). Bands blend over
a transition range; nothing switches abruptly.

| Band | Shows | The player can | Detail from |
|---|---|---|---|
| Planet | ice caps, desert belts, cloud bands, the spread of cities, night lights | read trends; open overlays (§31.7) | channels only, as colour and texture |
| Region (a basin or a coast, tens of cells) | towns, fields, plants, dikes, a river in flood, a drying valley | **place investments** (§4.2); **paint zoning** (§4.3); open lever previews (design §46.8) | channels; instanced assets (§31.5); sites (§4.5) |
| Local (one city or valley) | flooded streets, failed crops, burnt forest, smog | **watch only**: reached by zooming in anywhere, or flown to by event staging (§31.6) | streamed local tiles (§4.6) |

No lever is reachable from the local band. Every command is issued at region
altitude (or from panels), never by clicking inside a generated patch: below
the region band the placement and zoning tools are disabled and the cursor
says so. Zooming out to region altitude re-enables them.

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
  water level in its cell exceeds quantile `b`, the band's lower edge. This
  is deliberately conservative: a band partly under water counts as
  flooded, since the model has no position within the band. Damage follows
  from that, not from a probability attached to the kind.
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
  `(world_seed, detail_kind = site, cell_id, site index)` among the points of
  the cell whose hypsometric rank (§4.9) lies in the site's band. The rank
  does not depend on the camera, so the site stands on the same ground, in
  its band, in the region and the local band. The position is presentation,
  never state.
- Zoning is drawn as a player overlay (§31.7), not by the style. It is the
  player's own decision and is not gated by knowledge.

### 4.6 The local band

The local band shows terrain tiles of a few kilometres, generated on demand
and streamed as the camera descends (§4.8). Each tile is generated from:

- the cell's **hypsometry quantiles**: `TerrainSnapshot` gains `hypsometry_m`
  (nine floats per cell) for presentation. This is a copy of slow-state data
  already snapshotted; PSNAP does not change;
- the cell's channels, and its neighbours' for the edges;
- the sites in the cell;
- the presentation detail fields of §4.9, sampled at the tile's points.

Rules:

1. **Terrain matches the cell.** The terrain's area–elevation distribution
   over the cell reproduces its nine quantiles within a tolerance (§4.9;
   §5, V4). No single tile, which covers a few kilometres of a cell about
   110 km across, is held to the whole curve.
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
6. **Watch-only.** Descending into, leaving, scrubbing or capturing the band
   changes no state and no tick (§31.6, §8).
7. **Absent is neutral.** A channel absent at the current milestone shows
   its neutral state in the patch; the patch never invents a flood, a drought
   or a fire.

### 4.7 Boundaries

- Nothing at region or local scale writes authoritative state; only commands
  do, through ADR-0014's validation and the input log.
- The band is state; the position within a cell is not.
- Generated detail obeys §31.5's consistency test in every band.
- The camera's altitude and path are presentation; they never reach state
  and never change a tile's content.

### 4.8 The continuous view

**Camera.** One globe camera with the Earth-viewer controls: drag to turn
the globe, scroll or pinch to change altitude towards the point under the
cursor, and a tilt that follows altitude (looking straight down from orbit,
towards the horizon near the ground). The player may override the tilt. Altitude
runs from several planet radii to about a hundred metres above the local
terrain, and the camera never enters the ground.

**Bands.** The bands of §4.1 are defined by camera altitude above the
terrain, as fractions of the shipped cell size `d` (about 110 km at L6):

| Band | Altitude | Terrain | Blend |
|---|---|---|---|
| Planet | above ≈ 10 `d` | the cell mesh, corners interpolated | — |
| Region | ≈ 0.3 `d` to 10 `d` | refined tiles, procedural relief inside each cell (§31.5) | cross-fade over the band edges |
| Local | below ≈ 0.3 `d` | the §4.6 tiles: quantile-constrained terrain, sites, fields, streets | cross-fade over the band edge |

The thresholds are style-independent constants of the client, so all
styles share the interaction rules. The planet–region threshold scales with
the shipped cell size `d`, because the cell mesh is what the planet band
draws. The region–local threshold is fixed in metres (about 30 km, which is
0.3 `d` at L6), so a city looks the same size at every mesh level.

**Terrain tiles.** The client replaces its single fixed mesh with a
quadtree of terrain tiles on the sphere (chunked level of detail). The
coarsest level is the cell mesh; each finer level adds procedural relief
inside cells, constrained so that the area–elevation distribution of every
cell still matches its hypsometry (V4) and neighbouring cells meet without
cracks. Tiles are chosen by screen-space error, generated off the render
thread, and cached; a missing tile is drawn from its parent until it arrives,
never left as a hole. A tile only samples the detail fields of §4.9 at its
points: its content depends on `world_seed`, the positions it covers and the
current `VisualFrame`, never on the tile's id, its level or the camera's
path (driver 6). Neighbouring tiles and levels therefore agree wherever they
overlap.

**Earth-viewer features mapped to the design.**

| Earth viewer | Here | Governed by |
|---|---|---|
| Historical imagery slider | scrubbing the stored snapshot history for the place in view (see below) | ADR-0003, ADR-0020; presentation time only |
| Layers panel | overlays and lenses | §31.7 |
| Blurry areas before tiles load | the fog of knowledge: unexplored land stays at the detail the society has mapped | ADR-0019 (reserved) |
| "Fly to" | event staging and the newspaper's links fly the camera to the place | §31.6 |
| Place names and search | generated names of regions, cities and named events | design §46 naming grammar |

The fog row is a deliberate difference from Google Earth: blur means "not
yet mapped by this society", not "still loading". The two must look
different (V12).

**History.** The ADR-0003 history stores the slow state only. A past
moment therefore shows only the channels that can be computed from slow
state (terrain, snow, sea ice, surface temperature; later land use and
water extent); fields that are derived each step, such as precipitation,
winds and the climatology, are absent there and shown neutral, never
interpolated from the present. Turning a stored snapshot into a
`VisualFrame` is a PlanetSim service behind ADR-0020's read contract; the
client never opens PSNAP files. The slider waits for ADR-0020.

**Look.** Continuous zoom is a camera and a terrain system, not a style.
Every style pack draws every band; a realistic, satellite-like pack is one
pack among others and stays a non-goal before P1 (specification §31.12).
Styles add only per-band assets and materials, sourced under ADR-0018 §11.

**Precision.** Godot uses 32-bit floats for positions by default; near an
Earth-sized planet that is about 0.8 m, enough to make terrain and camera
jitter in the local band. The client must therefore render either camera-
relative (tile origins and the camera kept in `double` on the CPU, with only
small offsets reaching the GPU) or with a double-precision engine build.
Task R1-04 step 0 measures both and records the choice here as an amendment
before any tile code is written. If neither meets the pass line (under
0.5 px of jitter at 100 m, no z-fighting), the engine choice for the client
is reopened; PlanetSim is unaffected.

*Amended 2026-10-14 (R1-04 step 0):* the client renders **camera-relative**
in a stock Godot build. Tile origins and the camera are kept in `double` on
the CPU; each frame a tile's transform is set to `origin − camera` and the
camera stays at the scene origin, so the GPU only sees offsets from the
camera, and a 32-bit offset errs by about 10⁻⁷ of its length: 0.1 mm at
1 km, 1 cm at 100 km, always far under a pixel at that distance. In the
spike (task report §5) it removed all precision jitter in both renderers:
its frame-to-frame marker jitter at 100 m equals the measurement's floor
(the same figure with every precise configuration, in a stock and a
double-precision build), where absolute coordinates jitter by 2 px RMS and
up to 6 px. A double-precision build alone does not pass in the
Compatibility renderer the project and CI use (its transforms still reach
the GPU in single precision), is clean only under Forward+, and the 4.2
extension crashed when built against it. Depth is not a constraint: with
the near plane at 0.5 % of the altitude, surfaces 0.1 m apart separate
cleanly to 10 km from 100 m in Compatibility, and to the 30 km horizon
under Forward+ in Godot 4.7; beyond 10 km such separations are under a
hundredth of a pixel. Steps 1–6 of R1-04 are built on this choice, and the
V9 flight of the final tile system is held to the same pass line, measured
against the same floor.

**Budget.** The 60 frames-per-second target of §31.9 holds at every
altitude, with tile generation and upload spread over frames.

### 4.9 Detail is a function of position

Survey step 3 (`sim/presentation/detail_seed.hpp`) found that per-cell
random keys put a seam at every cell edge. Every procedural quantity in
every band is therefore a deterministic function of the position on the
sphere and of `(world_seed, detail_kind)`, drawn from ADR-0018's reserved
presentation stream. Tiles, levels and cells only sample it; none of them
seeds it. This amends ADR-0018 §4.8 and specification §31.5, whose key
`(world_seed, cell_id, detail_kind)` stays valid only for per-cell choices
that are not spatial fields, such as a site's index among its candidates.

**Terrain.** Sub-cell elevation is built by rank, so that it matches the
cell's hypsometry at any resolution:

``` text
n(x)     continuous noise for detail_kind = terrain, with known distribution F
r(x)     = F(n(x)) in [0, 1]           the point's hypsometric rank
h(x)     = Q_cell^-1(r(x))             the cell's inverse hypsometry, interpolating
                                        its nine quantiles at ranks 0, 1/8, ..., 1
```

- Every point is computed locally and identically at every level, so tiles
  need no neighbours to generate and cannot crack against them.
- The area–elevation distribution over the cell follows its quantiles
  statistically, to within the noise's sampling over the cell's area (V4).
- Near a cell's edge, `h` blends towards the neighbour's curve over a
  fixed width so that the surface is continuous. That blend is the only
  departure from the cell's curve, and V4's tolerance bounds it.

*Amended 2026-10-15 (R1-04):* a blend narrow enough to keep every quantile
within 2 % (0.002 cell spacings, about 220 m at L6) leaves a step at every
cell edge where neighbouring curves differ, a groove or cliff drawn along
the whole cell grid. The terrain therefore hands over smoothly instead:
the curve `Q_x` and the rank `r(x)` at a point are the cell's own (its
quantiles, and its distribution of `n` estimated over the cell, so ranks
are uniform there) in the cell's interior, and towards its edges blend to
those of its corners (each the mean of its three cells'), with weights
from the point's place in the cell mesh's fan triangle: the cell's weight
is a smoothstep of its barycentric weight over its last 15 %. On an edge
only the two shared corners remain, so neighbouring cells meet exactly and
without a step. A cell's area-elevation distribution is then its own curve
mixed, near its edges, with its neighbours': V4 bounds that mixture.
- Band `b` is the region `r(x) ∈ [b/8, (b+1)/8)`, so a site's ground (§4.5)
  and its flooding (§4.6 rule 3) are the same at every altitude.
- `n` is one fixed field with all its octaves. A level only samples it
  more or less densely, and coarse levels draw a filtered version for
  display, so a point's rank, and its band, never depend on the level.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Rendering at region and local scale leaves every state hash unchanged | regression test (extends ADR-0018 V6) |
| V2 | Commands at region scale are recorded and replay bit-identically; no command can be issued from the local band | command tests (ADR-0014) |
| V3 | An investment refused by validation names the failed rule | unit test |
| V4 | Terrain follows the cell's hypsometry (amended 2026-10-15): over points sampled across the whole cell, the handover towards its corners included, quantiles 1-7 within 10 % of the cell's elevation range on average over cells and 25 % in every cell; by area (the share below each quantile against its rank) within 8 % on average; no more than 20 % of a cell beyond its own minimum and maximum | headless generator test |
| V5 | Flooded area of the patch equals `water_extent` within 2 %; a site in band `b` is wet exactly when the state's water level exceeds quantile `b` | headless generator test |
| V6 | Field count and failed-field share stay within tolerance of `farmland` and `dryness` | headless generator test (§31.5) |
| V7 | The same `world_seed` and position give the same terrain, detail and site positions across runs, platforms and tile levels | determinism test |
| V8 | Readability: city dry vs flooded, and crops normal vs drought, pass ADR-0018 §4.9 in the local band of both styles | readability harness |
| V9 | Zooming from orbit to the ground and back shows no crack between tiles and no tile left missing for more than a frame (a parent stands in) | headless capture test |
| V10 | Path independence: the same tile reached by two different camera paths renders identically | determinism test |
| V11 | 60 frames per second at L6 in the `stylised` style at three altitudes (planet, region, local), with tile generation running | performance gate (§31.9) |
| V12 | Fog-of-knowledge blur and loading blur are distinguishable to the readability harness | readability harness (ADR-0018 §4.9) |
| V13 | Under 0.5 px of vertex jitter with the camera holding still 100 m above the terrain, and no z-fighting | capture test (R1-04 step 0, then in the V9 flight) |

V4–V7, V9, V10 and V13 run headless on synthetic snapshots before any
civilization state exists, so the generator and the camera can be built and
tested on the rendering track.

## 6. Consequences

- The player's attention stays on governing: where to invest, what land may
  become, what to protect. The footprint still reads as theirs.
- Floods and droughts get a close, honest consequence view that is generated
  from the same state as the numbers, so the picture and the reports cannot
  disagree.
- The hypsometric band gives investments real flood exposure with no new
  physics; dikes and managed retreat use the boundary §18.1 already defines.
- The local band needs a terrain generator constrained by quantiles, and an
  asset set per style. That is the largest new presentation cost, and it is
  paid once for every event type.
- The client trades its single planet mesh for a streamed tile quadtree.
  That is the largest new cost of the 2026-10-11 revision, and it is
  independent of civilization state, so it can be built and tested now on
  today's terrain (R1-04).
- The planet reads as one real place: the player zooms to a flood instead of
  cutting to it, and scrubbing history works anywhere on the globe.
- `VisualFrame` grows a site list; at the expected scale (hundreds to low
  thousands of sites) this is small next to the channels.

## 7. Milestone mapping

| Track | Uses this record for |
|---|---|
| R1-04 | the continuous camera, the band rules and the terrain tile quadtree on today's terrain (§4.8); needs only M2 |
| R5 | forest and dryness assets at region scale; flood water in the local band from `water_extent` |
| R8 | land-use assets, sites at region scale, the local band for cities, staging flights into it |
| P1 (S-track) | investment and zoning commands, after ADR-0012 and ADR-0014 |
| P2 | dikes and managed retreat (§18.1) drawn and staged |

## 8. Open questions

1. Are eight hypsometric bands enough to separate floodplain from terrace in
   steep coastal cells, or should a site carry a continuous area fraction?
2. River floods: until M9 provides a water level, `water_extent` is a
   fraction; this record converts it to a level through the inverse
   hypsometry. Is that accurate enough for river valleys, where water is not
   a level surface across the cell?
3. May the player zoom into the local band over another civilization's city, and
   what does fog (ADR-0019) show there?
4. Should a `rail_link` be a site pair or a new edge-based list?
5. ~~How fine should region-band relief be before the terrain generator
   implies valleys and ridges the hypsometry does not support? The local band
   has V4; the region band needs its own tolerance.~~ Answered 2026-10-15
   (R1-04): one generator draws both bands, and V4, amended, holds at every
   resolution, since every point is the same function of position. A tile
   too coarse to show sub-cell relief (vertex spacing over 0.2 of a cell)
   draws the filtered surface of §4.9 instead, so the region band never
   implies relief finer than it can resolve.
6. ~~Should the band thresholds scale with the shipped mesh level, or stay
   in metres?~~ Answered 2026-10-13 in §4.8: planet–region scales with the
   cell size, region–local is in metres.
