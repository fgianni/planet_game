# ADR-0010 — A layered atmosphere: hydrostatic columns, grey longwave and convection

- **Status:** Proposed
- **Date:** 2026-10-01
- **Milestone:** P0 / M5 (atmosphere and pressure)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §5 (3–5 layers), §6 (`AtmosphereState`), §9.1, §10 (vertical structure), §13 M5, §23, §24; Planetary Civilization Simulator — Design Record v0.9, §5.2–5.4, §15.1
- **Related:** ADR-0001 (modes, budget), ADR-0002 (field layout, precision), ADR-0003 (snapshots, §3.6 migration), ADR-0006 (sub-steps), ADR-0007 (surface columns, grey layer), ADR-0008 (snow and sea ice), ADR-0009 (transport, shared cell air)

## 1. Context

Since M3 the planet's greenhouse has been ADR-0007's single isothermal grey
layer, which has no heat capacity and no vertical structure. It reduces the
outgoing longwave to `(1 − g/2) εσT⁴`, and `g` is a calibration constant
(0.4965 since M4-04). The tiles also exchange heat with their cell's "air"
(ADR-0009 §10–11). That air is a diagnostic temperature with no mass, and
the implicit diffusion acts on it.

Specification §13 M5 asks for "a 3–5-layer-capable atmosphere, hydrostatic
initialisation and surface–atmosphere exchange. Do not permanently lock the
model to one layer." The design record (§15.1) develops with three layers
and targets five for the shipped L6 reference. The milestone tables already
assign M5 to take over the grey layer (ADR-0007 §7) and the atmospheric
share of the transport (ADR-0009 §7).

Winds arrive at M6 and moisture at M7. M5 therefore has no motion and no
water vapour. Its atmosphere is columns of air with mass, temperature and
pressure. The columns exchange radiation and sensible heat with the surface
and with each other's layers, they mix convectively, and heat diffuses
between them as in ADR-0009 until M6's winds carry it.

## 2. Decision drivers

- **D1 — Physical form** (specification §9.1, §24). Radiation, convection
  and exchange are written as fluxes with documented constants. Any
  calibration constant must have a physical meaning: the grey layer's `g`
  becomes the longwave optical depth `τ₀`.
- **D2 — Exact budgets.** The atmosphere stores energy. Every step must
  close surface plus atmospheric storage, latent heat, absorbed sunlight,
  outgoing longwave and transport to rounding, under the ADR-0007 V2 gate.
  Atmospheric mass is conserved exactly.
- **D3 — Not locked to N layers.** The layer count is data (3–5, plus 0
  for "no atmosphere" and 1 for tests). Code, snapshots and tests must work
  for any of them.
- **D4 — Stable on both step lengths.** A layer's radiative time scale is
  about a month and the monthly sub-step is a month. Integration must be
  implicit, like ADR-0007 and ADR-0009.
- **D5 — Inside the budget.** The ADR-0001 gates are 250 years at L5 in
  ≤ 240 s and at L6 in ≤ 600 s. The L6 run currently takes 572 s, so most
  of the atmosphere's cost has to be bought back.
- **D6 — Replaceable in place.** M6 adds wind and mass flux, M7 water
  vapour, M8 clouds and M12 CO₂. Each must change a term, not the
  structure.

## 3. Options considered

### 3.1 Vertical coordinate

**A. Fixed heights (z levels).** Over mountains the lowest levels sit
inside the terrain. Rejected.

**B. σ = p / p_s with N equal-mass layers — chosen.** It follows the
terrain, layer masses are `p_s / (g N)`, and the budget is a sum of equal
weights. This is the classic choice of simple GCMs (Held and Suarez, 1994;
Frierson et al., 2006). The model top is `p = 0`.

**C. Unequal σ layers.** Thinner layers near the surface resolve the
boundary layer better. That matters only once there is moisture and
turbulence (M7), so it is deferred. Interfaces are data, so it can be
adopted later without a structural change.

### 3.2 Longwave

**A. Keep the grey layer and add heat capacity.** One layer, which D3
forbids.

**B. Grey two-stream through N layers — chosen.** Each layer has optical
depth `Δτ_k` and emissivity `ε_k = 1 − exp(−Δτ_k)`. It emits `ε_k σT_k⁴`
up and down, and transmits `1 − ε_k` of what crosses it. The optical depth
follows pressure:

```text
τ(p) = τ₀ [ f_l (p / p₀) + (1 − f_l) (p / p₀)⁴ ],   p₀ = 101 325 Pa, f_l = 0.1
```

