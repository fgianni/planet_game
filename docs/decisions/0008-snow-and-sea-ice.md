# ADR-0008 — Snow, sea ice and the ice–albedo feedback

- **Status:** Accepted (implementation records §9 M4-01, §10 M4-03)
- **Date:** 2026-09-30
- **Accepted:** 2026-09-30
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
| V7 | Seasonal experiment: Earth-like and aqua planet after spin-up; annual cycle of ice area and volume repeats | year-to-year change ≤ 1e-3 relative; hemispheric areas recorded |
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

## 10. Implementation record (task M4-03, 2026-09-30)

§4.4 (the ocean tile with zero-layer sea ice) and §4.5's ice terms are
implemented. The sea-ice field of M4-01 (`sea_ice_mass_kg_m2`, schema 4) is
now live, and no new field or schema was needed.

| Concern | Code |
|---|---|
| Ocean tile: open water, freezing, ice growth and melt, complete melt, slope | `sim/planet/surface/sea_ice.{hpp,cpp}` |
| Cell solve, mesh step, latent and water budgets, hemispheric diagnostics, annual ice summary | `sim/planet/surface/surface_energy.{hpp,cpp}` |
| Tests | `tests/unit/test_sea_ice.cpp`, `tests/physics/test_sea_ice_planet.cpp` |
| `planet_cli thermal` ice line (end mass, smallest and largest area per hemisphere) | `apps/planet_cli/main.cpp` |

Refinements, recorded here rather than changing the decision:

- **Thickness solve.** A bracketed Newton on the growth residual, with the
  exact derivative from the implicit-function theorem on the surface
  balance, replaces the task's Newton–bisection. It starts warm from the
  tile's previous root in the same step, otherwise from the discrete Stefan
  estimate, and stops when a step is below 1e-13 of the thickness. The
  residual is reported.
- **Slope for the transport solve.** It includes the thickness response and
  is taken against the cell's air (ADR-0009 §11), so it is never zero. This
  replaces the task's decision 5 (slope 0 at a phase change), which let a
  freezing coastal cell drain without limit.
- **Initial state.** `initialise_cryosphere`, which also serves the
  schema 3 → 4 migration, raises ocean layers below `T_f` to `T_f`. The
  ADR-0007 state without ice holds polar oceans at 220–250 K, and the ice
  physics would otherwise turn that deficit into hundreds of metres of ice
  at once.

Validation (unit cases, and L4 planets with seed 20260930; Earth-like with
transport on):

| ID | Result |
|---|---|
| V1 | per tile: worst 0.16 of the ADR-0007 gate over a grid of cases; planet: 0.038 (Earth-like, three climate years and a reference day), 2.2e-5 (aqua planet, three climate years) |
| V2 | per tile exact; planet: worst 4.1e-5 of the gate (Earth-like), 2.5e-5 (aqua planet) |
| V3 | discrete Stefan scheme matched to 1.9e-13; against `h² = h₀² + 2 k_i ΔT t / (ρ_i L_f)` after a winter: 7.7 % at 30-day steps, 3.1 % at 10-day steps, 0.39 % at 1-day steps (gate ≤ 1 %), first order |
| V4 | ice ≥ 0; ice present ⇒ mixed layer = `T_f`; deep layer ≥ `T_f` (to 1e-9 K), per tile over a grid and on both planets |
| V5 (ice) | 1 m of extra ice on every ocean tile lowers the absorbed shortwave from 1.53e17 W to 9.24e16 W and the global mean a year later from 284.4 K to 259.5 K |
| V6 | open water above `T_f` is the ADR-0007 column bit for bit; dead rock unchanged (259.336 K, imbalance 3.8e-9) |
| V8 | 1/2/8/16 workers bit-identical; runs replay bit for bit (ADR-0003 V1–V2, `test_run_replay`) |
| V10 | 250 years on four workers, target machine: L5 117 s (limit 240 s), L6 395 s (limit 600 s). CI gates the equivalent rate (ADR-0001 §11) |
| Planets | ice forms in both hemispheres and only poleward of 37.6° on both the Earth-like and the aqua planet |

**Finding: too much ice, and hardly any seasonal cycle.** After a 60-year
spin-up at L5 (seed 1, `planet_cli thermal`):

| Planet | Northern ice area, year's range (million km²) | Southern ice area, year's range (million km²) |
|---|---|---|
| Earth-like | 21.2–21.6 | 47.5–47.7 |
| Aqua planet | 92.7, constant | 100.1, constant |

Earth's sea ice ranges about 4–16 million km² in the Arctic and 3–19 in the
Antarctic. Four causes, in expected order of weight:

1. **The calibration predates ice and ADR-0009 §11.** `D` and `g` were fitted
   without either (§11 there). The Earth-like planet now averages 282.7 K
   with a 64.7 K P2 equator-to-pole difference, against targets of 288 K
   and 42 K.
2. **No heat reaches the ice from below.** There is no ocean heat transport
   under the ice until M11, so ice that forms in winter meets no warm water
   in summer.
3. **Grey radiation lets polar summers lose heat quickly.** This is the same
   limitation ADR-0009 §10 records.
4. **The aqua planet has no transport and no greenhouse** (`g = 0`). It is
   a radiative-equilibrium planet: winter ice poleward of about 38° is
   perennial there, and its global mean falls from 274.8 K (ADR-0007) to
   254.9 K.

Items 1 and 3 belong to task M4-04 (the seasonal experiment V7 and the refit
of `D` and `g` with ice), item 2 to M11. None of them is a defect in the
§4.4 step, whose budgets, invariants and signs hold.
