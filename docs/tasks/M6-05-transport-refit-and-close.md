# Task M6-05 — Transport by the circulation, refit and close

- **Milestone:** P0 / M6 (fifth and last task; M6-01 to M6-04 are complete)
- **Status:** complete (2026-10-08) — steps A–F and the L6 follow-up; M6 closes with both 250-year gates met
- **Governing decisions:** ADR-0011 §3.5, §4.6–4.8, §17 (accepted
  2026-10-06), V7, V10, V12, V13; ADR-0009 §4.3, §12; ADR-0010 §4.6, §11;
  ADR-0001 §5
- **Starts from:** M6-04's open points (`docs/tasks/M6-04-balanced-circulation.md`)

## Audit (2026-10-06, at `1df1747`)

- **The transport** (`solve_implicit_transport`, `heat_transport.cpp`) is
  ADR-0009's Newton on one heat source h per coarse group. Its residual is
  h − K ∇² T̄(h) with a single conductance K; the Newton system is solved
  for δT̄ as the symmetric (A/S − K Q) by multigrid-preconditioned CG.
- **The column response** (`surface_energy.cpp`, the surface step's
  `TransportResponse`) returns the column's mean θ_c and its slope with
  respect to h. The source enters the bottom layer (ADR-0010 §11).
- **The circulation** (`ClimateCirculation`) runs after the surface step and
  writes derived fields only; its layer mass fluxes live on the coarse
  mesh's edges, which are the transport graph's edges.
- **Calibration.** τ₀ = 1.3581 and D = 0.6371 W/m²/K (ADR-0010 §11), fitted
  with the diffusion; c_E = 1.39e12 is reference mode's fit (§15).
- **Performance.** The 250-year gates pass with the circulation diagnosed
  only: L5 217 s of 240, L6 546 s of 600.

## Plan

