# Investigation INV-03 — L6 coupled-transport startup

- **Milestone:** P0, following INV-02 and before M9
- **Status:** ADR-0024 §9 accepted and implemented (2026-10-21): the
  diffusive fallback is in the earth_like preset (R_f = 100 W/m²). Its L4
  radiation pair is refitted below. L4, L5 and L6 have now passed their
  measured calibration, budget and performance gates on the refitted pair.
  Snapshot restart through a fallback also passes at L4 and, as of
  2026-10-22, on the refitted L6 preset (recorded below).
- **Scope:** investigate a numerical method for the L6 cold-start transport failure
- **Predecessor:** [INV-02](INV-02-moistening-refit-and-parity.md), which records the refit and V8 result
- **Governing records:** ADR-0024 §§1–5; ADR-0009 §9; development specification §20.12

## Finding

The failure is not isolated to `convective_moistening`. With the INV-02
refitted coefficients, an L6 first-month run forced to use the earlier
non-moistening Betts–Miller response still reaches the 30-iteration limit:
its transport consistency residual is **2,510.72 W/m²**, and the run fails
the column-convergence gate. Restoring M8-05's former radiation
coefficients for that diagnostic lowers the residual to **1,609.97 W/m²**,
but still leaves an unconverged column. The usual full-moistening startup can finish an
unconverged transport source that drives a bottom layer to about **4.9 K**.

This extends ADR-0024's original diagnosis: the L6 initial response is
strongly non-monotone and the Newton iteration does not reach a consistent
source. The refit's startup path remains unresolved even when the new
moistening response is disabled. The new tests were diagnostics only; no
acceptance gate was weakened.

## Prototypes tested

| Prototype | Result |
|---|---|
| Scale the complete transport operator from zero to full strength | With 64 equal stages, stages 1–2 converge but stage 3 stalls at 4.86 W/m² near 4.7% strength. With 256 stages, stage 9 converges but stage 10 stalls at 24.29 W/m² near 3.9% strength. The final physical source still makes a column non-positive. |
| Interpolate the column response from convective moistening off to on | The no-moistening starting response itself stalls at 2,510.71 W/m² after 30 Newton steps, before continuation can reach the target response. |
| Damped residual-gradient fallback after Newton line-search failure and after the Newton cap | It lowered the first-month residual from about 2,511 to 797 W/m², but columns remained unconverged; later startup residuals were 1,039 and 365 W/m². Runtime increased substantially. |

These results reject those prototypes as fixes. The experiments were
removed; the accepted 30-step Newton method and physical source are
unchanged.

## Next decision

The existing evidence does not yet establish whether the intended physical
equation has a reachable L6 startup root or whether a safeguarded solver
must select a different root branch. Credible next investigations are:

1. Prototype a damped least-squares solve using the full normal operator
   `Jᵀ A J + μI`, with conservation and determinism preserved, then measure
   whether the final unregularized residual reaches the existing gate.
2. Investigate a warm-start continuation from a previously converged L6
   state, which changes startup-path assumptions and can select a different
   branch.

Either successful method would change the coupled transport's nonlinear
solve behavior and needs an ADR review before it replaces the accepted
solver. M9 remains outside this investigation.

## The diffusive fallback (prototype, 2026-10-21)

**Idea.** The first-month crash is not the unconverged Newton itself:
ADR-0024 records M8-05's L6 first month ending near 1 900 W/m² and
surviving. It is the application of an unconverged source, which drives a
column to about 5 K. ADR-0011 §17.4 already gives a failed circulation
month ADR-0009's diffusive transport. The prototype gives it to a month
whose coupled solve ends above a residual threshold, so the start warms
itself up with no regridding or second start.

**Code** (off by default; the two-year L3 state hash is unchanged,
`49c729e4dea72e2b`):
- `SurfaceEnergyParameters::transport_fallback_residual_W_m2` (0: off);
- `SurfaceEnergyDiagnostics::transport_fell_back`;
- `planet_cli thermal --transport-fallback W`.

**L6 with the refitted preset, threshold 100 W/m², ten years from the
initial state** (12 workers, the machine shared):

