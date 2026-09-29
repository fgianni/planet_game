# Task M2-04 — Simulation modes, scheduler skeleton and sub-step calendar

- **Milestone:** P0 / M2 (closing item). ADR-0001 §8 requires "a scheduler
  skeleton with mode enum" by the end of M2; the conformance audit still marks
  it NOT FOUND (row C3).
- **Requested:** 2026-09-29
- **Scope:** infrastructure only: the mode enum, the ADR-0006 sub-step calendar
  and a deterministic scheduler that dispatches registered processes. No
  physics, no sub-step mean forcing (that is M3), no weather windows.
- **Governing decisions:** ADR-0001 §4.2 (as amended), §4.4, §4.5, §8;
  ADR-0003 §3.1–3.2; ADR-0006 §4.1, §4.2, §4.4, §5; specification
  `docs/DEVELOPMENT_SPEC_v0_4.md` §2, §8

## 1. Read first

1. `AGENTS.md`
2. `docs/decisions/0006-seasonal-climate-steps.md` in full
3. `docs/decisions/0001-time-acceleration.md` §4.2, §4.4, §4.5, §8
4. `docs/DEVELOPMENT_SPEC_v0_4.md` §8 (multi-rate simulation, pacing is
   presentation, state-only mode changes)
5. `sim/core/scheduler/simulation_clock.hpp`,
   `sim/core/scheduler/deterministic_executor.hpp`,
   `sim/planet/planet_parameters.hpp`

## 2. Current code (as of commit `e0d3c62`)

- `SimulationClock` holds a signed 64-bit tick (60 s per tick) with
  `advance_ticks`, `set_tick` and `reset`. Nothing schedules steps: the CLI
  evaluates forcing at a requested tick, and the Godot adapter advances its
  own clock by wall-clock-derived tick counts for presentation.
- `PlanetParameters` has `orbital_period_s` (default 31,556,925.216 s) and
  `initial_orbital_phase_rad` (mean anomaly at tick 0, default 0).
- There is no simulation-mode type and no process registry.

## 3. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Core stays domain-agnostic** (specification §2: `Core -X-> Domain`).
   The calendar lives in `sim/core/scheduler/` and takes a plain value type:

   ```cpp
   struct OrbitalCalendar {
       double orbital_period_s;            // positive, finite
       double initial_orbital_phase_rad;   // mean anomaly at tick 0, finite
   };
   ```

   `sim/planet/orbit/` adds `make_orbital_calendar(const PlanetParameters&)`
   and the two ADR-0006 §4.4 functions taking `PlanetParameters`, which
   forward to the core ones.
2. **Calendar arithmetic is exactly ADR-0006 §4.1:**
   `t_k = P·(k/12 − M0/(2π))`, `begin_tick(k) = ceil(t_k / 60)`, evaluated in
   `double` from the exact integer `k`. `month` is the non-negative remainder
   of `k` modulo 12, also for negative `k`. `climate_substep_containing(tick)`
   must agree with `climate_substep(k)` at every tick, including boundaries
   and negative ticks; compute a first estimate of `k` from the tick and
   correct it by at most one step either way, rather than trusting a
   floating-point division at a boundary.
3. **Modes.** `enum class SimulationMode : std::uint8_t { reference, climate,
   weather_window };` with a `simulation_mode_name` function. A weather window
   is not implemented: requesting one throws `std::logic_error` naming
   ADR-0001 §8 (M10–M12).
4. **One step, one mode.** `Scheduler::step()` advances the clock by exactly
   one step of the current mode:
   - climate mode: from the current tick to the next sub-step boundary (a
     partial sub-step when the current tick is not on a boundary, ADR-0006
     §4.1);
   - reference mode: `reference_step_ticks` (default 10, the ten-minute
     atmospheric step of ADR-0001 §4.2).

   It returns a `StepContext` (mode, begin and end tick, step index, and the
   `ClimateSubstep` in climate mode) and calls every registered process for
   that mode, in registration order, before advancing the clock.
5. **Mode changes are requested, recorded and take effect at the next step.**
   `request_mode(mode)` never splits a step. Each effective change is appended
   to a mode log `{tick, mode}`, which M3's run manifest will carry
   (ADR-0003 §3.3). The scheduler never decides a mode change itself; callers
   do, from state (specification §8).
6. **Pacing cannot change the tick sequence.** `run_until(target_tick)`
   executes whole steps while the step would end at or before the target and
   never splits a step to land on it. Therefore running to A and then to B
   produces the same steps as running straight to B.
