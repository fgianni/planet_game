# ADR-0008 — Snow, sea ice and the ice–albedo feedback

- **Status:** Accepted
- **Date:** 2026-09-30
- **Accepted:** 2026-09-30
- **Amended:** 2026-10-01 — §5 V7 tests the seasonal cycle's stationarity, not its exact repetition (§9.2); §4.4 thin ice is floes of fixed thickness over a fractional cover, with leads at `T_f` (§10)
- **Milestone:** P0 / M4 (basic snow, ice and albedo feedback)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §9.6, §9.10, §13 M4, §13.1 (experiment C), §14, §15, §24; Planetary Civilization Simulator — Design Record v0.9, §24.2, §24.4
- **Related:** ADR-0001 (modes, budget), ADR-0002 (precision), ADR-0003 (snapshots, migration, M4 history work), ADR-0005 (land and ocean tiles), ADR-0006 (sub-steps, climatology per `k mod 12`), ADR-0007 (surface columns)

## 1. Context

M4 moves basic cryosphere physics ahead of the atmosphere. The specification
asks for freeze and melt rules, a snow and ice surface state, latent heat,
snow and ice albedo, seasonal accumulation and melt hooks, and
water-equivalent accounting. It closes the loop

```text
cooling → snow/ice increases → albedo increases → absorbed shortwave decreases → further cooling
```

and accepts on latent-energy accounting, the correct sign of the albedo
feedback, water-equivalent conservation in controlled tests, and a stable
seasonal snow and ice experiment. Section 9.6 requires the feedback to
*emerge* from water storage, latent heat and albedo, not to be scripted.

Two facts of the current planet shape the choices:

- **There is no atmosphere and no moisture** until M5–M6. The specification
  allows "controlled/test forcing" to supply snow until then. Sea ice needs
  no precipitation: it freezes out of the ocean's mixed layer.
- **Surfaces are two-layer backward-Euler columns** per land and ocean tile
  (ADR-0007), stepped either ten minutes (reference) or one month (climate).
  Without horizontal transport the poles are already far below freezing
  (ADR-0007 §9: land mean 271 K, ocean 295 K, g = 0.4964), so ice will form.

ADR-0005 and ADR-0007 already fix the placement: snow on the land tile, sea
ice on the ocean tile. ADR-0003 separately assigns M4 its history work
(delta chains, fork storage, V6–V7); that work needs no new decision and is
planned as its own task.

## 2. Decision drivers

- **D1 — Emergent feedback.** Albedo follows from snow and ice mass; freezing
  and melting follow from energy. No temperature-threshold albedo switch.
- **D2 — Exact budgets.** Energy, now including latent heat, and water
  equivalent close per tile and globally to rounding, every step.
- **D3 — Both step lengths.** One formulation for ten-minute and month-long
  steps, stable on the long step. Thin sea ice grows fastest, so ice growth
  is stiff.
- **D4 — Analytic checks.** Simplified cases with closed forms (Stefan ice
  growth, clamped melting) before any experiment.
- **D5 — No fake moisture.** Snowfall before M6 is an explicit, labelled
  forcing, zero unless a scenario or test sets it.
- **D6 — Replaceable.** M5 (atmosphere), M6 (moisture), M9 (hydrology) and
  M11 (ocean transport) will feed and drain these reservoirs; the interfaces
  must not need rewriting.

## 3. Options considered

### 3.1 Sea ice thermodynamics

**A. Albedo from temperature.** Ocean albedo rises below the freezing
point. The textbook energy-balance shortcut: no ice mass, no latent heat,
no water accounting. Violates D1 and the acceptance criteria.

**B. Ice mass with the mixed layer at freezing, no insulation.** Freezing
turns the mixed layer's heat deficit into ice; ice raises albedo. The
surface still radiates at the freezing point, so ice-covered ocean never
gets colder than −1.8 °C and the feedback is weak and wrong in winter.

