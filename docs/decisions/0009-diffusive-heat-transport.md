# ADR-0009 — Diffusive horizontal heat transport before the atmosphere

- **Status:** Accepted
- **Date:** 2026-09-30
- **Accepted:** 2026-09-30
- **Amended:** 2026-09-30 — §4.1 tiles exchange heat with their cell's air, and tiles without area take no transport; §4.4 the calibration target is the equator-to-pole temperature difference, not the peak transport (both in §10); §4.1 the diffused temperature is the cells' air (§11); §4.3 the transport is solved on the mesh one level coarser (§12)
- **Amended:** 2026-10-08 — §13: the coupled transport stops its Newton at 1e-4 W/m² (task M6-05)
- **Milestone:** P0 / M4 (needed for ADR-0008's seasonal experiment)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §13 M3 ("optional explicitly documented reduced horizontal transport"), §13 M4, §23 (calibration lessons), §24; Planetary Civilization Simulator — Design Record v0.9, §24.2
- **Related:** ADR-0001 (modes, budget), ADR-0002 (finite-volume operators, determinism), ADR-0006 (sub-steps), ADR-0007 (surface columns), ADR-0008 (snow and sea ice, §9 finding)

## 1. Context

ADR-0007 left the planet without horizontal transport, so every cell is in
local radiative balance: the equator is too hot and the poles too cold.
ADR-0008 §9 showed a sharper consequence. Snow-covered land at 50° N in
summer absorbs about 140 W/m² but must radiate about 225 W/m² to sit at
the melting point, so snow that falls never melts: 72 % of land cells gain
snow in the first year and keep it, and the seasonal snow and ice experiment
that M4 must pass is out of reach. On Earth the difference is supplied by
heat carried poleward and inland by the atmosphere and oceans, about 5.5 PW
at its peak near 35° latitude (Trenberth and Caron, 2001).

The atmosphere arrives at M5 and ocean currents at M11. The specification
already allows M3 an "optional explicitly documented reduced horizontal
transport". This ADR decides that transport, its time integration and its
calibration.

## 2. Decision drivers

- **D1 — Physical form.** A flux down the temperature gradient, conservative
  by construction, with one documented and calibrated coefficient; no
  relaxation towards a target climate.
- **D2 — Exact budgets.** The transport moves energy between cells and
  creates none: its global sum closes to rounding, and every tile's ADR-0007
  and ADR-0008 closure keeps holding with the transport as one more flux.
- **D3 — Stable on both step lengths.** With Earth's transport strength the
  coupling between neighbouring L5 cells is about 1,800 W/m²/K, against about
  5 W/m²/K of radiative damping on a land tile whose heat capacity is small.
  An explicit step is unstable by a factor of several hundred on a monthly
  step; the integration must be implicit.
- **D4 — Exact phase change.** Transport arriving at a snow-covered tile at
  the melting point must melt snow, not raise the temperature; the clamped
  solves of ADR-0008 §4.3 must see it.
- **D5 — Replaceable.** M5 and M11 take over the atmospheric and oceanic
  shares. Specification §23 lesson 1 is binding: each replacement recalibrates
  what it partially replaces.

## 3. Options considered

### 3.1 Transport law

**A. Diffusion of surface temperature (Budyko–Sellers–North).**
`H = K ∇²T̄` in W/m², with `T̄` the cell's area-weighted surface temperature
and `K` in W/K. The standard energy-balance closure: down-gradient,
conservative, one coefficient, analytic solutions for smooth forcing
(North, 1975). Transports even where Earth's atmosphere would not (no
land–sea or moisture dependence).

**B. Relaxation of each cell towards the zonal mean.** Cheaper, but not a
flux: it creates or destroys energy unless separately corrected, and it
erases longitudinal structure. Rejected by D1 and D2.

**C. A prescribed transport climatology.** Scripted forcing; it would not
respond to ice or snow cover. Rejected by D1.

### 3.2 Time integration

**A. Explicit transport, computed from the start-of-step temperatures.**
Unstable by several hundred times on a monthly L5 step (D3).

**B. Operator splitting: implicit diffusion on a common heat capacity, then
the local columns.** The tiles of one cell have heat capacities from 1e5
(dry-soil surface layer) to 2.9e8 J/m²/K (mixed layer); any common capacity
misplaces the heat, and a month of transport dumped into a land surface
layer moves it by hundreds of kelvin before radiation can respond.

