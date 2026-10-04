# Task M6-04 — Climate-mode balanced circulation

- **Milestone:** P0 / M6 (fourth task; M6-01 to M6-03 are complete)
- **Status:** in progress — step A done; ADR-0011 §14 accepted (2026-10-04); step B part 1 (the zonal-mean model) done
- **Scope:** ADR-0011 §4.4–4.6 in four steps.
  - **Step A, the method.** Zonal-mean reference data, the eddy closure
    and the solution method decided against it (§14).
  - **Step B, the zonal-mean model.** §14 in C++ on the bands, with the
    column heating, Held–Suarez and Earth-like tests; V8, and V9 for the
    zonal part.
  - **Step C, azonal balance and balanced `p_s`.** §4.6 on the coarse
    mesh: the edge balance, the elliptic `p_s` solve (BiCGSTAB with the
    multigrid preconditioner), and the layer mass fluxes and vertical
    fluxes of §4.4.
  - **Step D, outputs.** The derived wind, sea-level pressure and stress
    fields; the climatology fields; the presentation snapshot; reference
    mode starting from the balanced circulation; the first L6 timing. V7
    provisional.
- **Governing decisions:** ADR-0011 §4.4–4.6, §14, V7–V9;
  ADR-0010 §4.4, §4.6; ADR-0001 §4.1–4.3 (derived fields, fitted to
  reference mode)

## Step A — the method (2026-10-04)

**Reference data.** `ZonalStatistics`
(`sim/planet/dynamics/zonal_statistics.{hpp,cpp}`,
`tests/unit/test_zonal_statistics.cpp`) reduces time means to 5° bands:
- the zonal wind and the transient eddy kinetic energy;
- the temperature;
- the eddy heat and momentum fluxes (transient and stationary);
- the mass streamfunction.

`planet_cli dynamics` and `reference` print the means and write them with
`--zonal-csv`. Three L4 runs (clang Release, 6 workers each, run side by
side):

| Run | Settings | Wall | Result |
|---|---|---|---|
| Held–Suarez, N = 3 | 400 days, last 300 averaged | 5.2 min | jets 25.8 m/s at 32.5° N; trades, westerlies, polar easterlies |
| Held–Suarez, N = 5 | 600 days, last 450 averaged | 13.2 min | — |
| Earth-like reference mode, N = 3 | 1,096 days, last 730 averaged | 21.6 min | energy drift −3.1e-5 per year over the whole run, the spin-up shock (M6-03) |

The CSVs and the analysis scripts are in `tools/zonal_mean_prototype/`.

**Findings.** They are recorded in ADR-0011 §14:
- **The Taylor-identity closure fails.** It gets the momentum flux wrong in
  sign at the jet in all three runs, and is 5–30 times too large.
- **Eddy momentum flux** follows `ℓ_m ∂E/∂y`, with ℓ_m = 1.7–2.7e5 m.
- **Eddy kinetic energy** follows `|∂θ̄/∂y|²`.
- **Heat flux** follows the mixing length `ℓ_h √(2E) σ²`. ℓ_h is
  1.19–1.25e5 m, the same in all three runs.
- **The Python prototype** of the axisymmetric model reaches its steady
  state by Newton in 5 iterations. That is the same state as a 400-day
  march.
  - Uncalibrated, it gives trades, westerlies, polar easterlies and a jet
    within 5° of the reference.
  - At Ω = 0 the zonal wind is zero, with one cell per hemisphere.

**Decided with the user (2026-10-04).**
- **Closure:** the EKE-gradient closure of §14.
- **Solution:** Newton to the steady state of the nonlinear axisymmetric
  model, with pseudo-transient continuation as the fallback.
- §14 was accepted on 2026-10-04.

## Step B, part 1 — the zonal-mean model (2026-10-04)

**Code.**
- `ZonalCirculation` (`sim/planet/dynamics/zonal_circulation.{hpp,cpp}`)
  implements §14 on 36 bands and N σ layers.
- `BandedMatrix` and `BandedLU` (`sim/core/math/banded_lu.{hpp,cpp}`): LU
  with partial pivoting, as in LAPACK's dgbtrf.
- `planet_cli zonal-circulation` solves it under Held–Suarez forcing and,
  with `--reference FILE.csv`, prints it beside a reference run's zonal
  means.
- Tests: `tests/physics/test_zonal_circulation.cpp`,
  `tests/unit/test_banded_lu.cpp`.

**Discretisation, as built.** These choices are within §14; each was
forced by a measured failure:
- **E is an unknown.** The column eddy kinetic energy is an unknown of
  each band, beside ū_k and θ̄_k, so the screened equation stays local.
  The Jacobian is then banded, with 3N + 2 unknowns per band (§14
  estimated 3N + 1).
