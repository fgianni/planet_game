# ADR-0009 — Diffusive horizontal heat transport before the atmosphere

- **Status:** Accepted
- **Date:** 2026-09-30
- **Accepted:** 2026-09-30
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
  shared air (a mixing toward the cell mean)? Current position: no; tiles
  receive the same source. Revisit if coastal land stays too cold.
- Should `D` vary with latitude, as moisture makes transport more efficient
  in the tropics? Current position: constant, until M6.