**C. Zero-layer thermodynamic ice (Semtner 1976).** Ice of thickness `h`
with no heat capacity: its surface temperature `T_i` balances absorbed
shortwave, emitted longwave and conduction `k_i (T_f − T_i) / h` from the
base, which stays at the seawater freezing point `T_f`. The base grows or
melts with the difference between conduction and the heat the ocean brings
up; the surface melts when `T_i` would exceed 0 °C. Insulation lets the ice
surface go far below freezing in winter. The standard minimal model of
climate studies; it has Stefan's law as a closed form.

**D. Multi-layer ice with brine and snow cover (Bitz–Lipscomb, CICE).**
Physically fuller and much larger; its gains (brine pockets, internal
melt, snow on ice) matter at the Earth-calibration stage, not for M4's
acceptance.

### 3.2 Ice cover of a tile

**A. Full cover once any ice exists.** Simple, but one kilogram of frazil
switches the whole tile's albedo: a discontinuity the monthly step would
amplify.

**B. Prognostic concentration (leads).** An area fraction with its own
opening and closing rules. Those rules are dynamic (winds, currents), which
the planet does not have until M5 and M11.

**C. Full thermodynamic cover, albedo ramped by thickness.** Thermodynamics
treat ice as covering the tile; the albedo rises smoothly from open water to
ice over a documented thickness scale, standing in for thin ice and leads.
Continuous, cheap, and replaceable by B when dynamics exist.

### 3.3 Snow on land before moisture exists

**A. Snow from a temperature rule** (snow depth grows when cold). A
scripted modifier; no water source. Rejected by D5.

**B. Prescribed precipitation forcing.** A precipitation-rate field in the
forcing partition, zero by default, set by a scenario or test. Falls as
snow on a land tile at or below 0 °C, otherwise as rain that runs off.
Honest about being forcing, conserves water exactly, and is replaced by M6
precipitation behind the same field.

**C. Wait for M6.** Leaves the land half of the specification's loop
untested for two milestones.

### 3.4 Time integration of phase change

**A. Explicit phase change after the ADR-0007 step.** Simple, but the
monthly step's temperature can overshoot the melting point by tens of
kelvin before the correction, and thin-ice growth is unstable when
explicit.

**B. Clamped implicit solve.** Where a phase change is possible, the step
solves with the phase interface held at its melting temperature, and the
energy the clamped equation cannot absorb becomes melt or freeze mass. The
thin-ice growth equation is solved implicitly in `h`. Energy closes by
construction, and the step stays unconditionally stable.

## 4. Decision

Sea ice is **option 3.1 C** (zero-layer thermodynamic ice); ice cover is
**3.2 C** (full thermodynamic cover, thickness-ramped albedo); land snow comes
from **3.3 B** (prescribed precipitation, zero by default); phase change is
integrated with **3.4 B** (clamped implicit solves).

### 4.1 State

| Field | Tile | Type | Partition |
|---|---|---|---|
| `land_snow_water_equivalent_kg_m2` | land | `double` (water reservoir) | slow |
| `sea_ice_mass_kg_m2` | ocean | `double` (water reservoir) | slow |
| `prescribed_precipitation_kg_m2_s` | cell | `float` | forcing (derived, not persisted) |
| `surface_albedo` | per tile, diagnostic | `float` | derived |

Both reservoirs are `double` for the reason ADR-0002 §4.4 gives for slow
ocean reservoirs: a ten-minute snowfall adds about 1e-2 kg/m² to stocks of
hundreds, and water conservation is a gate. The ice surface temperature is
diagnostic (zero heat capacity), so it is not stored. IDs use a new
subsystem prefix `0x0004'xxxx`.

**Invariant.** While `sea_ice_mass > 0`, the ocean mixed layer is at `T_f`.
While `land_snow_water_equivalent > 0`, the land surface layer is at or below
0 °C.

### 4.2 Constants