The linear term stands for well-mixed absorbers, which M12's CO₂ will
scale. The quartic term stands for water vapour, concentrated near the
surface (Frierson et al., 2006). `τ₀` is the calibration constant that
replaces `g`. Because τ depends on pressure rather than σ, a column over
high terrain has less optical depth above it and is colder at its surface.
That is the physical origin of the lapse-rate effect specification §13 M3
said not to fake before an atmosphere existed.

**C. Latitude-dependent τ₀ (Frierson's vapour proxy).** A prescribed
pattern is a scripted climate modifier. Rejected; the vapour proxy belongs
to M7, which will compute τ from humidity.

**D. Band or correlated-k radiation.** Beyond a reduced model and its
budget. Rejected for P0.

**Shortwave:** the atmosphere stays transparent, as the grey layer was.
Absorption by vapour and ozone arrives with vapour (M7) and clouds (M8),
so M5 changes only the longwave and the change can be attributed.

### 3.3 Convection

**A. None (pure radiative equilibrium).** The lowest layer becomes
superadiabatic by tens of kelvin (Manabe and Strickler, 1964). Rejected.

**B. Convective adjustment to a critical lapse rate — chosen.** Adjacent
layers whose lapse rate exceeds `Γ_c = 6.5 K/km` are mixed to neutral, and
the mixing conserves the column's enthalpy exactly. Neutral means uniform
`θ_c = T (p₀/p)^κ_c` with `κ_c = R_d Γ_c / g`, the potential temperature
of the Γ_c profile. This is the radiative–convective model of Manabe and
Strickler. Γ_c stands for the moist adiabat until M7–M8 supply latent
heating; it is a documented physical constant, not a fit.

**C. A relaxation (Betts–Miller) scheme.** It needs a time scale, which a
monthly step cannot resolve. Deferred to M8, where moist convection
needs it.

### 3.4 Horizontal transport until the winds

**A. Diffuse every layer.** That is N linear solves per step, at N times
the cost of ADR-0009 for the same planetary-scale effect. Rejected by D5.

**B. Diffuse the column's mean temperature `T̄`.** A column over a plateau
then receives no transport for being colder by its lapse, so heat flows
into mountains. Rejected.

**C. Diffuse the column's mean θ_c — chosen.** The diffused quantity is
the mass-weighted mean `θ̄_c` of the layers. Convective adjustment, which
conserves enthalpy, barely changes it, and over terrain it measures the temperature reduced
to the reference pressure along Γ_c, as an observer reduces to sea level.
The flux is still `K ∇θ̄_c` between cells, antisymmetric, so energy is
exact whatever variable sets it (ADR-0009 D2). A column receiving `h`
warms every layer by `ΔT_k = Δθ (p_k/p₀)^κ_c`, so its θ_c profile shifts
uniformly and convection is not disturbed.

## 4. Decision

### 4.1 State

| Field | Partition | Layout, type | Unit |
|---|---|---|---|
| `atmosphere_surface_pressure_Pa` | slow | cell, `float64` | Pa |
| `atmosphere_temperature_K` | slow | cell × N, `float64` | K |

`float64` follows ADR-0002 §4.4 for reservoirs and ADR-0007 §10. A layer's
step increment in reference mode is far below `float` resolution at 250 K,
and the budget gate would fail.

`p_s` is constant in M5, because no mass moves without wind. It is
nevertheless state, because M6 makes it prognostic.

**Variable layer count.** The registry marks the atmospheric layer fields
as *scenario-layered*. The run manifest records `atmosphere_layers = N` as
a scenario entry. Each snapshot's manifest already states `layers` per
field, and the reader requires all scenario-layered fields to agree.

N = 0 means no atmosphere: the field has zero layers and the
surface-pressure field holds zero. The atmosphere's diagnostics are
derived and not stored: layer pressures and geopotential heights,
sea-level pressure, surface air temperature and outgoing longwave.

### 4.2 Presets

| Preset | N | Longwave | Transport |
|---|---|---|---|
| `dead_rock` (experiment A) | 0 | surface to space | none |
| `aqua_planet` (experiment B) | 0 | surface to space | none |
| `earth_like` | 3 | τ₀ (fit, §4.6) | D (refit, §4.6) |

The grey layer is retired for any planet with an atmosphere: `g` must be 0
when N > 0. It remains in the code because the ADR-0007 and ADR-0008 tests
use it on atmosphere-free columns.

Experiments A and B are unchanged; they have no atmosphere by definition
(specification §13.1). N = 5 is supported and tested. It becomes the L6
default only when D5 allows it (design record §15.1).

### 4.3 Hydrostatic initialisation

Constants:
- `g = G M / R²`, from the planet parameters;
- `R_d = 287.04 J/kg/K` and `c_p = 1004.64 J/kg/K`.

Initialisation:
1. A cell's surface height `z_s` is the area-weighted height of its tiles:
   ocean at sea level, land at the mean of its hypsometry above sea level
   (ADR-0005).
2. The surface pressure is the reference sea-level pressure reduced along
   the Γ_c profile from the cell's tile-mean surface temperature `T_s`:

   ```text
   p_s = p₀ (1 − Γ_c z_s / (T_s + Γ_c z_s))^(g / (R_d Γ_c))
   ```

   It is then scaled by a single global factor so that the planet's mean
   sea-level-equivalent pressure is exactly `p₀`. Total mass `Σ A p_s / g`
   is recorded and held constant from then on.
3. Layer temperatures start on the Γ_c profile from `T_s`, with a floor at
   the skin temperature `T_s (1/2)^¼` of the grey limit. Radiation and
   convection set the rest within the spin-up.

This is also the snapshot migration step 4 → 5 (§4.7).

### 4.4 Step (one per sub-step, climate and reference mode)

Every cell is a coupled implicit system. The unknowns are its N layer
temperatures and its tiles' surface temperatures. The step is backward
Euler, solved by Newton over the layer temperatures:

1. **Tiles.** Each tile's existing safeguarded solve (ADR-0007 §4.3,
   ADR-0008 §4.3–4.4) runs with:
   - the downward longwave it absorbs, `ε_t L↓`, as a source;
   - sensible exchange `γ (T_air − T_t)` with the air at the surface,
     where `T_air = T_1 (p_s/p_1)^κ_c` is the lowest layer extrapolated
     along Γ_c and `γ = 10 W/m²/K` (ADR-0009 §10, unchanged);
   - its upward emission `ε_t σT_t⁴` plus the reflection `(1 − ε_t) L↓`,
     which goes up into the layers.

   The tiles return their temperatures and their slopes, so the snow
   clamp and the sea-ice solve are exact as before.
