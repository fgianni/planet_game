# ADR-0006 — Seasonal climate-mode steps on the integer clock

- **Status:** Accepted
- **Date:** 2026-09-29
- **Accepted:** 2026-09-29
- **Milestone:** P0 / M3 (blocks it; specification v0.4 §26.1 item 1)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §8 (seasonal resolution), §24, §26.1; Planetary Civilization Simulator — Design Record v0.9, §24.13, §28, §37
- **Related:** ADR-0001 (simulation modes; amended by this record, §4.5), ADR-0003 (integer clock, determinism), ADR-0004 (Keplerian orbit, epoch)

## 1. Context

Climate mode, the normal path of play (ADR-0001 §4.2), advances the slow
state with long steps. ADR-0001 sets that step to "1 month, seasonal cycle
resolved; adaptive to 3 months when the state is quiescent". Specification
v0.4 §8 takes the position that the seasonal cycle must be resolved
explicitly at twelve sub-steps per year, because:

1. the targets are seasonal: ice, permafrost, fire, monsoons and the
   early-warning statistics of §13.3 are driven by seasonal extremes;
2. annual-mean stepping is *biased*, not merely coarse: outgoing longwave
   goes as T⁴, saturation vapour pressure is exponential and ice is a
   threshold, so the mean of stepped seasons is not the step of the mean;
3. it is affordable within the ADR-0001 budget.

Before any M3 calibration is fitted, three things must be fixed, because
every fit of §24 is made against them:

- **What a step is on the integer clock.** One tick is 60 s (ADR-0003 §3.2).
  The default orbital period is 31,556,925.216 s = 525,948.7536 ticks, which
  neither twelve nor any integer divides. A fixed step of 43,829 ticks drifts
  0.75 tick per year against the orbit; the integer clock may not carry a
  fractional remainder.
- **Where the steps sit on the orbit.** The orbital phase is mean anomaly from
  periapsis, and a scenario may set the initial phase (ADR-0004). Step
  boundaries tied to tick 0 would put "January" at different orbital phases
  in different scenarios.
- **What forcing a step sees.** The M1 forcing is instantaneous
  top-of-atmosphere insolation. A month-long step needs its time mean, with
  the diurnal cycle and the month's motion along the orbit both averaged.

## 2. Decision drivers

- **D1 — No bias from the step.** The step must resolve the seasonal cycle.
- **D2 — No drift.** Step boundaries stay locked to the orbit for any run
  length.
- **D3 — Determinism (ADR-0003 L0).** Boundaries and forcing are pure
  functions of integers and planet parameters; no accumulated time, no
  wall clock, no dependence on thread count or pacing.
- **D4 — Scenario independence.** A season means the same orbital interval in
  every scenario.
- **D5 — Energy.** Sub-step forcing must integrate to the correct annual
  incoming energy.
- **D6 — Simplicity** for the scheduler and every consumer.

## 3. Options considered

**A. Fixed step of `round(P/12)` ticks from tick 0.** Simplest. Drifts
0.75 tick per year (about 225 ticks, 3.8 hours, over 300 years), and the
seasons sit at a scenario-dependent phase. Rejected by D2 and D4, although
the physical error is small.

**B. Boundaries at fixed mean-anomaly phases, rounded to ticks — chosen.**
Sub-step *k* begins at the first tick at or after mean anomaly 2πk/12,
measured from periapsis. Steps are 43,829 or 43,830 ticks long, never drift,
and mean the same orbital interval in every scenario.

**C. Boundaries at fixed solar-longitude (true-anomaly) phases.** Astronomical
seasons, of unequal length on an eccentric orbit. Physically meaningful, but
the unequal lengths complicate fitting, climatology and reporting for no gain
over B: B already resolves the seasonal cycle, and the forcing in each
sub-step is exact (§4.3). Rejected by D6.

**D. Annual steps carrying fitted seasonal statistics.** Rejected by D1 and by
specification §8.

## 4. Decision

### 4.1 The sub-step calendar