| Constant | Value | Source or reason |
|---|---|---|
| Latent heat of fusion `L_f` | 3.34e5 J/kg | standard |
| Melting point of snow and ice surface `T_m` | 273.15 K | fresh water |
| Seawater freezing point `T_f` | 271.35 K | −1.8 °C at salinity 35 (fixed until salinity exists, Design §24.4) |
| Ice density `ρ_i` | 917 kg/m³ | standard |
| Ice conductivity `k_i` | 2.03 W/m/K | Maykut and Untersteiner (1971) |
| Snow albedo `α_snow` | 0.75 | between fresh (0.8–0.9) and aged snow (0.5–0.7) |
| Bare sea-ice albedo `α_ice` | 0.55 | bare, snow-free ice (0.5–0.6) |
| Snow masking depth `W_m` | 10 kg/m² | cover `f = W / (W + W_m)` |
| Ice albedo ramp thickness `h_r` | 0.5 m | albedo = open water + (ice − water) · min(1, h / h_r) |

Each value is a documented starting point, not a fit. Snow on sea ice,
snow insulation of the ground and snow heat capacity are left out
(§8).

### 4.3 Land tile

At the start of a step, prescribed precipitation lands as snow if the land
surface layer is at or below `T_m`, otherwise as rain, which leaves as
runoff (to the ocean, as a diagnostic flux until hydrology exists, M9). The
tile albedo blends the material's albedo with `α_snow` by the cover `f`, from
the snow at the start of the step (explicit in albedo, as ADR-0007 is in
forcing). The ADR-0007 column step then runs. If snow is present and the new
surface temperature would exceed `T_m`, the step is solved again with the
surface held at `T_m`; the energy that equation leaves over melts snow at
`L_f`. If that energy melts all the snow, the remainder is returned to the
unclamped solve. Meltwater runs off.

### 4.4 Ocean tile

**Open water** is the ADR-0007 step. If the mixed layer would fall below
`T_f`, the step is solved with the mixed layer held at `T_f`, and the energy
deficit freezes ice at `L_f`.

**Ice-covered water** holds the mixed layer at `T_f` and the deep layer
exchanges with it as in ADR-0007. The heat that exchange brings up melts the
ice base (or, if negative, grows it). The surface balance

```text
(1 − α(h)) Q + k_i (T_f − T_i) / h' = β ε σ T_i⁴         with T_i ≤ T_m
```

is solved together with the backward-Euler growth equation

```text
ρ_i L_f (h' − h) / Δt = k_i (T_f − T_i) / h' − F_ocean − M_surface
```

for the new thickness `h'` and the ice surface temperature `T_i`, by a
fixed number of Newton iterations. `M_surface` is the melt when `T_i` is held
at `T_m`. If the ice melts completely within the step, the unused energy
warms the open mixed layer through the open-water solve. The implicit
growth equation is stable for any Δt: a month of −20 K surface from open
water grows about 0.8 m by Stefan's law, which the explicit form cannot
reach without oscillating.

### 4.5 Budgets

Energy closure (ADR-0007 §4.4) gains a latent term:

```text
Σ A f [C ΔT] − Σ A f L_f (Δ ice + Δ snow) = Δt Σ A f [(1 − α) Q − β ε σ T_surface⁴]
```

(`T_surface` is `T_i` over ice.) Water equivalent closes separately:

```text
Δ(Σ A_land snow + Σ A_ocean ice) = Δt Σ A_land P_snow − runoff − (ocean freshwater exchange)
```

The ocean's water is a reference inventory: sea level stays fixed
(ADR-0005), and the net freshwater flux the ocean gives to or takes from ice
is a diagnostic (Design §24.4: future salinity input). Snowfall carries no
energy in: the latent heat it later absorbs is recorded as an external
input, and M6 releases the matching heat in the atmosphere.

### 4.6 Scheduling, climatology and snapshots

