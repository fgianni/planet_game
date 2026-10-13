# Vertical slice — Industrial Dawn, one region, 1880–1980

- **Status:** proposed (2026-10-11)
- **Track:** parallel gameplay track (specification §25), in the GDScript
  prototype `prototype/climate_planet_0.5/`
- **Governing documents:** specification v0.8 §16, §17.1, §25, §29, §30.11,
  §31.5; design v1.4 §46; ADR-0022 (proposed, PR #4); Systems Architecture
  doc, "Research follows the planet, not Earth's history"
- **Must not touch:** PlanetSim (`sim/`), its tests, PSNAP, the Godot client
  in `godot/`, or the prototype's existing `game.tscn` and its balance test
  (they stay the oracle of `docs/prototype-oracle.md`)
- **Task documents:** G1-01 to G1-06 in `docs/tasks/`

## 1. Why a slice, and why now

The design has grown much faster than anything a player can touch. Design
v1.4 and specification v0.8 now cover population and society, public
finance, food, conflict, the fog of knowledge, style packs, procedural
cities and planet-driven technology routes. PlanetSim is at P0 / M7 and,
by its own roadmap (§13), will not host a civilization before M15 and P1.

Every system added before the loop is playable is a guess about what is
fun. Specification §25 already names the fast lane for testing such
guesses: the GDScript prototype. This slice gives that lane a concrete
target. It is the smallest game that crosses every layer of the design
once:

``` text
planet (prototype climate model)
  -> exposure (local rain, heat, coast, air)
    -> cohorts (needs, grievance, trust)
      -> government (speech, budget, investment, research)
        -> routes (capability reached by a planet-dependent route)
          -> fluxes back to the planet (CO2, land use, coast use, pollution)
```

It answers whether the loop holds attention, and it settles the
playtest questions of §25 with players rather than argument. What it learns
becomes ADRs and parameters for P1; none of its code is merged into
PlanetSim (§25: retire the prototype at parity, never by merging it).

## 2. Scope

| Dimension | Slice | Rationale |
|---|---|---|
| Start | 1880, `industrial_dawn` (§16), coal steam already known | the scenario the design commits to first |
| End | 1980, or earlier on collapse | long enough for local loops to bite and for the global signal to start emerging from noise |
| Turn | 5 years, 20 turns | the prototype's cadence; a target session of 45–60 minutes. Pacing is a playtest question (§9) |
| Space | the prototype's region (≈26 land cells plus ≈12 coastal sea cells) on its 2,562-cell planet | one region is enough to test every link; several regions are §5 alpha |
| Planet | the prototype climate model, spun up to 1880 (≈290 ppm) | already calibrated (oracle §2) |
| Rest of world | follows a historical CO2 curve, damped or amplified by the player's example | keeps the global signal honest without other civilizations |
| Capabilities | 2: *turn heat into work*, *light and power* | the minimum that makes route choice a real decision |
| Routes | 3: coal steam, algae, river dams | each leads to a different crisis (§4) |
| Cohorts | 3: mill towns, coast and valley, notables | enough for a conflict of interests, few enough to read |
| Levers | 4: speech, budget split, major investment, research direction; plus one zoning brush | ADR-0022's split between placed investments and painted zones |
| Knowledge | stages *local records* and *climatology* only (§17.1) | the climate view starts absent, and the first unlock is earned in play |
| Camera | planet and region scale; local scale only as a still "consequence card" | ADR-0022's watch-only local scale is a stretch goal (G1-06) |

## 3. The player's job

The player is the region's civilization acting through its government
(design v1.0 §8.4, the hybrid identity). Their aim is stated on the first
screen in one sentence: *keep your people fed, employed and willing to
follow you for a hundred years.* Climate is not mentioned. The game is
designed so that by 1980 the player has met it anyway, through their own
fields, rivers and coast, before their science can name it.

There is no score on screen during play. The end report (§7) shows the
four indicators of §30.11 that the slice can compute.

## 4. Routes

Routes are data (`data/routes.json`). Each record declares, in physical
terms, what the planet must offer, what discovers it, what it consumes and
produces, its climate sensitivity and its counterweights. A route the
planet cannot support never appears.

### 4.1 Capability: turn heat into work

| | Coal steam | Algae engines |
|---|---|---|
| Known at start | yes | no |
| Planet condition | coal basin under at least one region land cell (always present: the fossil trap) | ≥ 4 coastal sea cells that are shallow (shelf), sunlit, and warmer than 14 °C annual mean |
| Discovered by | — | exposure: cumulative fishing on suitable shelf cells; research direction "the sea" multiplies the chance |
| Output per building | high, steady | lower; a share feeds its own drying and extraction (low energy return) |
| Pollution and fluxes | CO2; soot that raises local air grievance; urban heat | near-zero net CO2; shelf area taken from fishing; nutrient load that can tip coastal cells hypoxic; rotting-algae gas episodes on the coast |
| Climate sensitivity | cooling-water shortfall in heat waves | grows better as the sea warms, until toxic blooms cut output and close beaches above a temperature threshold |
| Counterweight felt by | mill towns (air), everyone later (warming) | coast cohort (lost fishing, foul air, dead zones) |

### 4.2 Capability: light and power

| | Coal gas grid | River dams |
|---|---|---|
| Known at start | yes (gasworks) | no |
| Planet condition | coal steam adopted | a land cell with high runoff and steep relief upstream of another region cell |
| Discovered by | — | exposure: mills built on rivers; research direction "the rivers" |
| Output | steady, needs coal | high, varies with the cell's rainfall each turn |
| Counterweight | CO2, soot | a valley cell is flooded (farms and homes there are lost, once); fish below the dam decline; downstream delta cells lose silt and farm yield slowly |
| Climate sensitivity | — | output falls in droughts, which the player's own land clearing upwind can cause (prototype fast loop) |

### 4.3 Rules the slice must demonstrate

1. **No clean route.** Every route's costs arrive through state the
   prototype already simulates or that this slice adds (coast health, air,
   fish), never through a penalty number.
2. **The planet decides.** On a seed with no suitable shelf the algae route
   never appears. G1-02 includes a seed sweep that finds seeds of each kind.
3. **Lock-in.** Route buildings last 6 turns or more; research effort on a
   capability is split in proportion to its installed base, so the route a
   society already uses keeps improving.
4. **Climate can break a route.** The two sensitivities above must fire at
   least once in a typical game on a suitable seed.
5. **The fossil trap stays tempting.** Coal is the cheapest energy per
   unit of money in every year of the slice.

Hidden side effects, depletion, rebound, identity and trade (Systems
Architecture doc, "More routes") are **not** in the slice; they are alpha.

## 5. Cohorts

Three cohorts, each a single aggregate (no age structure, no
representative citizens yet):

| Cohort | Lives in | Cares about |
|---|---|---|
| Mill towns | urban cells | jobs, wages, air, food price |
| Coast and valley | farm and coastal cells | harvests, fish, flood losses, land taken by projects |
| Notables | the capital cell | treasury, growth, order |

Each cohort carries, per turn:

- **Needs met** (0–1): from production, prices and losses in its cells.
- **Grievance** (stock): rises with unmet needs and with harms whose
  source the cohort can see in its own cells (soot, a flooded valley, a
  dead bay). It decays slowly. This is the slice's version of §29.6.3.
- **Trust** in the government (0–1): moved by kept and broken promises
  with the asymmetry of §29.6.7 (default ratio 4, a playtest question).
- **Support** = f(needs met, grievance, trust); the support of the three
  cohorts weighted by population gives legitimacy (§30.11).

Grievance above a threshold produces an event the player sees on the map
and in the newspaper: petition, then strike, then riot (the escalation
ladder of the Systems Architecture doc). A riot costs output in its cells
for a turn.

## 6. Levers

Each turn the player makes all four choices (any may be "no change"). The
turn should take two to four minutes.

| Lever | Choices | Delay | Notes |
|---|---|---|---|
| **Speech** | one promise from a short menu, tied to a cohort and a measurable target: "clean air in the mill towns", "no family will lose its land to a project", "cheap light in every home", "the bay will feed us"; or no promise | target checked after 2 turns | the slice's promise ledger (§29.10). A kept promise raises that cohort's trust; a broken one lowers it four times as much |
| **Budget split** | three sliders: works, research, relief | immediate | relief lowers grievance; research is the only way to discover routes faster than exposure alone |
| **Major investment** | place one or two buildings from the prototype palette plus the route buildings that the society has discovered | builds in 1 turn | ADR-0022: the player places only major investments |
| **Research direction** | one of: the mines, the sea, the rivers, the sky (instruments) | accumulates | "the sky" is the only route to the *climatology* stage |
| **Zoning brush** | paint cells *protected* or *open* | immediate | protecting coast blocks algae farms there; protecting a valley blocks a dam |

Fields, homes and city growth are not placed; they emerge from the
prototype's existing population and food rules.

## 7. A turn, minute by minute

| Step | What the player sees | What they decide |
|---|---|---|
| 1. The newspaper | headline and two items, written from the last turn's events; framing follows the region's concern (prototype) | nothing; this frames the turn |
| 2. The region | the planet, then the camera settles on the region; changes since last turn pulse once (new fields, soot, a dead bay, a lake behind a dam) | nothing yet |
| 3. The cohorts | three portraits with a mood line each and one sentence of their main complaint, worded from the dominant grievance source | which group to serve this turn |
| 4. The records | own-region temperature and rain record since 1880, with gaps; after *climatology*, regional anomaly and a first, very uncertain global mean | whether to fund "the sky" |
| 5. Decide | the four levers and the brush | the turn's choices |
| 6. Five years pass | the climate model runs; the planet animates at planet scale for a few seconds | — |
| 7. Consequence card | at most one, for the largest local event: a still of the affected cells with a one-line cause chain ("the bay died: algae farms + warm summer → no oxygen") | — |

**Design bug rule:** if the playtest log shows a turn in which no lever
changed and no player comment was recorded, that turn has no interesting
decision. Every such turn is reported in the playtest summary.

### 7.1 The arc the slice is designed to produce

- 1880–1900: coal is the obvious choice; mills grow, the first fields
  replace forest, the mill towns' air worsens. Fishing on a suitable shelf
  starts accumulating algae exposure.
- 1900–1930: a route is discovered (algae or dams) and becomes a real
  alternative. Adopting it creates a new interest group's grievance
  (fishers, valley farmers). The player's first promises start coming due.
- 1930–1960: local loops bite: rain downwind of cleared land falls; a dam
  fails in a drought; a warm summer turns the algae bay toxic. If the player
  funded "the sky", *climatology* arrives and shows a trend in their own
  record that they cannot yet explain.
- 1960–1980: the global mean, uncertain and noisy, begins to move. The end
  report places the society's history next to the counterfactual planet
  (the prototype's shadow model) and, in the debrief, next to Earth's
  (§30.12).

## 8. End report

Four indicators from §30.11, each over time, plus one line of the
society's own history:

- living conditions (needs met, weighted toward the worst-off cohort);
- planetary state (global warming, the region's fingerprint from the
  shadow model, coast health);
- legitimacy (support, protests);
- resilience (loss and recovery after each event);
- the society's history in one line, generated from its routes: "the
  algae age, 1912–1980" or "a coal society, 1880–1980".

There is no win or lose label in the slice. Endings and weights are an
open question of §27 that the playtests feed.

## 9. Questions the slice must answer

Inherited from §25, in its order, then new ones. Each has a measure taken
from the action log, the in-game survey (G1-05) or a think-aloud session.

| # | Question | Measure | Pass |
|---|---|---|---|
| Q1 | Fast-loop causality: can a player name the cause of a local change within two turns, unprompted? | think-aloud transcript | ≥ 3 of 5 players for drying downwind and for the dead bay |
| Q2 | Does science pay, and do players feel it did? | games with ≥ 20 % research vs fewer: time to first route switch; survey | measurable difference, and players who funded it say so |
| Q3 | Fast-loop strength: does local housekeeping crowd out the global story? | share of turns whose levers address a local issue only; survey "what was this game about?" | at least half the players mention climate or the planet in their answer |
| Q4 | Route legibility: can a player explain why their society took the route it did? | end-of-game question | names the planet condition or the exposure ("we had a warm shallow sea") |
| Q5 | Counterweight legibility: does the player see the new route's crisis as caused by it? | think-aloud at the first coast or valley event | names the route without help |
| Q6 | Promise tension: do players make promises, and do broken ones change their behaviour? | promises per game; behaviour in the turn after a broken one | ≥ 1 promise per 4 turns; the next turn changes a lever toward the broken target |
| Q7 | Decision density | turns with no lever change and no comment | ≤ 2 per game |
| Q8 | Pacing | session length, turn time, survey | 45–60 minutes; no player asks to skip turns before 1940 |

Q1 and Q3 close spec §25 items 1 and 5; Q2 closes item 3. Items 2 (the
projection cone) and 4 (tipping legibility) need stages and tipping
elements beyond the slice and stay open.

## 10. Cut list

Every section of the specification that the slice touches, tagged by
when it is needed. *Slice* means a reduced version is in this slice;
*alpha* means the next gameplay milestone (several regions, the next
knowledge stages); *later* means P1 or after.

| Specification | Tag | Slice version |
|---|---|---|
| §7.1 counterfactual planet | slice | the prototype's shadow model, unchanged |
| §7.2 projection ensemble | later | — |
| §9.14 fast local loops | slice | the prototype's land-use drying and heat island, plus coast health and air |
| §13.3 tipping elements | alpha | the prototype's overturning stays, not staged |
| §15.1 consequence ledger | slice | one cause chain per consequence card |
| §16 scenario system | slice | `industrial_dawn` as constants in the slice state, not scenario data |
| §17.1 knowledge stages | slice | *local records* and *climatology* only |
| §18.1 adaptation | alpha | — |
| §29.6.1–2 demography, migration | later | population is one number per cohort |
| §29.6.3 needs and grievance | slice | three cohorts, §5 |
| §29.6.4 consumption, rebound | later | — |
| §29.6.5 opinion dynamics | later | — |
| §29.6.6 media and statements | slice | speech only |
| §29.6.7 credibility, promise ledger, §29.10 | slice | §6 |
| §29.6.8 mobilisation | slice | petition, strike, riot |
| §29.6.9 elections | alpha | legitimacy only; no election in the slice |
| §29.6.10 climate perception | alpha | the prototype's concern value |
| §29.7 representative citizens | alpha | one sentence per cohort |
| §29.8 political actors | later | — |
| §30.2–30.5 natural forcing, carbonate, biodiversity, pollution | slice (pollution, coast health) / later | air and coast health only |
| §30.6 food system | slice | the prototype's harvests, plus fish |
| §30.7 public finance | slice | treasury and the three-way budget |
| §30.8 conflict | later | — |
| §30.9 future generations | alpha | — |
| §30.10 lifestyle norms | later | — |
| §30.11 indicators and endings | slice | four indicators, no endings |
| §30.12 learning debrief | slice | Earth series beside the region's, from 1880 |
| §31 style packs | later | the prototype's look |
| §31.5 camera scales | slice | planet and region; consequence card for local |
| §32 fog of knowledge | alpha | the climate view is gated (§17.1); no geographic or social fog |
| Systems Architecture: routes | slice | 2 capabilities, 3 routes, §4 |
| Systems Architecture: procedural cities | alpha | — |
| Systems Architecture: hidden side effects, depletion, identity, trade | alpha | — |

Implementation agents working on the slice read this document and the
G1 task documents; they do not pull features from the specification's
later sections into it.

## 11. Deliverables

All paths are inside `prototype/climate_planet_0.5/`.

- A new scene `slice.tscn` beside `game.tscn`, with its state in
  `scripts/slice_state.gd` (pure logic, testable headless, seeded and
  replayable like `GameState`).
- `data/routes.json` and `data/cohorts.json`.
- A headless runner `tests/test_slice.gd` that plays scripted strategies
  (coal only, algae-seeking, dam-seeking, research-heavy) over a seed
  sweep and prints the indicators: the balance harness.
- An action-and-decision log with an end-of-game survey, exported as
  JSON beside the existing `export_log()`.
- A playtest protocol and a summary template (G1-05).

## 12. What the slice does not decide

It does not choose PopSim's equations, the technology system's data model
in C++, or the presentation of cohorts in the final game. Its results feed
the decision records that P1 will need: the society state (ADR-0012), the
cohort model (ADR-0013), player commands and the promise ledger
(ADR-0014), and a new record for
technology routes, which this slice proposes to number when its playtests
are summarised.