2. **Layers.** Each layer balances its storage `(c_p p_s / (g N)) ΔT_k / Δt`
   against the longwave it absorbs minus what it emits, plus the sensible
   heat (layer 1) and the transport source (§4.5).
3. **Newton.** The N × N Jacobian is assembled exactly, with the tile slopes
   entering through the surface terms. It is solved directly (N ≤ 5),
   starting from the previous state, with backtracking and the residual
   reported.
4. **Convective adjustment.** It runs after the implicit step, conserves
   enthalpy exactly and is idempotent. It is split from the implicit step,
   so the budget sees it as an internal redistribution.

The outgoing longwave is what leaves the top layer. On a planet without an
atmosphere every tile reduces to the ADR-0007 column, so the old tests
still apply unchanged.

### 4.5 Transport

ADR-0009 is kept: implicit diffusion on the coarse transport graph (§12),
the same Newton and multigrid, and the same diagnostics. Two things change:
- The diffused temperature is the column mean `θ̄_c` (§3.4 C) instead of
  the massless cell air of ADR-0009 §11.
- The response of a cell to a source `h` is its column solve, with `h`
  distributed by §3.4 C.

The column's heat capacity (about 10⁷ J/m²/K) separates the transport from
the tiles' phase-change kinks. Those kinks are what cost the 20–25
evaluations per step in ADR-0009 §12, so fewer outer iterations are
expected; D5's gates decide.

If the gates fail, the fallback is a linearly implicit coupling: one
linear solve with the columns' slopes and the consistency residual
reported. That would be recorded as an amendment, not adopted silently.

### 4.6 Calibration

`τ₀` replaces `g` as the greenhouse constant. It is fitted jointly with
`D` to the M4-04 targets: a 288 K global mean surface temperature and a
42 K P2 equator-to-pole difference, at L4 with seed 1 and N = 3, using
`planet_cli thermal --calibrate`. The fit record follows the specification
§24 format.

Recorded but not gated:
- the vertical profile (lowest-layer and top-layer means);
- the effective emission height;
- the global outgoing longwave;
- the planet's sensitivity to `+1 W/m²`.

Specification §23 lesson 2 applies. Assertions check that every layer's
emissivity lies in (0, 1), that `τ₀ > 0`, and that the radiative damping
`dOLR/dT_s` is positive everywhere.

### 4.7 Snapshots and migration (ADR-0003 §3.6, M5 row)

PSNAP schema 5 stores the two fields and supports scenario-layered fields.
The migration framework becomes the ordered chain ADR-0003 §3.6 asks for:
- a table of `vN → vN+1` steps, each named;
- core-level steps (such as the 2 → 3 widening) and planet-level
  initialisers (1 → 2 surface temperatures, 3 → 4 cryosphere, 4 → 5 the
  §4.3 atmosphere);
- a log of the steps applied, returned with the manifest;
- removal of a field as a logged step.