The cryosphere runs inside the ADR-0007 surface process: one step solves
temperatures and phase change together, so the order of the two cannot
matter. The climatology partition gains per-sub-step means of surface
temperature, snow and sea ice keyed by `k mod 12` (ADR-0006 §7).

PSNAP schema 4 adds the two reservoirs. The v3 → v4 migration initialises
them to zero (no snow, no ice) and runs no physics; spin-up then grows the
seasonal state. The v1–v3 golden files keep loading through the chain, and a
v4 golden file is added.

### 4.7 Calibration

The Earth-like `g` (ADR-0007 §9) is refitted to 288 K with the cryosphere
active, recorded as a new calibration entry (specification §24). Snowfall
stays zero in the fit, so the refit reflects sea ice only.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Energy closure with latent heat, per tile and globally, reference and climate steps | ADR-0007 V2 gate (1e-9 of flux scale plus the rounding floor) |
| V2 | Water-equivalent closure in a controlled prescribed-snowfall test, per tile and globally | ≤ 1e-12 relative, plus the rounding floor |
| V3 | Stefan growth: ice surface held at a fixed temperature, no ocean flux | backward-Euler discrete solution exact; converges to `h² = h₀² + 2 k_i ΔT t / (ρ_i L_f)` at first order in Δt, ≤ 1% at one-day steps over a winter |
| V4 | Invariants: snow present ⇒ surface ≤ `T_m`; ice present ⇒ mixed layer = `T_f`; masses ≥ 0 | exact |
| V5 | Albedo-feedback sign: added snow or ice under the same forcing lowers absorbed shortwave and the next temperature; removed snow or ice raises them | exact sign |
| V6 | Degenerate cases: no snowfall and never freezing ⇒ ADR-0007 results unchanged, bit for bit; dead rock unchanged | bit-identical |
| V7 | Seasonal experiment: Earth-like and aqua planet after spin-up; the seasonal cycle of ice and snow cover is stationary *(amended 2026-10-01, §9.2: the accepted text asked for a year-to-year repeat within 1e-3)* | decadal means of each year's largest and smallest cover agree between consecutive decades within 2 %; interannual spread and volume drift recorded |
| V8 | Determinism: 1/2/8/16 workers, chunked runs, replay hashes (ADR-0003 V1–V2) | bit-identical |
| V9 | Snapshots: v4 round trip; v1–v3 golden files load through the chain | exact |
| V10 | Cost against the ADR-0001 gate | within the M3-03 gate |

## 6. Consequences

**Positive.** The ice–albedo loop emerges from mass and energy. Energy and
water close to rounding with phase change included. The monthly step stays
unconditionally stable. Stefan's law gives an analytic check. The
precipitation field is the seam M6 plugs into, the runoff diagnostic the
seam M9 plugs into, and the freshwater flux the seam for salinity.

**Negative.** Without transport, ice covers too much ocean and poles are
too cold; hemispheric ice areas will not match Earth until M5 and M11.
Default land has no snow until M6 supplies precipitation, so the land half
of the loop is exercised only by controlled experiments before then.
Thickness-ramped albedo stands in for leads and will be replaced by a
concentration when ice dynamics exist. A clamped solve adds a second
Newton solve on phase-change steps.

**Risks.** Local multiple equilibria (an ice-covered and an ice-free
state under the same forcing) are physical in this model and may appear
in cells near the ice edge; the V7 experiment records them rather than
suppresses them. The refitted `g` shifts again at M5 and M8.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M4 | §4.1–4.7; V1–V10 |
| M5 | The atmosphere replaces the grey layer above snow and ice |
| M6 | Model precipitation replaces the prescribed field; snowfall's latent heat released in the atmosphere |
| M9 | Runoff and meltwater enter hydrology; snow insulates the ground |
| M11 | Ice concentration and drift with ocean transport; freshwater flux into salinity |

## 8. Open questions