| Year | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Fallback months | 4 | 3 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 1 |
| Months at the 30-iteration cap | 4 | 4 | 1 | 1 | 0 | 0 | 1 | 0 | 0 | 0 |
| Surface (K) | 304.5 | 300.6 | 297.7 | 295.7 | 294.4 | 293.5 | 292.9 | 292.4 | 292.1 | 293.0 |
| Wall time (s) | 96 | 74 | 32 | 29 | 15 | 16 | 30 | 17 | 16 | 18 |

- **L6 runs.** Ten years complete from the cold start, the energy closes
  every month (about 1e-5 of its gate), and no circulation month fails.
  The year-1 L6 `thermal` run that fails today completes (288 K fit
  aside: 306.7 K in year 1, the hot start).
- **A safety net, not a cure.** Most fallbacks are in the first two years,
  as the planet cools from its hot initial state. Isolated months still
  fail later (years 7 and 10), usually when the line search stops after
  5–7 iterations at a kink in the columns' response, not at the cap.
- **Settled months converge on their own** in 4–6 iterations, to
  1e-5–1e-4 W/m².
- **V11 not measured.** About 16 s per settled year here, sharing the
  machine; V11's L6 limit (2 025 s for 250 years) needs about 8 s.

**For the decision.** Accepting the fallback changes the coupled
transport's behaviour in its failing months (diffusion of T_m in place
of the circulation's transport, as in §17.4's failed months), so it needs
an ADR amendment (ADR-0011 §17.4 and ADR-0024). Then: a clean V11 run at
L5 and L6, and the threshold chosen. The kinks that stop the line search
early remain a separate, deeper question (the columns' non-monotone
response, ADR-0024 §1).

## ADR-0024 §9, implemented (2026-10-21)

**Code:**
- **The fallback subtracts the eddies' latent heat E.**
  `solve_implicit_transport` takes an optional per-node E, solving
  h = K ∇² T̄(h) − E and applying K ∇² T̄ − E, which still sums to zero.
  The surface step keeps E for the fallback; the coupled solve used to
  move it away.
- **On in the preset:**
  `SurfaceEnergyParameters::transport_fallback_residual_W_m2 = 100` in
  earth_like.
- **The long test** (`test_convective_moistening_run`) reports each
  fallback month and each month with unconverged columns.

**Validation (ADR-0024 §9.4):**
- **V1:** the two-year L3 run ends with the same state hash,
  `49c729e4dea72e2b`. No L3 month reaches 100 W/m², so L3 and its golden
  data are unchanged.
- **V4** (`test_heat_transport`, `check_latent`): with a zero-sum E the
  solve converges to 9.7e-7 W/m², its source sums to zero (1e-17
  relative), and E changes it by up to 2.1 W/m².
- **V5:** six L4 months on one and four workers, through a fallback month
  (month 2, 106.6 W/m²), end with the same state hash
  (`c49db7ff261a5cd8`).
- **Prior-pair L6 result:** with INV-02's coefficients, L6 completed in
  1 516.6 s and passed energy/water budgets with no failed circulation
  month. One column-convergence assertion failed in month 0 with a
  7.8e-11 K correction; this is superseded by the refitted-pair run below.
- **V2/V11 after refit:** L6, 250 years from the initial state, 16 workers
  (`test_convective_moistening_run 6 250 16`): **1 061.1 s**, within the
  2 025 s limit; energy 1.39e-5 and water 3.23e-4 of their gates; zero
  unconverged columns and no failed circulation months. There are 10
  fallback months and 8 months at the Newton cap. The final-year mean is
  288.473 K, cloud cover 0.582, and cloud effects −46.29/+25.54 W/m².
- **V5 worker determinism after refit:** two L6 years on one and four
  workers both pass through the same eight fallback months and end at
  state hash `479957516275377720`. The L4 fallback case was also restarted
  from a PSNAP written at the start of month 2: the loaded climate scheduler
  reproduced the fallback and reached the uninterrupted six-month hash with
  four workers versus one. This exercises the saved slow state and tick
  across a fallback without adding a `PlanetRun` resume API; PRUN replay still
  starts from the scenario's initial state. The restart uses INV-02's L4 pair,
  which has the month-2 fallback; the refitted L4 pair has no fallback. A
  snapshot restart through a fallback on the refitted L6 path was subsequently
  verified on 2026-10-22, as recorded below.
