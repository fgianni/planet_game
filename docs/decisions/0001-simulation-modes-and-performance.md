# ADR 0001: Simulation modes and performance budget

- Status: Accepted
- Date: 2026-09-24
- Milestone: P0 / M1 architecture

## Context

PlanetSim must represent weather processes that evolve over minutes while a
normal game spans centuries. Running the future explicit weather solver for
every minute of a multi-century history is not computationally viable. A
single vaguely defined "accelerated" solver would also make it difficult to
tell whether speedups preserve the physical budgets and climate response of
the validated model.

The v0.4 design therefore makes extreme time acceleration an architectural
constraint rather than a later optimization.

## Decision

PlanetSim will distinguish three simulation modes:

1. **Reference mode** uses explicit processes, conservative small timesteps,
   and deterministic execution for validation, CI, and calibration.
2. **Climate mode** is the normal long-history gameplay path. It advances slow
   state with long or implicit steps and represents unresolved weather with
   statistics fitted against reference-mode experiments. Energy, water, and
   carbon budgets remain explicit.
3. **Weather windows** run the explicit solver for a bounded region and time
   span, seeded from climate-mode state, for hazards, forecasts, and close
   inspection.

All modes use the same physical clock, planetary/orbital coordinates, units,
and conservation diagnostics. Presentation snapshot cadence is independent of
solver cadence. M1 orbit and solar geometry are analytic functions of physical
time and therefore do not vary by simulation mode.

Starting at M3, reproducible benchmarks will track these v0.4 budgets on the
target desktop:

- climate mode: at least 20 simulated years per wall-clock minute at the L5
  development resolution and at least 5 at the L6 shipped resolution;
- a 250-year headless scenario completes in CI in under ten minutes;
- a regional weather window runs a season in real time or faster.

## Consequences

- The future scheduler must name the active mode rather than infer it from a
  large timestep.
- Accelerated parameterizations require comparison tests against reference
  runs and cannot silently replace reference physics.
- Snapshots and rendering cannot assume one solver step per visual frame.
- M1 does not implement a scheduler or either weather path; it establishes
  mode-independent time and forcing inputs that those paths will share.

## Alternatives considered

### One explicit solver at every speed

This is conceptually simple but cannot meet the century-scale performance
budget.

### One accelerated solver with no reference path

This is faster to implement but removes the baseline needed to detect
calibration drift and conservation defects.