Let `P` be the orbital period in seconds, `M0` the mean anomaly at tick 0
(the scenario's initial orbital phase, radians), and `s = 60` seconds per
tick. Sub-step *k* (any integer, including negative) begins at

```text
t_k = P · (k/12 − M0/(2π))                 seconds, the exact crossing
begin_tick(k) = ceil(t_k / s)               the first tick at or after it
```

and spans the ticks `[begin_tick(k), begin_tick(k+1))`. Consequences:

- every sub-step is `floor(P/12s)` or `ceil(P/12s)` ticks long (43,829 or
  43,830 for the default Earth);
- twelve consecutive sub-steps span `floor(P/s)` or `ceil(P/s)` ticks, and
  `begin_tick(12n)` stays within one tick of `n·P/s` from the epoch for any
  `n`: there is no drift;
- with the default orbit (`M0 = 0`, longitude of periapsis π), sub-step 0
  begins at tick 0, which is periapsis and the northern vernal equinox
  (ADR-0004);
- `k mod 12` is the sub-step's position in the orbital year, the same in
  every scenario.

`begin_tick` is evaluated in `double` from exact integer `k` and the planet
parameters, then rounded up with `ceil`. Under ADR-0003 L0 this is
bit-reproducible; `P · (k/12)` stays exact to well below a tick for
|k| < 10⁶ (80,000 years).

A run whose start tick is not a boundary begins with the partial sub-step up
to the next boundary; it does not shift the calendar.

### 4.2 The climate-mode step

One climate-mode step is one sub-step. There is **no adaptive coarsening** in
climate mode: a quiescent state still steps monthly, because the seasonal
bias of §1 does not depend on how quiet the state is. Spin-up to equilibrium
(before a scenario starts) may take longer steps; it is not climate mode and
its result is not part of a run.

### 4.3 The forcing a sub-step sees

A sub-step is forced by the **time mean** of top-of-atmosphere insolation over
its ticks, per cell:

- the diurnal cycle is averaged analytically: the daily-mean insolation of a
  latitude for the instantaneous declination and orbital distance
  (the standard sunrise-hour-angle formula), since the default solar day is
  about thirty times shorter than a sub-step;
- the motion along the orbit is averaged by fixed Gauss–Legendre quadrature
  in time over the sub-step, with the node count fixed in code (starting at
  8) and chosen so that V3 passes;
- the result is a derived forcing field, not slow state; it is recomputed
  from the orbit and never stored in snapshots.

The instantaneous forcing of M1 is unchanged and remains what reference mode
and weather windows use.

### 4.4 The scheduler contract

The scheduler exposes the calendar and never invents time:

```cpp
struct ClimateSubstep {
    std::int64_t index;          // k
    SimulationTick begin_tick;   // inclusive
    SimulationTick end_tick;     // exclusive = begin_tick(k + 1)
    int month;                   // non-negative k mod 12, in 0..11 (also for k < 0)
};
[[nodiscard]] ClimateSubstep climate_substep_containing(SimulationTick tick,
                                                        const PlanetParameters&);
[[nodiscard]] ClimateSubstep climate_substep(std::int64_t index,
                                             const PlanetParameters&);
```

The tick sequence of a climate-mode run is the sequence of sub-step
boundaries. Report intervals, pacing, pauses and snapshot cadence
(specification §8) choose which boundaries are *shown*; they never move one.

### 4.5 Amendment to ADR-0001

ADR-0001 §4.2's climate-mode timestep becomes "one orbital sub-step (twelve
per orbital year, ADR-0006); no adaptive coarsening". The performance budget
of ADR-0001 §5 then translates into a per-sub-step budget: at 20 simulated
years per minute at L5, 240 sub-steps per minute, or 250 ms per sub-step; at 5
years per minute at L6, 1 s per sub-step, each on half the machine when the
counterfactual planet runs (specification §7.3).

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Boundaries strictly increasing; every length is `floor(P/12s)` or `ceil(P/12s)` ticks | exact, for k in [−10⁵, 10⁵] |
| V2 | No drift: `|begin_tick(12n) − begin_tick(0) − n·P/s| ≤ 1` tick | exact, for n up to 10⁴ years |
| V3 | Energy: the area-weighted, length-weighted mean of the twelve sub-step forcings over one orbital year equals the analytic annual mean `L/(16π a² √(1 − e²))` | relative error ≤ 1e-6 |
| V4 | Each sub-step's mean forcing against a high-resolution reference quadrature (instantaneous forcing, many samples per day) | relative error ≤ 1e-4 per cell |
| V5 | `climate_substep_containing` is consistent with `climate_substep` at every tick of a year, including boundaries and negative ticks | exact |
| V6 | Changing the report interval, pausing or re-running with different worker counts leaves the sub-step tick sequence unchanged | bit-identical |
| V7 | Scenario independence: a run starting mid-sub-step shares all later boundaries with a run starting at tick 0 | exact |

## 6. Consequences

**Positive.** Seasons are resolved, which specification §8 and design v0.9
§24.13 require. Boundaries never drift and mean the same orbital phase in
every scenario. The step is a pure function of integers, so replay is exact
and the scheduler stays trivial. Month-by-month climatology (ADR-0001 §4.1)
has a natural definition: the statistics of sub-step `k mod 12`.

**Negative.** Steps differ by one tick, so any per-step rate must use the
actual step length, never a constant "month". Spin-up needs its own longer
stepping. No quiescent-state speed-up is available to meet the budget; the
budget must be met at twelve steps per year.

**Risks and mitigations.**

- *A consumer assumes a constant month length* → the API returns begin and
  end ticks, and V1 tests both lengths.
- *Slow rotators* (a solar day comparable to a sub-step) break the analytic
  diurnal average → out of scope until a scenario needs one; the forcing code
  asserts that the solar day is shorter than a tenth of a sub-step.
- *The budget is not met at twelve steps per year* → measured from M3
  (ADR-0001 §5); the remedy is a faster step, not coarser seasons.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M2 (closing) | Sub-step calendar and scheduler contract (§4.1, §4.4), with the ADR-0001 mode enum; V1, V2, V5, V7 |
| M3 | Sub-step mean forcing (§4.3); climate-mode surface energy stepped monthly; V3, V4, V6; first per-sub-step timing |
| M4 onwards | Seasonal snow and ice, climatology per `k mod 12` |

## 8. Open questions

- Should the calendar also carry astronomical season markers (equinoxes and
  solstices as tick values) for reporting? Current position: derive them on
  demand from the orbit for presentation; they are not step boundaries.
- Does a weather window start and end on sub-step boundaries? Current
  position: no, it runs on its own reference-mode ticks and hands back
  accumulated fluxes for the sub-step it overlaps (ADR-0001 §4.3).
