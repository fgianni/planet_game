# Investigation INV-03 — L6 coupled-transport startup

- **Milestone:** P0, following INV-02 and before M9
- **Status:** prototype methods did not meet the L6 convergence gate (2026-10-20)
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