- Snow on sea ice (albedo, insulation) and snow insulation of land: left
  out of M4 for size. Current position: add with M9, when snow depth
  matters for soil and permafrost.
- Should the ice albedo ramp use thickness or a prognostic concentration
  from the start? Current position: thickness, until ice dynamics exist.
- Land ice sheets: perennial snow accumulates without limit under
  prescribed snowfall. Current position: no cap in M4; ice-sheet flow is a
  later milestone.

## 9. Implementation record (task M4-01, 2026-09-30)

§4.1 (all fields), §4.2, §4.3, §4.5 (land terms) and §4.6 (schema 4) are
implemented; §4.4 (sea ice) is task M4-03, after the heat transport of
ADR-0009 (task M4-02), and the sea-ice field stays zero until then.

| Concern | Code |
|---|---|
| Constants (§4.2) | `sim/planet/surface/cryosphere_constants.hpp` |
| Eliminated column system, surface solve with a constant sink | `sim/planet/surface/column_step.{hpp,cpp}` |
| Land tile with snow (§4.3) | `sim/planet/surface/land_snow.{hpp,cpp}` |
| Mesh step, budgets, `initialise_cryosphere`, migration hook | `sim/planet/surface/surface_energy.{hpp,cpp}` |
| Fields, PSNAP schema 4 | `sim/core/fields/field_registry.hpp`, `sim/planet/planet_state.{hpp,cpp}`, `sim/core/serialization/snapshot_file.{hpp,cpp}` |
| Tests | `tests/unit/test_land_snow.cpp`, `tests/physics/test_land_snow_planet.cpp`, `tests/unit/test_snapshot_file.cpp`, `tests/regression/test_golden_snapshot.cpp` |
| `planet_cli thermal --precipitation` | `apps/planet_cli/main.cpp` |

Refinements, recorded here rather than changing the decision:

- **Phase of precipitation** is decided by the surface layer at the start of
  the step (at or below `T_m`: snow), and the albedo by the snow before that
  step's snowfall, both explicit as the forcing is.
- **Without snow the land tile is the ADR-0007 column bit for bit**: the
  albedo blend is skipped at zero snow, and `step_column` is rebuilt on the
  same eliminated system (V6).
- **The schema 3 → 4 initialiser is required**, like the 1 → 2 one: the core
  reader refuses an older file without it rather than zero-filling
  (ADR-0003 §3.6); `surface_energy_migration` supplies both.
- **Closure gates live on the diagnostics** (`closure_gate_J`,
  `water_gate_kg`), so every test applies the same V1 and V2 gates.

Validation (L4, 3 × 10⁻⁵ kg/m²/s prescribed everywhere unless stated):

| ID | Result |
|---|---|
| V1 | worst energy closure 0.027 of the gate, climate and reference steps, per tile and globally |
| V2 | worst water closure 1.3e-5 of the gate |
| V4 | snow ≥ 0; snow present ⇒ land surface ≤ `T_m`, per tile over a grid of cases and on the planet |
| V5 (snow) | 50 kg/m² of snow lowers absorbed shortwave (1.47e17 → 1.31e17 W) and every cell's land temperature |
| V6 | no snow ⇒ `step_column` bit for bit; zero precipitation ⇒ no snow and no latent heat; `planet_cli thermal` output for the three presets unchanged |
| V8 (snow) | 1/2/8/16 workers bit-identical |
| V9 | v4 round trip exact; v1–v3 golden files load through the chain with zero reservoirs; all four step ten years with closed budgets |