7. **Cadence in reference mode.** A process may declare `cadence_ticks`, a
   positive multiple of `reference_step_ticks`, and runs on reference steps
   whose begin tick is a multiple of it (for example the ocean every hour and
   the land every hour, ADR-0001 §4.2). Climate-mode processes run on every
   sub-step. Cadence is fixed at registration.
8. **No threads in the scheduler.** It is a serial dispatcher; processes use
   the deterministic block executor internally when they need parallelism.
9. **Leave the Godot adapter's presentation clock alone.** It is a
   presentation path and is out of scope.

## 4. Implementation steps

Small, reviewable commits; build and run `ctest` after each.

1. `sim/core/scheduler/orbital_calendar.{hpp,cpp}`: `OrbitalCalendar`,
   `ClimateSubstep`, `climate_substep(k)`, `climate_substep_containing(tick)`,
   input validation, and the planet-side overloads in `sim/planet/orbit/`.
2. `sim/core/scheduler/simulation_mode.hpp` and
   `sim/core/scheduler/scheduler.{hpp,cpp}`: the mode enum, `StepContext`,
   process registration with modes and cadence, `step`, `request_mode`,
   `run_until` and the mode log.
3. Tests (§5).
4. CLI: `planet_cli calendar [--year N] [--from-tick T]` printing the twelve
   sub-steps of an orbital year (index, month, begin, end, length in ticks)
   for the default Earth, plus a ctest smoke test.
5. Documentation: ADR-0006 implementation record (new §9), ADR-0001 §8 M0–M2
   row status, audit rows C3 and G4, README (calendar and scheduler), the
   task index and `AGENTS.md`.

## 5. Acceptance criteria

| # | Check | Gate |
|---|---|---|
| C1 | ADR-0006 V1: boundaries strictly increasing; every length is 43,829 or 43,830 ticks for the default orbit, for k in [−10⁵, 10⁵] | exact |
| C2 | ADR-0006 V2: `|begin_tick(12n) − begin_tick(0) − n·P/60| ≤ 1` for n up to 10⁴ | exact |
| C3 | ADR-0006 V5: `climate_substep_containing` agrees with `climate_substep` at every tick of one full year and of the year around tick 0 (negative ticks), including every boundary | exact |
| C4 | ADR-0006 V7: a run started mid-sub-step shares every later boundary with a run started at tick 0; the first step is the partial remainder | exact |
| C5 | Default orbit: sub-step 0 begins at tick 0; a non-zero initial phase shifts every boundary consistently | exact |
| C6 | Scheduler: climate steps coincide with calendar sub-steps; reference steps are 10 ticks; processes run in registration order, only in their modes, at their cadence | exact |
| C7 | Mode changes take effect at the next step, never split one, and are logged with their tick; requesting a weather window throws | exact |
| C8 | Pacing independence (part of ADR-0006 V6): `run_until` in several chunks and in one call produce identical step sequences and final ticks, in both modes and across a mode change | bit-identical |
| C9 | Invalid inputs (non-positive or non-finite period, non-finite phase, non-positive reference step, cadence not a multiple of it) are rejected with `std::invalid_argument` | exact |
| C10 | The full suite stays green in CI (GCC and Clang, Debug and Release, ASan+UBSan), with warnings as errors, the floating-point policy check and the field-registry check | pass |

## 6. Out of scope

- Sub-step mean insolation (ADR-0006 §4.3, V3, V4) and any physics process:
  M3.
- The run manifest, command log and state hashes (ADR-0003 §3.3): M3. The
  mode log is their seed only.
- Weather windows (M10–M12), the projection ensemble, the counterfactual
  instance.
- Any change to the Godot adapter.

## 7. Environment notes

- Configure and test: `cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON`,
  `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- On Linux 6.x kernels with 32-bit mmap randomisation, sanitizer binaries
  built by Clang 14–17 hang at start-up; run them as
  `setarch -R ctest --test-dir build-sanitize`. CI lowers `vm.mmap_rnd_bits`.
- Work alone in the checkout: do not run while another agent is editing it.
- Stop and ask before adding a dependency, changing an accepted ADR's
  decision, changing a §3 decision, or weakening an existing test.

## 8. Report when done

- Files added and changed, per step.
- The result of each acceptance item C1–C10, and the `planet_cli calendar`
  output for year 0.
- Anything not met, and why; any ambiguity found in ADR-0001, ADR-0006 or the
  specification.

## 9. Completion note (2026-09-29)

Implemented directly rather than by Codex, in commits `11c08d9` (calendar),
`2100b86` (modes and scheduler) and the `planet_cli calendar` commit. C1–C10
pass; results and the two refinements (reference steps on a fixed grid, the
core `OrbitalCalendar` type) are recorded in ADR-0006 §9.