**C. Coupled implicit solve.** The transport source of every cell is the
unknown. Given sources, each tile is the ADR-0007/0008 local solve; the
sources must equal the diffusion of the resulting cell temperatures. Newton
on the sources, each iteration a symmetric positive-definite sparse system
solved by conjugate gradients; the final transport is the conservative
divergence of the last cell temperatures, so energy closes exactly however
far Newton has converged.

## 4. Decision

Transport is **option 3.1 A** (diffusion of the cell's surface temperature)
integrated by **option 3.2 C** (coupled implicit solve).

### 4.1 Law

For each cell `c` with area `A_c`, the transport source, in W/m² of cell
area, is

```text
H_c = K · (1/A_c) Σ_edges (l_e / d_e) (T̄_n − T̄_c)          (the ADR-0002 two-point Laplacian)
T̄_c = Σ_tiles f_t T_t                                    (tile area fractions, ADR-0005)
```

where `T_t` is a tile's radiating surface temperature (land surface layer,
ocean mixed layer, or the sea-ice surface once M4-02 adds it). Both tiles of
the cell receive `H_c` per unit area as an extra term in their surface
equation, like absorbed shortwave. Σ A_c H_c = 0 to rounding, for any
temperatures, because every edge flux leaves one cell and enters the other.

`K = D R²` with `D` in W/m²/K the coefficient of North's unit-sphere
formulation and `R` the planet radius, so `K` is independent of mesh
resolution. It represents the total (atmosphere plus ocean) transport until
M5 and M11 replace their shares.

### 4.2 Presets

Experiments A (dead rock) and B (aqua planet) have no atmosphere and no
currents by definition (specification §13.1), so `D = 0` there and their
results stay those of ADR-0007 bit for bit. The Earth-like preset uses the
calibrated `D` (§4.4).

### 4.3 Step

Unknown: the source `h_c` of every cell. For given sources, each tile runs
its local step with `h` added to `b` (ADR-0007 §4.3), including the ADR-0008
clamps, which yields `T̄_c(h)` and its slope `s_c = dT̄_c/dh_c ≥ 0` (zero on a
clamped tile, whose extra heat goes into melting).

1. Start from the explicit estimate `h⁰ = K ∇² T̄(start of step)`.
2. Newton on `F(h) = h − K ∇² T̄(h) = 0`. With `δT̄ = S δh`, the step solves
   the symmetric positive-definite system
   `(A / S) δT̄ − K Q δT̄ = −A F` (`Q` the symmetric edge-weight matrix,
   `A` the diagonal of cell areas; cells with `s_c = 0` keep `δT̄_c = 0`)
   by conjugate gradients with deterministic block reductions, then
   `δh = −F + K ∇² δT̄`.
3. After a fixed number of Newton iterations, the transport actually applied
   is `H = K ∇² T̄(h_N)`: the conservative divergence of the last cell
   temperatures. The tiles take their final local step with `H`. The
   consistency residual `|H − h_N|` is reported, never hidden.

Energy therefore closes exactly (D2) whatever the convergence, and the
implicit solve is stable (D3). The same step runs in climate and reference
mode. Everything is in double; the result is independent of worker count.

### 4.4 Calibration

Two constants are fitted together on the Earth-like preset (specification
§24), with sea ice and snow as they then exist:

- `D` so that the peak annual-mean poleward transport is 5.5 PW;
- the grey-layer `g` (ADR-0007) so that the global annual mean stays 288 K.

The record states the fit, the targets, the equator-to-pole temperature
difference and the seasonal snow and ice it produces. `D > 0` is asserted at
construction (specification §23 lesson 2).

### 4.5 Diagnostics