**Finding: snow on land never melts without heat transport.** With
prescribed snowfall on the Earth-like planet (L4, seed 1, after a 30-year
spin-up), 72 % of land cells gain snow in the first year and none loses it
again; the snow grows every year and the mean land temperature falls from
271 K to 229 K. At the melting point, snow-covered land at 50° N in summer
absorbs about 140 W/m² (α ≈ 0.7) but radiates about 225 W/m², so it cannot
reach 0 °C from sunlight alone. On Earth the difference is supplied by warm
air carried from lower latitudes and the oceans, which this planet does not
have until M5 (atmosphere) or a reduced transport. This is the physics of
§4.3 on a transport-free planet, not a defect of the step, and it puts the
seasonal-experiment acceptance (§5 V7) for land snow out of reach until
heat reaches the land. Sea ice (M4-02) has the ocean's heat beneath it and
is expected to behave differently. The decision on transport is ADR-0009
(accepted 2026-09-30), implemented as task M4-02 before sea ice.

### 9.1 Sea ice (task M4-03, 2026-09-30)

§4.4 is implemented in `sim/planet/surface/sea_ice.{hpp,cpp}`; the surface
step and the ADR-0009 cell solve use it for every ocean tile, and the ice
surface is the tile's radiating temperature (emission, the cell's air, zonal
and P2 means). Tests: `tests/unit/test_sea_ice.cpp`,
`tests/physics/test_sea_ice_planet.cpp`.

Refinements, recorded here rather than changing the decision:

- **Growth solve.** For a trial thickness the ice surface is the ADR-0007
  quartic with the conduction `k_i/h'` added to `a`; the growth residual
  `ρ_i L_f (h' − h)/Δt − F_top(h') + F_o` is strictly increasing in `h'`
  (conduction falls with thickness; with the surface held at `T_m` the
  conduction and the top melt cancel), so a bracketed Illinois false position
  finds the root to 4ε. When the residual is already positive as `h' → 0`,
  all the ice melts and the step is the open-water one with the latent sink.
- **Transport slope.** The ice tile reports `dT_i/ds` with the thickness
  responding (implicit function theorem on the surface balance and the
  growth equation); holding the thickness fixed underestimated it by up to
  a few times for thin ice and slowed the transport Newton to linear
  convergence.
- **Transport and shared air** (ADR-0009) act on the ice surface; the ocean
  heat flux to the base is the deep layer's exchange with the mixed layer at
  `T_f` (plus the mixed layer's own excess if a state violates the
  invariant).
- **Gross terms.** Frozen and melted mass are reported gross (the base may
  grow while the top melts); their difference is the exact change of mass.
- **Seawater never below freezing.** ADR-0007's initial state (§4.5) holds
  polar ocean layers at their ice-free radiative equilibrium, 220–250 K; the
  sea-ice physics would pay for raising them with hundreds of metres of ice.
  `initialise_cryosphere`, which is also the schema 3 → 4 migration's
  initialiser, raises both ocean layers to at least `T_f`. A migrated v1–v3
  state is changed accordingly (a declared repair; the golden tests expect
  it).

Validation (L4, seed 20260930, Earth-like preset with `D`, `g` of ADR-0009 §10):

| ID | Result |
|---|---|
| V1 | energy closure 0.11 of the gate per tile over a grid of cases, 0.035 globally with ice forming (climate and reference steps) |
| V2 | water closure 8e-5 of the gate per tile, 3e-5 globally, snow and ice together |
| V3 | Stefan growth: the discrete backward-Euler thickness to 5e-16; after a winter from 0.1 m under an 18 K deficit, errors 7.7 %, 3.1 %, 0.39 % at 30-, 10- and 1-day steps |
| V4 | ice ≥ 0; ice present ⇒ mixed layer = `T_f` and ice surface ≤ `T_m`; open water ≥ `T_f`; deep ocean ≥ `T_f` |
| V5 | 1 m of extra ice on every ocean tile: absorbed shortwave 1.50e17 → 0.92e17 W, global mean a year later 283.4 → 259.9 K |
| V6 | open water above `T_f` is the ADR-0007 column bit for bit |
| V8 | 1/2/8/16 workers bit-identical with ice |

