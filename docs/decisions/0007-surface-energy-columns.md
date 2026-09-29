# ADR-0007 — Surface energy columns for the first thermal planet

- **Status:** Accepted
- **Date:** 2026-09-29
- **Accepted:** 2026-09-29
- **Milestone:** P0 / M3 (surface energy and first thermal planet)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §9.1, §13 M3, §13.1 (experiments A–C), §23, §24; `docs/prototype-oracle.md`; Planetary Civilization Simulator — Design Record v0.9, §5.3, §28
- **Related:** ADR-0001 (modes, budget), ADR-0002 (precision), ADR-0003 (snapshots, migration), ADR-0005 (land and ocean tiles), ADR-0006 (sub-steps and their forcing)

## 1. Context

M3 gives the planet its first prognostic temperatures. The specification
fixes the physics in outline:

```text
C dT/dt = absorbed shortwave − outgoing longwave + ground/ocean exchange
          (+ optional, documented, reduced horizontal transport)
Q_absorbed = (1 − albedo) · Q_solar          Q_longwave ≈ ε σ T⁴
```

with distinct thermal properties for ocean, rock, dry soil and wet soil;
land must show stronger diurnal and seasonal variation than ocean; there is
no dynamic atmosphere and no faked lapse-rate effect. Acceptance: stable
integration under documented timestep limits, per-cell and global energy
accounting, correct signs, predictable simplified equilibria, and land/ocean
thermal-inertia A/B tests. Experiments A (dead rock) and B (aqua planet) are
the first headless scenarios.

The prototype (`docs/prototype-oracle.md`) supplies calibrated heat
capacities but also the shortcuts this project may not copy: a linearised
longwave term, a lumped single-layer land, and a split CO₂ forcing.

Several choices made here outlive M3: the layer structure every later
surface process writes into, the longwave formulation later replaced by the
atmosphere, the time integration that climate mode depends on, and the first
real change to the persistent slow state.

## 2. Decision drivers

- **D1 — Physical form.** State and fluxes, documented constants, T⁴
  longwave; no scripted modifiers.
- **D2 — Exact budgets.** Energy closes per tile, per cell and globally to
  rounding, every step.
- **D3 — Two time scales.** The same physics must run in reference mode
  (10-minute steps, instantaneous forcing, diurnal cycle) and climate mode
  (month-long ADR-0006 sub-steps, sub-step mean forcing) without a stability
  limit on the long step.
- **D4 — Testable equilibria.** Simplified cases must have closed-form
  answers.
- **D5 — Useful for M4.** Snow and ice need freezing and thawing to happen
  somewhere on an Earth-like planet before the atmosphere exists.
- **D6 — Saves survive.** Adding slow state must not orphan existing
  snapshots (ADR-0003 §3.6).

## 3. Options considered

### 3.1 Column structure

**A. One lumped layer per cell (the prototype).** Cannot show a diurnal
cycle and a seasonal cycle with the same heat capacity, and has no
"ground/ocean exchange" term. Rejected by D3.

**B. Two layers per ADR-0005 tile — chosen.** Each cell has a land tile and an
ocean tile, weighted by the land and ocean fractions. The land tile has a
thin surface layer over a ground layer; the ocean tile has a mixed layer
over a deep layer. The exchange between the two layers is the specification's
"ground/ocean exchange".

**C. Multi-layer soil and ocean profiles.** More faithful, not needed to meet
M3's acceptance, and easy to add later below the two-layer interface.
Deferred.

### 3.2 Longwave

**A. Linearised A + B·T (the prototype).** Calibrated, but not physical in
form. Rejected by D1.

**B. Surface εσT⁴ to space only.** Physical for a planet without an
atmosphere (experiment A). With surface albedo alone (about 0.1 globally,
since M3 has no clouds) an Earth-like planet equilibrates near 273 K on
global average, 15 K colder than Earth. Once M4 adds the ice–albedo feedback,
such a planet is prone to glaciating, which defeats D5's purpose of a
plausible seasonal cryosphere.