The step reports the global transport sum (which must vanish), the
consistency residual, the Newton and conjugate-gradient iteration counts,
and the northward transport across every 10° latitude, in PW.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Conservation: `Σ A H / Σ A |H|` every step | ≤ 1e-13 |
| V2 | Energy closure per tile and globally with the transport term, both modes | ADR-0007 V2 gate |
| V3 | Down-gradient: `Σ A H T̄ ≤ 0` every step | exact sign |
| V4 | Analytic: steady response of an ocean planet to a small P2 forcing anomaly; ratio of the response with and without transport against `λ / (λ + 6 D)`, `λ = 4 β ε σ T₀³` | ≤ 2 % at L5, converging with level |
| V5 | Stability: `D` up to 10× calibrated, monthly and ten-minute steps; a grid-scale checkerboard perturbation never grows | exact |
| V6 | `D = 0` reproduces ADR-0007/0008 results | bit-identical |
| V7 | Convergence: consistency residual after the fixed Newton count | ≤ 1e-6 W/m² |
| V8 | Determinism: 1/2/8/16 workers, chunked runs, replay hashes | bit-identical |
| V9 | Calibration record: peak transport, global mean, equator–pole difference, land cells whose snow clears seasonally | recorded; 5.5 PW and 288 K within the fit tolerance |
| V10 | Cost against the ADR-0001 gate | within the M3-03 gate |

## 6. Consequences

*(§4.1's equal share per unit area and §4.4's transport target are amended
in §10.)*

**Positive.** Heat reaches snow-covered land and the poles, so the seasonal
cryosphere of ADR-0008 becomes possible. The equator–pole difference moves
toward Earth's. Energy stays exact, the monthly step stays stable, and the
analytic P2 check pins the implementation. The transport is one
conservative flux that M5 and M11 can take over.

**Negative.** Diffusion knows nothing about moisture, land–sea contrast or
circulation: it transports as readily across a continent as along a storm
track. One coefficient cannot give Earth's transport and its polar
amplification at once. The Newton–CG solve costs more than the local
columns did; V10 measures it.

**Risks.** Specification §23 lesson 1: when M5 and M11 add explicit
transports, `D` must shrink by what they carry, or the planet double-counts
and loses its polar amplification. The calibration record makes the
dependency explicit.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M4 | §4.1–4.5; V1–V10; used by ADR-0008's seasonal experiment |
| M5 | The atmosphere's transport replaces the atmospheric share; `D` recalibrated or removed |
| M11 | Ocean currents replace the oceanic share; `D` recalibrated |

## 8. Open questions

- Should a coastal cell's land and ocean tiles exchange heat through their
  shared air (a mixing toward the cell mean)? *Resolved by the §10
  amendment: yes, at the bulk sensible-heat rate.*
- Should `D` vary with latitude, as moisture makes transport more efficient
  in the tropics? Current position: constant, until M6.

## 9. Implementation record (task M4-02, 2026-09-30)

| Concern | Code |
|---|---|
| Implicit solve: Newton, line search, multigrid-preconditioned CG | `sim/planet/surface/heat_transport.{hpp,cpp}` |
| Local solves with a source, an exchange term and their slope | `sim/planet/surface/column_step.{hpp,cpp}`, `sim/planet/surface/land_snow.{hpp,cpp}` |
| Cell solve with shared air, presets, diagnostics, calibration constants | `sim/planet/surface/surface_energy.{hpp,cpp}` |
| Persistent worker pool behind the deterministic executor | `sim/core/scheduler/worker_pool.{hpp,cpp}`, `sim/core/scheduler/deterministic_executor.hpp` |
| Tests | `tests/unit/test_heat_transport.cpp`, `tests/physics/test_heat_transport_planet.cpp`, `tests/unit/test_land_snow.cpp` |
| `planet_cli thermal --transport`, `--calibrate-gradient`, `--calibrate-transport` | `apps/planet_cli/main.cpp` |

Refinements of §4.3, recorded here rather than changing the decision:

