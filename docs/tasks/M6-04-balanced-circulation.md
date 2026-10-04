# Task M6-04 — Climate-mode balanced circulation

- **Milestone:** P0 / M6 (fourth task; M6-01 to M6-03 are complete)
- **Status:** in progress — step A done; ADR-0011 §14 accepted (2026-10-04); step B under way
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

## Plan for steps B–D

1. **`ZonalCirculation`** (`sim/planet/dynamics/zonal_circulation.{hpp,cpp}`):
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