**Step A — the advection–diffusion solve** (`heat_transport`, no planet).
- A new solve beside `solve_implicit_transport` (which stays, for the
  fallback and the presets without circulation):
  - per-edge conductances (the eddies' c_p μ̄ D̄_e);
  - per-layer edge mass fluxes F_k, carrying the upwind group's s_k;
  - a response returning, per group, θ_c and its slope and the layers' s_k
    and their slopes;
  - Newton on h, with the line search and floors of ADR-0009 §9;
  - the non-symmetric Newton system by BiCGSTAB, preconditioned by the
    multigrid on its symmetric part and ILU(0) of the full operator (the
    ILU moves from `balanced_circulation.cpp` to a shared place).
- Tests on synthetic graphs: Σ A H = 0 to rounding; a uniform s with
  divergence-free column fluxes gives no transport; with F = 0 and uniform
  conductance it agrees with `solve_implicit_transport` to its tolerance;
  quadratic convergence on a smooth case; bit-identical for 1 and 4
  workers.

**Step B — the column's layer slopes** (`surface_energy`).
- The column solve exposes its layer temperatures and ∂T_k/∂h, and the
  layer geopotentials Φ_k (hydrostatic, true heights) with ∂Φ_k/∂h.
- Tests: the slopes against central differences of the column solve.

**Step C — the coupled climate step** (`ClimateCirculation`, surface step).
- The circulation moves ahead of the column solve, from the step's
  starting slow state (§17.2).
- The surface step takes the circulation's fluxes and diffusivities and
  calls step A's solve; a failed month uses ADR-0009's diffusion with D
  (§17.4).
- Tests: the energy budget of every climate step closes (V10, the
  ADR-0007 V2 gate) with N = 3 and N = 5; the fallback is exercised and
  bit-identical across a snapshot continuation; workers 1, 2 and 8 agree
  (V11).

**Step D — the balanced p_s in the slow state, with its energy** (§17.3).
- The cells' balanced p_s (step D of M6-04) replaces the slow state's at
  the start of each climate step.
- The mass-flux potential: Poisson on the fine mesh for the change of
  column mass; each layer's 1/N share of the flux carries its upwind s_k;
  the cells' layer temperatures are set to their new energy.
- Tests: mass held exactly; energy Σ (c_p T m + Φ_s M) held to rounding; a
  uniform-temperature atmosphere over flat ground keeps its temperature;
  PSNAP schema unchanged (p_s is already a slow field).

**Step E — refit** (§4.8, specification §24).
- τ₀ and c_E jointly to 288 ± 0.5 K and 42 ± 1 K at L4, seed 1, N = 3.
- Re-measure §16's tropical damping with the coupled transport.
- Update the golden replay hashes and record why they changed.

**Step F — close.**
- V7 (gated now), V10, V12 (calibration and climate against reference
  zonal means), V13 (the 250-year gates, which must still pass with the
  transport's cost), V11.
- README, ADR-0011 implementation record, specification and CLAUDE.md;
  M6 closes.

## Step A — the advection–diffusion solve (2026-10-06)

- `solve_implicit_advection_diffusion` and `advection_diffusion_source`
  (`heat_transport.{hpp,cpp}`), with `AdvectionDiffusion` (per-face eddy
  conductances, per-layer face outflows) and `AdvectionResponse`.
- **Newton** on h as ADR-0009 (floors, backtracking, bounded non-monotone
  steps). J = I − ∂H/∂h is assembled on the graph's pattern (H_a depends on
  h_a and its neighbours only) and solved by BiCGSTAB with ILU(0) of J.
  The multigrid stage was not needed: 4–11 BiCGSTAB iterations a Newton step.
- `IncompleteLU` moved to `sim/core/math/incomplete_lu.{hpp,cpp}`; the
  balance uses it unchanged (same hashes).
- **Tests** (`tests/unit/test_advection_diffusion.cpp`, L3 graph, synthetic
  columns): a uniform s with divergence-free column fluxes carries 0 (to
  1e-9 W/m²); Σ A H = 0 to 1e-12 of Σ A |H|; with no flux it is ADR-0009's
  solve to 1.1e-10 W/m² (5 Newton steps); with fluxes, the consistency
  residual is 1e-11 W/m², 1 and 4 workers agree bit for bit.

## Step B — the column's layer slopes (2026-10-06)

- `ColumnSolveResult::temperature_slope_K_m2_W`: dT_k/dh of the implicit
  step, through the convective pools (the column already solved it).
- `layer_dry_static_energy` (`atmosphere.{hpp,cpp}`): s_k = c_p T_k + Φ_k at
  the mean-Exner levels, as the balance. It does not depend on p_s and is
  linear in Φ_s and T, so it also gives the slopes.
- **Tests** (`test_atmosphere_column`): the layer slopes and s's slopes
  against central differences (1e-6 relative) for 1, 3 and 5 layers; the
  scheme's energy identity, mean_k Φ_k = Φ_s + R mean_k T_k, to rounding.

## Step C — the coupled climate step (2026-10-06)

- `ClimateCirculationUse { none, diagnostic, coupled }` for `PlanetRun`
  (diagnostic stays the default until step E). `planet_cli run --circulation`.
- **Coupled:** the circulation is registered before the surface, sets the
  sub-step's insolation, solves from the step's starting state, and hands
  `CirculationTransport` (fluxes, conductances, Φ_s) to the surface step,
  which calls step A's solve. Groups take each layer's s mass-weighted over
  their cells.
- **The eddies' conductance** per face: c_p (p̄_s/g) D̄(φ_e) l/d, with D̄ the
  layers' mean of the zonal model's D_k interpolated to the face's latitude.
- **The azonal flow carries no heat** (ADR-0011 §17.6, decided with the
  user). With it, the first month at L4 had bottom-layer azonal winds of
  17 m/s rms (116 m/s at 11° S) and balanced p_s jumps of 18–34 hPa between
  tropical neighbours; ascending columns then export W Δs whatever their
  temperature, and even 10% of the azonal flux drove a column to 0 K. With
  the overturning alone, Newton converges in 4–6 steps.
- **Tests** (`tests/physics/test_coupled_transport.cpp`, L3): every month of
  a year is coupled and closes its energy budget (5e-6 of the ADR-0007 V2
  gate) with N = 3 and N = 5 (V10); Σ A H = 2e-16 of Σ A |H|; 1 and 4
  workers agree (V11); a step given an inactive circulation is ADR-0009's
  diffusion bit for bit (§17.4).

## Step D — the balanced p_s in the slow state (2026-10-06)

- `PressureRedistribution` (`sim/planet/dynamics/pressure_redistribution.{hpp,cpp}`):
  the Poisson solve for χ on the fine mesh (multigrid-preconditioned CG to 1e-10;
  formerly 1e-12, which stalls at rounding), each layer's 1/N of the mass flux carrying its s upwind, the
  cells' layer energy m_l (c_p T_l + Φ_s) and new temperatures.
- Coupled runs write the cells' balanced p_s after every solved month.
- **Energy.** Σ (c_p T m + Φ_s M) holds to 2e-17 relative; enthalpy and
  potential energy trade exactly (an imposed ±2% p_s pattern at L3:
  ±8.7e18 J). Columns gaining mass warm by compression (up to 3.7 K for
  20 hPa), those losing it cool.
- **In a run** (L3, a year): month-to-month changes of p_s reach 12.7 hPa,
  of layer temperature 1.6 K; mass held to 1e-12.

## Step E — refit (2026-10-06 to 2026-10-08)

- **The joint fit cannot reach 42 K** with overturning and eddies only
  (150-year coupled spin-ups at L4, τ₀ = 1.3581):

  | c_E | Failed months | P2 equator-to-pole | Peak transport |
  |---|---|---|---|
  | 1.39e12 (reference mode's fit) | 0 | 52.9 K | 2.9 PW |
  | 2.0e12 | 0 | 50.2 K | 3.1 PW |
  | 2.8e12 | 5 | 48.0 K | 3.2 PW |
  | 4.0e12 | 1,132 of 1,800 | 43.0 K (mostly the fallback) | 3.8 PW |

- **Decided with the user (ADR-0011 §17.7):** c_E stays 1.39e12, τ₀ alone
  is fitted to 288 K, and the 42 K target waits for M7 and M11.
- **τ₀ = 1.442** (1.440 → 287.97 K, 1.448 → 288.09 K, 1.456 → 288.20 K; P2
  52.2 K, peak transport 2.83 PW, no failed month). The 150-year runs end
  with the same −0.74% top-of-atmosphere imbalance as M5's diffusive one.
- **Coupled is the default** (`PlanetRun`, `planet_cli run`); a coupled run
  spins up coupled. Every hash of an Earth-like run at L3 and finer
  changes with it; L0–L2, dead rock and the aqua planet are unchanged.
- The `thermal` command's joint bisection was lost to the background time
  limit (its output was buffered); the fit was made by hand from single
  evaluations instead.
- §16's tropical damping is kept: the azonal flow no longer carries heat,
  so its tropical strength does not reach the climate.

## Step F — close (2026-10-08)

**V7** (gated now; L5, 20 coupled spin-up years, the decade's zonal means):

| | North | South | Gate |
|---|---|---|---|
| Bottom layer, 5–20° | −0.17 m/s | −0.57 m/s | easterly |
| Bottom layer, 40–55° | +0.72 m/s | +0.62 m/s | westerly |
| Hadley edge | 33.2° | −33.0° | 20–40° |
| Top-layer jet | 33.4 m/s at 47.5° | 34.1 m/s at −52.5° | 25–50° |

- Seven of eight criteria pass; the southern jet is 2.5° poleward of the
  gate, as reference mode's own jet (52.5° at L4, M6-04 step B). Recorded,
  not forced.
- **V9:** torque 7.7e-14 of the gross. **V10:** every coupled step closes its
  budget (5–7e-6 of the ADR-0007 V2 gate, N = 3 and 5). **V11:** 1 and 4
  workers bit-identical. **V12:** climate against reference zonal means:
  coupled jets of 33–34 m/s against reference mode's 28 m/s at L4, the
  same latitude in the south; gated from M7 (ADR-0001 §8).

**V13, the 250-year gates** (idle machine, clang Release, 4 workers):

| | Total | Transport solve | Balance | Zonal | p_s | Heating |
|---|---|---|---|---|---|---|
| L5 | 220 s, passed (240) | 83 s | 25 s | 68 s | 7 s | 7 s |
| L6 | 678 s, passed at 750 (ADR-0001 §11; was 600) | 301 s | 125 s | 82 s | 27 s | 24 s |

- The first coupled runs measured L5 243–268 s and L6 799 s. The cuts that
  change no conserved quantity:
  - the redistribution's CG tolerance 1e-6 (energy closes regardless) and
    then the coarse groups (ADR-0011 §17.8): 52 → 7 s at L5;
  - the balance's tolerance 1e-6 in the climate circulation (only the
    overturning carries heat, and its column fluxes cancel exactly);
  - block ILU(0) on the graph's fixed blocks, factored and solved in
    parallel, for the coupled transport.
- L6's largest cost is the coupled Newton's passes over all 40,962 columns
  (8 passes, about 70 ms a month), with BiCGSTAB about 30 ms.
- Timing is now reported per part: `planet_cli run` prints the transport
  solve's and the redistribution's seconds beside the circulation's.

## Follow-up — L6 back under 600 s (2026-10-08)

| Change | L6, 250 years |
|---|---|
| At the close above | 678 s |
| Coupled transport preconditioned by multigrid on A Θ⁻¹ + K, then block ILU (BiCGSTAB 161 → 52 iterations a month); the balance's ILU in parallel blocks | 665 s |
| The coupled Newton stops at 1e-4 W/m² (ADR-0009 §13, decided with the user): 7 → 5 passes over the columns | 619 s |
| Inexact linear solves (BiCGSTAB to 1e-3 of each Newton residual); the balanced p_s mapping in parallel | **588 s, passed** |

- L5 runs in 219 s. Every coupled step still closes its budget (3.7e-7 of
  the gate at L6), because the transport applied is the conservative H.
- The coupled Newton converges linearly (about twentyfold a step), as the
  diffusive solve does at L6; making it quadratic at the surface's kinks
  would buy back margin without a looser tolerance. The L6 margin is about
  2%.
- ADR-0001 §11's 750 s is withdrawn: the L6 gate is 600 s again.

## Risks

- **Cost.** The transport's inner solve becomes BiCGSTAB with a larger
  operator, and the circulation must now run every month. The L5 margin is
  23 s and the L6 margin 54 s.
- **The refit may not reach both targets** with advection: the transport's
  structure is no longer a free diffusivity. If it cannot, the finding goes
  to an amendment before any constant is forced.
- **Newton's robustness:** advection makes the response less monotone than
  diffusion; the line search and the non-monotone steps may need tuning.