On the calibrated Earth-like planet ice forms only poleward of about 45°,
with a seasonal cycle: 20–26 million km² in the north and 31–35 million km²
in the south (third year from the initial state, L4), against Earth's
roughly 6–15 and 3–18. `D` and `g` were fitted
before ice existed; the refit with ice is task M4-04. On the aqua planet
(no greenhouse, no transport) perennial ice keeps thickening, as Stefan's
law requires when nothing brings heat to its base, while the columns'
sensible storage settles (−2e-4 of the absorbed after 60 years).

Performance: sea ice made the transport step kinked and slow (the ADR-0001
scenarios took about 290 s at L5 and 1,400 s at L6); with the exact ice
slopes, the warm-started thickness Newton and ADR-0009 §12 (transport one
level coarser), 250 years take 176 s at L5 and 572 s at L6 on 4 workers,
within the gates (240 s, 600 s).

### 9.2 Climatology, refit and the seasonal experiment (task M4-04, 2026-10-01)

**Climatology (§4.6).** `sim/planet/climatology/monthly_climatology.{hpp,cpp}`:
per cell and sub-step `k mod 12`, the Welford running mean and population
variance of the cell's radiating surface temperature (a derived field the
surface step now writes) and the means of land snow and sea ice, over the
climate steps of a run; four `climatology`-partition fields, never
persisted; spin-up does not contribute. The scheduler's climate process
accumulates it. Tested against the two-pass statistics
(`tests/unit/test_climatology.cpp`).

**Refit (§4.7).** With sea ice active, `D` and `g` are refitted to 288 K and
a 42 K P2 equator-to-pole difference (ADR-0009 §10): `g = 0.4965`,
`D = 0.6400` (record at their definition). The joint bisection alternates
between two neighbours 0.5 K apart in the gradient, because the ice edge
moves a cell at a time. At L5: 288.2 K, 40.9 K, peak poleward transport
3.72 PW (Earth about 5.5; 1.8 PW before ice), northern sea ice
4.8–10.2 million km² over the year (Earth about 6–15). The southern cover,
33–35 million km², is the whole cap south of about 60° S: on this seed the
south pole is open ocean, where Earth has Antarctica.

**V7 amended (accepted 2026-10-01).** The planet's seasonal cycle is not
periodic. After spin-up the northern winter maximum repeats exactly, but
the summer minimum varies irregularly from year to year by about 6 %
(standard deviation 0.41 of a mean 6.6 million km² at L4): thin edge ice
either melts out completely or survives a summer, which amplifies small
differences, much as Earth's September minimum varies. A year-to-year
repeat within 1e-3 is therefore unreachable. V7 instead requires a
stationary cycle: the decadal means of each year's largest and smallest
cover (area weighted by the albedo ramps of §4.2, which the radiation sees;
counting any cell with ice quantises the edge by whole cells, about 2 %)
agree between two consecutive decades within 2 %, with the interannual
spread recorded (`tests/physics/test_seasonal_experiment.cpp`):

| Planet (L4, 40 spin-up years) | Decadal change | Northern ice cover, km² | Drift per decade |
|---|---|---|---|
| Earth-like | 0.5 % | 6.6 (sd 0.41) – 10.3 million | ice mass +3.8 % |
| Earth-like, 1e-5 kg/m²/s snowfall | 0.9 % | 14.1 million, snow cover 36.6–37.0 million | ice +7 %, snow +17 % |
| Aqua planet | 0 | 93 million, perennial | ice +9 % |

**Findings.** Perennial sea ice keeps thickening (about 0.4 % a year):
without ocean heat transport (M11) nothing brings heat to its base, and
summer melt does not reach the thick interior. Under uniform prescribed
snowfall, perennial land snow accumulates as nascent ice sheets with no flow
to limit them, and the snowy planet settles about 6 K colder. Both drifts
are recorded rather than gated; they belong to M11 (ocean transport) and to
a later ice-sheet process.

