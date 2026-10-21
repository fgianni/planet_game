# Investigation INV-03 — L6 coupled-transport startup

- **Milestone:** P0, following INV-02 and before M9
- **Status:** a fallback prototype runs L6 (2026-10-21); for a decision.
  The 2026-10-20 solver prototypes did not meet the convergence gate
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