- **L5/250 after refit:** **308.3 s**, within the 607.5 s limit; energy
  1.11e-5 and water 2.29e-4 of their gates; zero unconverged columns and
  no failed circulation months. There are 3 fallback months and 5 capped
  months. The final-year mean is 288.377 K.
- **V3:** L4's 150-year fit gives 288.086 K with the fallback (one
  fallback month, month 2), against INV-02's 288.005 K. That is outside
  the long test's ±0.05 K. The calibration CLI's surface-only spin-up
  does not run `AtmosphereDynamics`, so the refit was measured with the
  same `PlanetRun` path as the acceptance test. At τ_d = 1.111 and
  κ_v = 0.005555 m²/kg, the 150-year L4 run gives 287.984 K; energy is
  1.15e-5 and water 1.69e-4 of their gates. It has three capped early
  transport months but no fallback month. V9 cloud targets still pass.
  The refitted pair preserves the INV-02 dry/vapour ratio.


**A continuous fallback, tried and not kept (2026-10-22).** Blending the
two sources with w = smoothstep((R − R_f/2) / (2 R_f − R_f/2)) does not
make L4's fit smooth. The jump is in R itself. Across scales of INV-02's
pair, L4's first months end at 52/37/80 W/m² (scale 1.0), 41/37/22
(0.9978) and 38/253 (0.9970): the cold start's Newton converges at one
scale and fails at the next. With the fallback off, the 150-year mean is
smooth (288.005 / 287.919 / 287.899 K). One full-diffusion month at the
start moves it by up to about 0.2 K, beyond the ±0.05 K gate. So the
refitted pair passes L4 because its cold start has no fallback month, and
a later refit can lose that. If it does, the option is a fallback
triggered by the columns failing instead of by R: L4 would then never
fall back.

## Refitted L6 snapshot restart (2026-10-22)

ADR-0024 §9.4 V5 now passes on the current Earth-like preset at L6
(seed 1, τ_d = 1.111, κ_v = 0.005555 m²/kg, R_f = 100 W/m²).
The test writes a PSNAP at tick **175317**, immediately before zero-based
month **4**, after evolving the uninterrupted run with one worker. It
rebuilds the terrain and climate scheduler, loads the slow state and saved
tick, and continues with four workers. The loaded hash is identical to the
saved hash (`11623466116100483792`). Every subsequent monthly checkpoint
matches, including three fallback months and the next non-fallback month:

| Month (zero-based) | End tick | Fallback in both runs | State hash in both runs |
|---|---:|---|---:|
| 4 | 219146 | yes | 15066035426067663690 |
| 5 | 262975 | yes | 11692907357000182108 |
| 6 | 306804 | yes | 2869199173728343433 |
| 7 | 350633 | no | 16000283396048090233 |

- Maximum energy residual/gate: **1.22979e-5**.
- Maximum water residual/gate: **1.75347e-4**.
- Zero unconverged columns and zero failed circulation months.
- Runtime: **377.5 s**, GCC 11.4 Release on the development machine;
  this is a restart-check measurement, not a new performance gate.
- The existing L4 restart, L3 moistening/determinism, snapshot-file and
  heat-transport checks also pass.

Reproduce the L6 check with:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPLANETSIM_BUILD_TESTS=ON
cmake --build build-release --parallel --target test_convective_moistening_run
./build-release/tests/test_convective_moistening_run --snapshot-restart-l6
```

This explicit long-run command complements the default test's L4 case.
It searches months 1–11 for a fallback, saving before each candidate,
and fails if none occurs. This permits build-dependent startup paths
under ADR-0003 while requiring an evolved-state restart through an actual
fallback. It checks the fallback and three following months, including
per-step energy/water gates, column convergence, ticks, fallback decisions
and state hashes. No physics, tolerance, preset or PSNAP format changed.
The restart still uses the climate scheduler directly; a general
`PlanetRun` resume API and PRUN replay from a snapshot are not provided.
V8 temperature parity remains unresolved, and M9 has not started.