Performance with the refit constants, 4 workers: 250 years at L5 in 148 s
(gate 240 s) and at L6 in 508 s (gate 600 s); the L5 run replays bit for
bit.

## 10. Amendment: floes, leads and a continuous ice tile (accepted 2026-10-01)

**Finding (task M5-03).** Under ADR-0010's atmosphere, every cell's tiles
sit under a column with heat capacity, and the column's implicit solve needs
the tiles to respond continuously to their source. §4.4's tile does not:

- **A jump at complete melt.** Ice melting from the top radiates from a
  surface held at `T_m` = 273.15 K. When the last ice goes, the tile becomes
  open water at `T_f` = 271.35 K. At the same inputs the tile's longwave
  jumps by about 8 W/m² and its sensible heat by about 20 W/m².
- **A fold for thin ice.** As the ice thins, its surface cools towards
  `T_f`, so it radiates less and melts faster. On a monthly step the growth
  residual first falls with the new thickness and then rises, so a step can
  have two roots or none. §4.4 also assumed the residual increases, and so
  sometimes melted 0.2 m of ice that a root would have kept.

A column above such a cell has no exact solution. Its response jitters, and
the transport, coupling neighbours at thousands of W/m² per K, turned the
jitter into local inconsistencies of 100–1,700 W/m² and steps that did not
converge. The grey path of ADR-0009 escaped only because its massless cell
air never coupled single-tile cells.

**Change (§4.4).** Ice thinner than `h_r` = 0.5 m (§4.2's albedo ramp) is
floes of thickness `h_r` covering the fraction `c = m / (ρ_i h_r)` of the
tile; thicker ice covers it fully. The rest is **leads**: open water over
the mixed layer, which stays at `T_f` while any ice remains. Within a step:

- The cover is that at the start, `c₀`, which already sets the albedo.
  Floes absorb with the ice albedo and leads with the ocean's, so the
  tile's absorbed sunlight is unchanged.
- The floes' surface balance is §4.4's, at the floe thickness
  `h_f' = max(h_r, m'/ρ_i)`.
- The leads lose or gain `Q_L = (1 − α_ocean) Q + s − γ T_f − ε σ T_f⁴` per
  unit area (`s` is the external source of ADR-0009 and ADR-0010). That heat
  freezes or melts ice, as heat lost from leads does in nature.
- The growth equation, in mass per unit tile area, is

  ```text
  L_f (m' − m) / Δt = c₀ F_top(h_f') − (1 − c₀) Q_L − F_ocean
  ```

  where `F_top` is the floes' conduction minus their top melt. Its residual
  increases with `m'`. Below `ρ_i h_r` the floe thickness is fixed, so the
  dependence is linear. Above it, the thick-ice terms change by less than
  the latent term (`k_i ΔT / h_r²` with `ΔT` at most 1.8 K above `T_f`).
  So there is exactly one root and no fold.
- **Complete melt** occurs where that root is not positive. The floes and
  leads keep the step's surface balance, and the heat left over after
  melting all the ice warms the mixed layer. The tile's response is then
  continuous at the threshold, and flat beyond it within the step, like a
  phase change.
- The tile radiates `c₀ ε σ T_i⁴ + (1 − c₀) ε σ T_f⁴`. Its surface
  temperature, which sets the sensible exchange and the diagnostics, is
  `c₀ T_i + (1 − c₀) T_f`.

Ice at least `h_r` thick (`c₀ = 1`) behaves as before, apart from the
amended root. Open water without ice is unchanged.

**Consequences.**
- M4's calibrated constants were fitted with the old tile. ADR-0010 §4.6
  refits the greenhouse and transport in M5-04 anyway.
- The ADR-0008 tests that pin sea-ice behaviour are rechecked against the
  amended tile, and the changes are recorded in the M5-03 task record.
- The water and energy budgets are unchanged in form: floes, leads and the
  ocean exchange are each counted once.