**C. εσT⁴ with an optional single-layer grey atmosphere — chosen.** The
textbook isothermal layer of longwave emissivity `g`, in radiative
equilibrium and transparent to sunlight, reduces the outgoing longwave to
`(1 − g/2)·εσT⁴`. It is a static, documented reduced greenhouse formulation
(specification §9.1), not a dynamic atmosphere and not a lapse-rate effect.
`g = 0` for dead rock.

For the Earth-like preset, `g` is a **calibration constant**, and is treated
as one (specification §24): versioned with its fit target, date and run. The
textbook `g ≈ 0.78` assumes Earth's planetary albedo of about 0.3, most of it
clouds. M3 has no clouds, so the planet's albedo is the surface's, about 0.1
globally, and `g = 0.78` would give a global mean near 309 K. The fit target
for M3 is a global annual mean of 288 K, which needs `g` near 0.39; it exists
to give M4 freezing and thawing in plausible places, and is refitted when
clouds (M8) and the atmosphere (M5) arrive.

### 3.3 Time integration

**A. Explicit.** A month-long step on a land surface layer of heat capacity
~10⁵ J/m²/K is far beyond its stability limit. Rejected by D3.

**B. Backward Euler, solved per tile — chosen.** Unconditionally stable for
any step, and its end-of-step fluxes make the energy budget close exactly.
The coupled two-layer T⁴ system is solved by a fixed number of Newton
iterations from the previous state; the iteration residual is a reported
diagnostic, not a hidden error.

### 3.4 New slow state and old saves

**A. Regenerate the golden save.** Contradicts the golden-save rule and
ADR-0003 §3.6. Rejected.

**B. Schema v2 with the first migration — chosen.** The new fields are
registered slow state; PSNAP schema becomes v2; the reader keeps loading v1
through a `v1 → v2` migration whose initialiser is a declared function
(ADR-0003 §3.6: never silently zero). This brings the first link of the M5
migration chain forward, deliberately and minimally.

## 4. Decision

### 4.1 State

| Field | Tile | Type | Partition |
|---|---|---|---|
| `land_surface_temperature_K` | land, surface layer | `float` | slow |
| `land_ground_temperature_K` | land, ground layer | `float` | slow |
| `ocean_mixed_layer_temperature_K` | ocean, mixed layer | `float` | slow |
| `ocean_deep_temperature_K` | ocean, deep layer | `double` (ADR-0002: slow ocean reservoir) | slow |

Every cell carries both tiles' temperatures, including a tile with zero area,
so that a later change of sea level exposes land or floods it with a defined
state. Fluxes, albedo and the absorbed and emitted terms are derived fields.
IDs use a new subsystem prefix `0x0003'xxxx`.

### 4.2 Materials

A parameter table with SI units, documented sources and no hidden tuning:

| Material | Surface-layer heat capacity | Conductance to ground / deep | Albedo | Emissivity |
|---|---|---|---|---|
| Ocean | mixed layer 2.9e8 J/m²/K (~70 m) | 0.7 W/m²/K to a 2.5e9 J/m²/K deep layer | 0.06 | 0.97 |
| Rock | from ρc and a skin depth | from conductivity and depth | ~0.25 | ~0.95 |
| Dry soil | idem | idem | ~0.30 | ~0.95 |
| Wet soil | idem | idem | ~0.15 | ~0.97 |

The ocean values are the prototype's calibrated ones. Land values come from
standard volumetric heat capacities and conductivities, with the surface layer
thick enough to hold a diurnal cycle and the ground layer a seasonal one; the
task records the chosen numbers and their sources. Until hydrology (M9)
supplies soil moisture, the land material is a scenario choice: rock for
dead rock, dry soil for the Earth-like preset; wet soil is exercised by tests.
Sea ice and snow are M4.

### 4.3 Step

For each tile, one backward-Euler step of length Δt:

```text
C_s (T_s' − T_s) = Δt [ (1 − α) Q − (1 − g/2) ε σ T_s'⁴ − k (T_s' − T_g') ]
C_g (T_g' − T_g) = Δt [ k (T_s' − T_g') ]
```

solved by a fixed number of Newton iterations (starting at 6), identical in
both modes. `Q` is the ADR-0006 §4.3 sub-step mean in climate mode and the
instantaneous M1 insolation in reference mode. There is no horizontal
transport in M3; transport arrives with the atmosphere (M5) and the ocean
(M11), which also avoids the double counting that specification §23 warns
about.