- **Exact Jacobian.** The residual is one template, evaluated on doubles
  or on forward-mode dual numbers. Columns coloured five blocks apart
  give the exact banded Jacobian in 5(3N + 2) evaluations. It agrees with
  central differences to 3e-7 of the row scale, converging as h².
- **Limited fluxes.** The meridional fluxes of M and θ̄ use limited MUSCL
  face values (van Albada, which is differentiable), not first-order
  upwind.
  - First-order upwind diffuses the planetary angular momentum by
    ½|F| ΔM, and ΔM ≈ 2e8 m²/s per band.
  - That is a spurious torque of about 1e-5 m/s² wherever |v̄| varies, as
    large as the friction. It left ±1 m/s two-band noise in the bottom
    wind, which the latitude C-grid's averaged Coriolis term does not
    restore.
  - The limiter keeps the face values upwind at extrema of M, which is
    Hide's constraint.
- **Smoothed |F|.** The upwind |F| is F² / √(F² + ε²) with ε = μ × 0.01
  m/s, which is zero at rest.
  - The first choice, √(F² + ε²), diffused M and θ̄ with no flow, so a
    resting atmosphere felt a torque.
  - The eddy and gust speed is √(E + √(E² + w⁴)), smooth through E = 0.
    That matters at Ω = 0, where E vanishes.
- **Isobaric gradients.** ∂θ̄/∂y in the closure and in the numerical
  diffusion is taken on pressure surfaces:
  `∂lnθ/∂y|_p = ∂lnθ/∂y|_σ − (∂lnθ/∂ln p) ∂ln p/∂y|_σ`.
  - Along σ, an isothermal atmosphere at rest over terrain generated eddy
    energy and heat fluxes.
  - The correction is exact for an isothermal column. That rest state is
    now the exact steady state (residual below 1e-9, wind below 1e-9 m/s)
    at Ω = 0 and at Earth's Ω.

**Solution, as built.**
1. **Newton first.** Newton from §14's initial state, with an Armijo line
   search on the scaled RMS residual: at most 12 iterations and 4 halvings
   of the step.
2. **Continuation as fallback.** If Newton fails, pseudo-transient
   continuation starts again from the same initial state.
   - Backward Euler in ū, θ̄ and v̄, with E and the lid held.
   - The step grows as the RMS residual falls, at most 2× per iteration.
   - A step that would raise the RMS by half is rejected, and the
     pseudo-step cut fourfold.
3. **Newton polish.** Every 10 accepted continuation steps, Newton is
   tried from a copy of the iterate.
4. **Convergence.** The scaled maximum residual (m/s and K per day) falls
   below 1e-8.

Findings:
- **Newton alone rarely converges from §14's initial state**, even when
  T⁰ is the equilibrated temperature. That state is thermal wind with no
  Hadley cell and E = 0, and line searches stall at 7 m/s/day. Plain
  Newton diverges.
- **Small pseudo-steps stall too.** Continuation alone stalls with
  residuals of about 1 m/s/day in the polar bands (N = 5), where small
  pseudo-steps follow slow dynamics that never settle.
- **The steady state is unstable to time-marching.** From that stalled
  iterate, Newton converges quadratically: 9.6 → 7.1 → 3.4 → 1.2 → 4e-2 →
  3e-4 → 6e-8 → 9e-12. Hence the polish.
- **The answer is the same whichever path reaches it.** At Ω and 2Ω,
  starting from T_eq or from the equilibrated temperatures gives the same
  steady state to 5e-9 m/s.
- **At Ω/2 it is not unique.** The two starts reach states 12–16 m/s apart
  (N = 3 and N = 5). The N = 3 Ω/2 solution also has two-band noise in
  the polar bottom wind.

**Held–Suarez** (`planet_cli zonal-circulation --reference
tools/zonal_mean_prototype/data/hs_L4_N3.csv`; c_E = 1.7e12, the HS N = 3
fit, ν = 5e4 m²/s):

| | Model, N = 3 | Reference, L4 N = 3 | Model, N = 5 | Reference, L4 N = 5 |
|---|---|---|---|---|
| Jet | 24.3 m/s at 27.5° | 25.8 m/s at 32.5° | 46.4 m/s at 32.5° | 30.4 m/s at 42.5° |
| Bottom winds, 2.5–87.5° N | EEWEWWWWWWW0EEEEEE | EEEEWWWWWW0EEEEEEE | EWEEWWWWWEEEEEEEEW | EEEEEWWWWWWWEEEEEE |
| Bottom-layer contrast | 41.7 K | 38.7 K | 41.1 K | 38.5 K |

- **N = 3 is close.** The jet is 5° equatorward. The trades reach 7.5°
  (reference 17.5°), and 12.5° is westerly where the Hadley cell's descent
  brings the upper branch's momentum into the bottom layer.