It replaces today's per-schema hooks in `SnapshotMigration`. A v5 golden
save is added. Every golden save (v1–v5) loads through the chain and steps
ten years with closing budgets (ADR-0003 V5).

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Hydrostatics: mass after initialisation; `p_s` against the analytic Γ_c profile for a plateau; layer heights against the hypsometric equation | mass exact to rounding; profile 1e-12 relative |
| V2 | One layer over a black surface (ε = 1), no exchange, no convection: the steady state against ADR-0007's closed form with `g = ε₁` | 1e-10 relative |
| V3 | N black layers (ε_k → 1) over a black surface: `T_s⁴ = (N + 1) T_e⁴` and the exact layer ladder, N = 1…5 | 1e-9 relative |
| V4 | Convective adjustment: enthalpy conserved, result neutral where adjusted, idempotent, stable layers untouched | exact to rounding |
| V5 | Energy closure of every step, surface + atmosphere, climate and reference mode, N = 3 and 5 | ADR-0007 V2 gate |
| V6 | Plateau experiment: an aqua planet with one raised continent; surface temperature against height | between 4 and 9.8 K/km |
| V7 | Determinism and replay: workers 1/2/8/16 bit-identical; replay of the L5 performance run | bit-identical |
| V8 | Snapshots: schema 5 round trip (N = 0, 3, 5), the 4 → 5 migration, the v1–v5 golden saves stepping ten years | ADR-0003 V4, V5 |
| V9 | Calibration: τ₀ and D fitted, the record written; invariants of §4.6 | 288 ± 0.5 K, 42 ± 1 K |
| V10 | Performance: ADR-0001 250-year runs at L5 and L6, N = 3, 4 workers | ≤ 240 s, ≤ 600 s |

## 6. Consequences

**Positive.**
- The greenhouse has a physical constant and a vertical structure.
- Mountains are cold for a physical reason.
- The atmosphere stores heat, which damps land seasonality as on Earth.
- Pressure and geopotential fields exist for M6's winds: the thickness
  between pressure levels already gives the thermal wind.
- Water vapour (M7) changes τ, clouds (M8) the shortwave and longwave terms,
  and CO₂ (M12) the linear τ term, each in one place.
- Old saves keep loading through the formal chain.

**Negative.**
- Each cell solve is several times more work than the tile solves alone.
- The grey layer's `g` and ADR-0009's `D` are refitted (specification §23
  lesson 1), so every calibrated number in the M4 records moves.
- Without moisture, the surface loses heat only by radiation and sensible
  exchange, so the surface–air difference is larger than on Earth until
  M7.

**Risks and mitigations.**
- *The L6 gate fails* (D5): measured in the first column task, with the
  linearly implicit coupling (§4.5) as the prepared fallback, decided by
  amendment.
- *Split convection disturbs the transport Newton's slopes*: the column's
  response including the adjustment is what the outer iteration sees, and
  its consistency residual is reported and gated as in ADR-0009.
- *The top layer of three cannot form a stratosphere*: with three layers
  the top layer spans 0–340 hPa, so no realistic tropopause is expected
  until five layers. That is a known limit, not a defect; the design
  record targets five.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M5 | §4.1–4.7; V1–V10 |
| M6 | `p_s` prognostic; winds replace the diffusion's atmospheric share; D recalibrated or removed |
| M7 | τ from humidity replaces the quartic term's constant; latent heat leaves the surface |
| M8 | Clouds absorb and reflect; moist convection replaces Γ_c adjustment |
| M12 | CO₂ scales the linear τ term (logarithmic forcing) |

## 8. Open questions

- Should N = 5 become the default at every level once performance allows,
  or stay an L6 reference setting? Current position: L6 only (design
  record §15.1).
- Does a vertical remap between layer counts belong to the conservative
  remap of ADR-0002 §4.7? Current position: yes, when first needed; in M5 a
  save and a run must agree on N.

## 9. Proposed tasks

1. **M5-01 Migration chain** (ADR-0003 §3.6): ordered named steps, the
   log, scenario-layered fields in the registry and reader. No physics
   changes; all golden saves still load.
2. **M5-02 Atmosphere state and hydrostatics**: the fields, presets,
   hydrostatic initialisation, schema 5 and the 4 → 5 migration, and
   pressure and height diagnostics. V1 and V8.
3. **M5-03 Column radiation and convection**: the coupled implicit column,
   grey two-stream, convective adjustment, the budget and the tile
   coupling. V2–V5, V7, and a first L6 timing.
4. **M5-04 Transport, calibration and close**: θ̄_c diffusion, the τ₀ and D
   refit, the plateau experiment, the performance gates and the records.
   V6, V9, V10.
