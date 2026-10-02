# Task M5-03 — Column radiation, convection and surface coupling

- **Milestone:** P0 / M5 (third task; M5-01 and M5-02 are complete)
- **Scope:** ADR-0010 §4.4–4.5:
  - the coupled implicit column: grey two-stream longwave through N
    layers, sensible exchange, Newton over the layers with the tiles as
    its surface, and convective adjustment;
  - the transport acting on the columns' mean θ_c;
  - the budget and diagnostics, V2–V5 and V7, and a first L6 timing.

  The `earth_like` preset keeps the grey layer until the M5-04 refit, so
  the calibrated tests do not move twice.
- **Governing decisions:** ADR-0010 §3.2–3.4, §4.4–4.5, V2–V5, V7;
  ADR-0008 §10 (amended during this task); ADR-0009 §4.3, §12

## 1. Decisions made for this task

1. **The column solver** (`sim/planet/atmosphere/atmosphere_column.hpp`)
   is header-only and templated on its surface, so the per-cell callback
   allocates nothing.
   - It sees the surface only through a linearised exchange: the longwave
     sent up and the sensible heat, as functions of the downward longwave
     and the surface air temperature. The planet's tiles and the tests'
     analytic surfaces plug into the same solver.
   - The Jacobian is exact. Steps are backtracked, with a bounded number
     of non-monotone steps across kinks such as snow starting to melt.
2. **Transport.** The diffused temperature is the column's mean θ_c, and
   a source h warms the layers as h π_k / Σπ, which is a uniform θ_c
   shift. The slope the outer Newton uses includes the convective
   adjustment, which is linear for a fixed set of mixed pools. Without
   that, 90 % of columns gave the outer Newton a wrong Jacobian and it
   needed up to 30 iterations.
3. **Exact base exchange.** The float land and ocean fractions can sum to
   slightly more than one. The uncovered remainder is not clamped at
   zero, so the downward longwave given by the column equals what the
   tiles receive. Clamping it created about 2e-5 W/m² per land cell,
   which took the closure ratio to 0.25–0.77.
4. **Unconverged columns** are finished from their last fluxes,
   T_k = T*_k − F_k Δt / C, which keeps the budget exact. They are
   counted and their correction is reported. With the amended sea ice
   none occurs. Converged columns are not corrected: on a very long step
   C/Δt is tiny and the correction would amplify rounding.
5. **ADR-0008 §10 (floes and leads).** The zero-layer sea ice had a jump
   at complete melt (273.15 K surface to 271.35 K open water) and a fold
   for thin ice. Under a column with heat capacity those left some cells
   without an exact solution, and the transport turned their jitter into
   100–1,700 W/m² inconsistencies. The choice was put on 2026-10-01 and
   option 1 was taken: thin ice is floes of h_r = 0.5 m over a cover
   c = m / (ρ h_r), with leads at T_f. The tile's response is now
   continuous and its growth residual monotone.
   - ADR-0008 V3's Stefan test now starts at full cover (0.6 m). Thinner
     ice has leads, which the test's pinning exchange freezes at once.
   - All other M4 tests pass unchanged.
   - The grey `earth_like` mean at L4 moves from 288.17 to 288.13 K.

## 2. Results

- **V2.** One grey layer over a black surface reproduces ADR-0007's
  `(1 − g/2)` closed form to 3e-14.
- **V3.** N black layers (N = 1…5) reproduce the ladder
  T⁴ = j T_e⁴ to 3e-14.
- **V4.** Convective adjustment conserves Σ T to 8e-16, leaves θ_c
  non-decreasing, is idempotent, and leaves stable columns untouched.
- **Slope.** The θ_c slope agrees with a finite difference to 1e-6.
- **V5** (`test_atmosphere_planet`, L3, τ₀ = 3):
  - every climate step of three years (N = 3) and one year (N = 5)
    closes within 1.4e-5 of the gate;
  - a day of ten-minute reference steps closes within 0.03 of the gate;
  - water closes and no column fails to converge. Residuals are ≤ 1e-10
    W/m² on monthly steps and at the rounding floor (about 5e-9 W/m²) on
    ten-minute steps.
- **V7.** A year on 1 and 8 workers is bit-identical.
- **Structure.** The outgoing longwave is below the surface's, and the
  layers cool upward. With five layers the top layer sits up to 1.3 K
  above the one below: radiative equilibrium above the convective layers,
  a first tropopause.
- **L4, 30 years** (τ₀ = 3, uncalibrated, 8 workers):
  - layers at 272, 248 and 225 K; outgoing longwave 275 W/m², surface up
    466, down 310, sensible 119 W/m²;
  - convection in 89 % of the area;
  - every column converges in at most 4 iterations;
  - 11.9 ms per sub-step, against 14.7 for the grey path.
- **First L6 timing** (5-year spin-up from rest, 4 workers): 327 ms per
  sub-step against 330 for the grey path (L5: 105 against 88). The
  atmosphere costs no more than the grey layer. The ADR-0001 gate itself
  is measured in M5-04, after the refit.

## 3. Left to M5-04

- The τ₀ and D refit, and switching `earth_like` to the atmosphere.
- Recording N in the run manifest.
- The plateau experiment (V6) and the performance gates (V10).