### 4.4 Budget

Per tile, the stored-energy change `C_s ΔT_s + C_g ΔT_g` equals
`Δt · [(1 − α) Q − OLR(T_s')]`, both evaluated from the solved end state. Cell
and global budgets are area-weighted sums through
`reduce_deterministic_blocks`.

### 4.5 Scheduling and spin-up

The surface is the first physics process registered with the M2-04
scheduler, in both climate and reference mode. Initial temperatures are the
closed-form radiative equilibrium of the annual-mean forcing at each cell,
both layers equal. Spin-up to a seasonally repeating state runs climate-mode
sub-steps outside the scored run (ADR-0006 §4.2).

### 4.6 Snapshots

PSNAP schema v2 adds the four fields. Reading v1 applies a `v1 → v2`
migration that initialises them with the §4.5 equilibrium (a declared
function of the planet parameters and the stored hypsometry and sea level).
The v1 golden save stays and must load through the migration; a v2 golden
save is added.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Stability: any Δt up to one sub-step, all materials; finite, positive temperatures | exact |
| V2 | Energy closure per tile and globally, every step | relative residual ≤ 1e-9 |
| V3 | Signs: more insolation warms; higher albedo, emissivity or smaller `g` cools | exact |
| V4 | Closed-form equilibrium under constant forcing: `T = [(1 − α) Q / ((1 − g/2) ε σ)]^¼`, both layers | ≤ 1e-6 K |
| V5 | Thermal inertia A/B: diurnal (reference mode) and seasonal (climate mode) amplitude of a land column vs an ocean column under the same forcing | land larger; ratio recorded |
| V6 | Experiment A (dead rock, `g = 0`): day/night range at the equator; global absorbed = emitted after spin-up | range recorded; balance ≤ 1e-3 |
| V7 | Experiment B (aqua planet): small seasonal cycle; global balance after spin-up | balance ≤ 1e-3 |
| V8 | Determinism: 1/2/8/16 workers, and chunked vs single `run_until` | bit-identical |
| V9 | Snapshots: v2 round trip bit-identical; v1 golden loads through the migration with the declared initial state | exact |
| V10 | Cost per climate sub-step at L5 and L6 against ADR-0006 §4.5 (250 ms, 1 s) | measured; flag if over |

## 6. Consequences

**Positive.** Temperatures come from state and fluxes in a physical form. The
budget is exact by construction. Climate mode's long step is stable. Four
closed-form or near-closed-form checks exist before any calibration. M4 can
start with plausible freezing and thawing. The migration path of ADR-0003 is
exercised on real data long before M5.

**Negative.** Without transport, the equator is too hot and the poles too
cold compared with Earth; the §23 targets (polar amplification, 13–15 °C
preindustrial) are not expected until M5 and M11. The grey layer must be
removed or reinterpreted when the atmosphere of M5 provides its own
longwave; that change will need recalibration (specification §23, lesson 1).

**Risks and mitigations.**

- *The grey layer becomes a tuning knob* → it is one, openly: a single
  calibration constant with one recorded fit target (§3.2 C), refitted and
  re-recorded when clouds and the atmosphere arrive, under the harness of
  specification §24.
- *Newton fails to converge at extreme forcing* → fixed iterations with the
  residual reported and a test at the hottest and coldest cases.
- *Material values look like tuning* → each is sourced in the task record.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M3 | §4.1–4.6; V1–V10 |
| M4 | Snow and ice on the land tile, sea ice on the ocean tile, modifying albedo and heat capacity |
| M5 | The atmosphere takes over the grey layer's longwave role |
| M9 | Soil moisture selects dry or wet soil per cell |
| M11 | Ocean transport between ocean tiles |

## 8. Open questions

- Should the land surface layer's depth follow the diurnal skin depth of each
  material exactly, or use one depth for all? Current position: one depth per
  material from its diffusivity.
- Is `g` global, or does it vary with latitude to stand in for water vapour?
  Current position: global and constant in M3; anything more is atmosphere.