- **N = 5 is not.** The jet is half as strong again and 10° equatorward,
  and the westerlies stop at 42.5°. Raising c_E strengthens the jet
  (76 m/s at 3e12) instead of moving it poleward, and at 4.35e12 (the N = 5
  fit without χ) no steady state is found.
- **The cause is not yet diagnosed.** The momentum closure's vertical
  distribution is a candidate: uniform over σ < 0.7, where the reference
  concentrates it near the jet. The Earth-like preset runs on three
  layers (M5-04), so this does not block the climate path, but c_E's
  calibration (§4.8) must be rechecked at N = 5.

**V8 and V9** (N = 3, from T_eq):

| Ω × | Hadley edge | Max \|ψ\| (1e9 kg/s) | Jet | Surface torque / gross |
|---|---|---|---|---|
| 0 | 90° (one cell) | 2,181 | 0 (exactly) | 0 |
| ½ | 30° | 875 | 43.5 m/s at 32.5° | 2e-15 |
| 1 | 25° | 231 | 24.3 m/s at 27.5° | 8e-16 |
| 2 | 20° | 55 | 13.2 m/s at 22.5° | 1e-15 |

At Ω = 0 the wind and the eddy energy are zero to rounding. The torque is
the rounding of a converged solution: the transport and the eddies are in
flux form.

**Cost** (GCC Release, one core; first figure from T_eq, second from the
equilibrated temperatures):

| | Ω × 0 | Ω × ½ | Ω × 1 | Ω × 2 |
|---|---|---|---|---|
| N = 3 | 72 / 180 ms | 63 / 19 ms | 23 / 17 ms | 10 / 7 ms |
| N = 5 | 263 / 671 ms | 430 / 163 ms | 113 / 132 ms | 56 / 108 ms |

- **Above the estimate.** §14 estimated well under 50 ms at N = 5; the
  measured cost is 2–3 times that at Earth's rotation.
- **Where the time goes.** Each Jacobian costs 5(3N + 2) dual
  evaluations of the residual. A Jacobian that evaluates only the rows
  each colour touches would cut that roughly fivefold.
- **Optimisation is deferred to step D's L6 timing, where the budget is
  set.**

**Tests** (`test_zonal_circulation`, 0.3 s):
- **Rest.** Over terrain, the rest state is exact at Ω = 0 and Ω.
- **Jacobian.** It matches central differences; nothing lies outside
  the band.
- **Held–Suarez, N = 3.**
  - The jet is within 22.5–37.5° at 18–35 m/s.
  - Trades at 2.5° and 7.5°; westerlies over 27.5–42.5°; easterlies over
    67.5–77.5°. Each holds in both hemispheres.
  - The bottom-layer contrast is 33–45 K.
  - The solution is symmetric about the equator to 1e-6 m/s.
  - A second solve repeats it bit for bit.
- **Newton's convergence.** From a perturbed solution, Newton converges
  quadratically.
- **V8.** At Ω = 0: no wind, no eddy energy, one cell. The Hadley edge
  narrows monotonically with Ω and lies within 20–35° at Ω.
- **V9.** The total torque is at most 1e-9 of the gross for every Ω.
- **N = 5** converges.
- **Validation.** Invalid forcing and a single layer are refused.

The solve is sequential, so the plan's "1, 2 and 8 workers" check does not
apply to it. It applies to the band means and the mapping to the mesh,
which step B part 2 adds.

## Plan for steps B–D

1. **`ZonalCirculation`** (`sim/planet/dynamics/zonal_circulation.{hpp,cpp}`)
   — done, step B part 1 above:
   - the bands, the σ-layer vertical structure of M6-03, and the residual;
   - the banded Jacobian, banded LU, Newton and pseudo-transient
     continuation.
   - Tests:
     - the residual vanishes on an exact state at rest (isothermal,
       Ω = 0);
     - the Jacobian agrees with finite differences;
     - Newton converges quadratically;
     - the solution is the same on 1, 2 and 8 workers;
     - Held–Suarez against the reference zonal means, recorded with gates
       on the pattern: trades, westerlies, jet latitude;
     - V8 (Ω × {0, ½, 1, 2});
     - V9 for the zonal part (the eddy torque is zero to rounding; the drag
       torque balances).
2. **Column heating**:
   - Q⁰ and Λ from the column physics at the slow state, as a function
     beside the column solve;
   - convective relaxation;
   - the Earth-like zonal solve at L4 and L5, compared with the V6 data.
3. **Azonal balance and balanced `p_s`** (§4.6) on the coarse mesh:
   - BiCGSTAB with the existing multigrid as preconditioner;
   - total mass exact;
   - the layer mass fluxes and the vertical fluxes from discrete
     continuity, so that mass closes per cell.
4. **Derived fields** (§4.1) and the climatology fields; reference mode
   starts from the balanced winds; the first L6 timing.