- **Start and stopping.** Newton starts from zero transport, not the
  explicit estimate: at Earth-like coupling the explicit estimate is
  thousands of W/m² and far outside the local solves' range. It stops when
  `max |F| ≤ 1e-6 W/m²` (V7's gate) or when a backtracking line search on
  `Σ A F²` finds no descent (the rounding floor), not after a fixed count.
  The tile response is concave in `h`, so full steps overshoot; with the
  line search the solve converges in 4–6 iterations.
- **Floors.** Every iterate is kept above the source at which a cell's
  temperature would fall to 100 K, so every local solve has a positive
  root.
- **Precision.** Each cell's air temperature is converged to 4ε: the
  neighbour coupling (thousands of W/m²/K at L6) multiplies any error in
  it into the transport residual.
- **Linear solve.** Conjugate gradients with an aggregation-multigrid
  V-cycle preconditioner (greedy aggregation on the mesh graph weighted by
  `l/d`, Galerkin coarse operators, block-hybrid symmetric Gauss–Seidel on
  the finest level, serial below, dense Cholesky at the bottom), relative
  tolerance 1e-6 (inexact Newton). Plain Jacobi needed about 300 iterations
  per Newton step at L6; the V-cycle needs about 20. The hierarchy's
  structure is built once per step. All fine-level work runs over the
  mesh's fixed blocks, so results are independent of the worker count.
- **Worker pool.** The deterministic executor now dispatches to a persistent
  process-wide pool (task M4-02); block-to-worker assignment is unchanged.

Validation (tests at L4 with `D = 0.5` unless stated; timings with 4
workers):

| ID | Result |
|---|---|
| V1 | `Σ A H / Σ A |H|` ≤ 9e-17 |
| V2 | worst closure 0.025 of the ADR-0007 gate, a year of climate steps and a day of reference steps |
| V3 | dissipation negative on every step |
| V4 | P2 response ratio against `λ / (λ + 6 D)`: error 0.45 % (L3), 0.21 % (L4), 0.15 % (L5) |
| V5 | ten times `D` on an aqua planet with a random ±5 K perturbation: the spread never grows, monthly or ten-minute |
| V6 | transport and shared air off ⇒ the independent per-tile step, bit for bit; dead rock and aqua planet unchanged |
| V7 | consistency residual ≤ 5e-7 W/m²; Newton 4–6 iterations |
| V8 | 1/2/8/16 workers bit-identical; the 250-year L5 run replays bit for bit |
| V9 | §10 fit: at L5 288.01 K, P2 equator-to-pole 41.7 K, peak poleward transport 1.86 PW, zonal annual means 300.7 K (equator) to 260 K and 256 K (85° S, 85° N); with 1e-5 kg/m²/s of prescribed snowfall (L4) 15 % of land cells clear their snow every summer, against 1 % without transport, while snow still accumulates at high latitudes |
| V10 | 250 years: L5 107 s (gate 240 s), L6 461 s (gate 600 s); monthly step L5 about 40 ms, L6 about 170 ms |

The L6 scenario keeps about 23 % margin on this machine; slower CI runners
may take it closer to its gate.

## 10. Amendment: shared cell air and the calibration target (2026-09-30)

**Shared air (§4.1).** With every tile receiving the cell's source per unit
area, a small tile took the whole cell's transport: a 6 % land sliver in a
coastal cell reached 355 K in polar summer, and slivers in polar night fell
to 135 K, because the ocean's heat capacity sets the cell's transport and a
land surface layer cannot absorb it. The accepted change resolves the §8
open question: every tile also exchanges heat with its cell's air at

```text
γ (T̄ − T_t),   γ = ρ c_p C_H U = 1.2 · 1005 · 1.2e-3 · 7 ≈ 10 W/m²/K
```

(bulk sensible-heat exchange), where `T̄` is the area-weighted mean of the
tiles with area. The exchange sums to zero over a cell, so energy is
unchanged; the cell's `T̄` solves a scalar equation (§9). A tile without
area takes no transport and follows its cell's air. With this, tiles stay
within 228–314 K and coastal land is maritime. Dead rock and the aqua
planet have no air (`γ = 0`).

**Calibration target (§4.4).** Fitting `D` to the 5.5 PW peak transport
gives `D ≈ 1.98` and flattens the planet to a 14 K equator-to-pole
difference with polar temperatures near 278 K, leaving almost no snow or
ice: this planet's grey `T⁴` radiation damps temperature anomalies about
twice as strongly as Earth's outgoing longwave (no water-vapour feedback),
so carrying Earth's transport erases Earth's gradient. Since snow and ice
depend on where temperatures lie, the accepted target is the equator-to-pole
difference of the P2 fit to the annual-mean surface temperature, 42 K
(`T₂ ≈ −28 K`; North, Cahalan and Coakley, 1981), with `g` refitted to
288 K. The resulting transport (1.8 PW) is recorded as too weak by a factor
of about three, a deficit M5's atmosphere must take up.

## 11. Amendment: diffusing the cells' air (accepted 2026-09-30)

**Finding (task M4-03).** With sea ice, a freezing ocean holds its
temperature at `T_f` while releasing latent heat without limit. Diffusing
the cells' surface temperature `T̄` then let the implicit solve drain a
coastal cell (99 % ocean at `T_f`, next to ice surfaces some 30 K colder)
of about 5,000 W/m² for a month: 38 m of new ice in one step, and the 1 %
land tile, which receives the same source per unit area, collapsed to
−142 K. Such cells also have zero slope, which stalled the Newton
iteration.

**Change.** The diffused temperature is the cell's air, whose balance
with its tiles (§10) gives

```text
T_a = T̄ + h / (γ (f_land + f_ocean)),     H = K ∇² T_a
```

Heat leaves a cell only through its tiles' exchange with the air,
`γ (T_t − T_a)`, so a freezing ocean can give at most what the air can take,
and the slope `dT_a/dh ≥ 1/(γ s)` is never zero. For a planetary-scale
pattern of degree `l` the air adds a series resistance: the coefficient is
effectively `D γ / (γ + l(l+1) D)`, 0.18 instead of 0.20 for P2 at the
calibrated `D`; at the grid scale the exchange is capped near
`γ ΔT` (a few hundred W/m², the order of winter heat loss from open
leads). Transport now requires `γ > 0`. The V4 test compares against
`λ / (λ + 6 D γ / (γ + 6 D))` and agrees to 0.13 % at L5.

**Consequence.** `D` and `g` of §10 were fitted with surface-temperature
diffusion and without ice; they are refitted in task M4-04.

## 12. Amendment: the transport one mesh level coarser (accepted 2026-09-30)

**Finding (task M4-03).** With sea ice the 250-year CI scenarios of
ADR-0001 took about 290 s at L5 (gate 240 s) and 1,400 s at L6 (gate
600 s): freezing and melting make the tiles' response kinked, and each
transport step needed 20–25 tile evaluations and about 200 ms of linear
solves at L6.

**Change (option 2 of the choice put on 2026-09-30).** The implicit
transport is solved on the mesh one level coarser. Every fine cell belongs
to the nearest coarse cell (about four fine cells each; a greedy walk over
the coarse mesh, deterministic); a group's area is the sum of its cells',
so energy stays exact, and its links are the coarse mesh's own two-point
weights `l/d`, the ADR-0002 operator one level down, exactly symmetric. A
group's air temperature is the area-weighted mean of its cells' air
(§11); every cell of a group receives the group's source. The coarse mesh
and the map are built once per mesh level and radius and cached for the
process (`agglomerated_transport_graph`); the solver works on a
`TransportGraph`, the mesh or its agglomeration. A first attempt grouping
cells greedily into sevens was rejected: its two-point fluxes between
irregular groups were inconsistent (the P2 check stayed 5 % off at every
level).

**Validation.** The P2 check converges: 3.1 %, 0.9 %, 0.31 % at L3, L4,
L5 (gate 2 % at L5). The transport resolution is about 480 km at L5 and
240 km at L6, adequate for a large-scale diffusion. With sea ice, on 4
workers: 250 years at L5 in 176 s (gate 240 s) and at L6 in 572 s (gate
600 s); the L5 run replays bit for bit. The L6 margin is about 5 % on the
development machine, so slower CI runners may exceed it: the next
performance work belongs there.

## 13. Amendment: the coupled transport's Newton tolerance (accepted 2026-10-08)

**Finding (task M6-05).** With the climate circulation carrying the heat
(ADR-0011 §17), the implicit transport keeps this record's Newton on one
source per group. At L6 each Newton iterate is a pass over all 40,962
columns, and the iterates converge linearly, about twentyfold per step (as
this record's diffusive solve does there, most likely at the surface's
kinks: sea ice forming or melting, snow held at 0 °C). The last two passes,
from about 1e-4 to V7's 1e-6 W/m², cost a tenth of an L6 climate step and
kept the 250-year L6 run over ADR-0001's 600 s.

**Change (decided with the user).** The coupled solve stops at a
consistency residual of **1e-4 W/m²** (`coupled_newton_tolerance_W_m2`).
The transport applied is still the conservative H of the last iterate, so
energy closes exactly whatever the residual; the residual only measures
how far the step is from fully implicit (1e-4 W/m² is about 1e-6 of the
transport's local magnitude). The diffusive solve, now the coupled one's
fallback, keeps V7's 1e-6.

**Not decided.** Making Newton quadratic at the kinks, which would save
the passes without the looser tolerance.
