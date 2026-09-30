# ADR-0001 — Time acceleration, simulation modes and the performance budget

- **Status:** Accepted
- **Date:** 2026-09-23
- **Accepted:** 2026-09-24
- **Amended:** 2026-09-28 — §8 milestone mapping aligned with `docs/DEVELOPMENT_SPEC_v0_2.md` §13
- **Amended:** 2026-09-29 — §4.2 climate-mode step fixed by [ADR-0006](0006-seasonal-climate-steps.md): twelve orbital sub-steps per year, no adaptive coarsening
- **Amended:** 2026-09-30 — §11: the budget is gated in CI as a rate scaled by a measured runner factor; the 250-year scenario is measured on the target machine
- **Context document:** Planetary Civilization Simulator — Design Record v0.4, §28
- **Supersedes / superseded by:** —
- **Related:** ADR-0002 (mesh topology, resolution policy, field layout), ADR-0003 (determinism scope, snapshot schema, migration)

## 1. Context

The design requires two things that cannot both be met by one solver:

- weather that evolves in **minutes to hours** and is felt as hazards (storms, heatwaves, floods, droughts);
- histories that span **one to three centuries**, replayed, forked and compared.

The cost of doing this explicitly is decisive. At the shipped reference resolution (level 6, ~40,962 surface cells, 5 atmospheric layers ≈ 205,000 atmospheric cells) with a 10-minute atmospheric step:

| Quantity | Value |
|---|---|
| Steps per simulated century | ≈ 5.3 × 10⁶ |
| Atmospheric cell-updates per century | ≈ 1.1 × 10¹² |
| At an optimistic 20 ns per cell-update | ≈ 6 hours of compute per simulated century |

A player expects to cross a century in minutes, not hours, and CI must cross 250 years in under ten minutes. Explicit weather is therefore **three to four orders of magnitude too expensive** to be the normal mode of play. This is a structural constraint on the architecture, not a tuning problem, and it must be settled before the field layout, the scheduler and the snapshot API are written.

## 2. Decision drivers

- **D1 — Playability.** Centuries must pass at a speed a player tolerates, with the civilization loop interleaved.
- **D2 — Physical credibility.** Whatever runs fast must still conserve energy, water and carbon, and must reproduce the acceptance targets of Design Record §30.
- **D3 — Architectural stability.** Consumers (civilization systems, observation layer, presentation) must not care which mode produced the state.
- **D4 — Testability and replay.** Results must be reproducible for a given build, platform and seed, independently of when the player zoomed in or paused.
- **D5 — Memory.** Snapshots and history forks must stay small enough to store many of them.

## 3. Options considered

**A. One explicit solver, larger timesteps.** Rejected. Stability aside, the physics is meaningless at multi-year steps: convection, fronts and storms are not slow processes integrated coarsely, they are transient by nature.

**B. One implicit climate solver, no weather at all.** Cheap and stable, and close to what the current prototype does. Rejected as the sole path because hazards are where the player meets the climate: without storms, droughts and heatwaves as events, the causal chain that the whole design rests on is invisible.

**C. Two-tier: a validated reference solver, an accelerated climate mode for normal play, and bounded explicit weather windows on demand.** Chosen. It keeps explicit physics where it is affordable and meaningful, and keeps the slow state — which is what history, forks and the civilization actually consume — always authoritative.

**D. Learned surrogate (ML emulator trained on reference runs) in place of climate mode.** Premature, but not excluded: it fits the same contract as option C's climate mode and could replace its parameterisations later without disturbing consumers. Any such work must be validated against the same acceptance targets.

## 4. Decision

### 4.1 State partition

The planetary state is split by how it is integrated, not by subsystem:

| Partition | Contents | Lifetime |
|---|---|---|
| **Slow state `S`** (authoritative, always integrated) | surface and soil temperature, soil moisture, snow and ice mass, vegetation and carbon pools, ocean layer temperature and salinity, ocean circulation strength, atmospheric composition, sea level | persistent; the unit of snapshots, forks and history |
| **Fast state `F`** (weather) | 3-D winds, humidity, cloud water, instantaneous precipitation | exists only inside reference runs and weather windows; **never** required to reconstruct `S` |
| **Climatology `X`** (statistics) | per cell and per month: means and variances of temperature, rainfall and wind, plus rates for heatwave, drought, flood and storm-strike | derived; regenerated from `S` and fitted coefficients |

**Rule:** consumers read `S` and `X`. Only the presentation layer and hazard resolution read `F`, and only when a window is open.

*Implementation note (2026-09-28):* `PlanetState` now exposes `SlowState`,
`FastState`, and `Climatology`. The initial slow partition contains the M2
hypsometry layers and global sea level; the currently empty fast partition is
lazily allocated and releasable; forcing remains derived and outside slow
state. Persistent snapshots serialize only registered slow fields. Physical
weather/climatology fields remain future work.

*Implementation note (2026-09-29, task M2-04):* `SimulationMode` (reference,
climate, weather window) and a serial, deterministic `Scheduler` complete the
M0–M2 row of §8 together with the partition above. Climate steps are the
ADR-0006 sub-steps; reference steps are ten ticks on a fixed grid; mode
changes are requested, take effect at the next step and are logged; weather
windows throw until M10–M12. See ADR-0006 §9.

### 4.2 The three modes

| Mode | Timestep | Purpose | When it runs |
|---|---|---|---|
| **Reference** | atmosphere 10 min (CFL-bound), ocean 1–6 h, land 1 h | ground truth for calibration, conservation tests, CI | offline, headless, short runs (days to a few years) |
| **Climate** | one orbital sub-step, twelve per orbital year, seasonal cycle resolved; no adaptive coarsening (*amended 2026-09-29, ADR-0006*; was: 1 month, adaptive to 3 months when quiescent) | the normal mode of play: decades to centuries | whenever the player is not inside a weather window |
| **Weather window** | as reference, over a bounded region and period (hours to one season) | hazards as they happen, zoom-in, in-game forecasts | on demand: player zoom, hazard trigger, forecast request |

Climate mode integrates `S` with long implicit steps and derives `X` from the resolved circulation, moisture and energy budgets. Weather appears to the player as statistics and as discrete events sampled from `X` — a storm track, a drought, a heat spell — rather than as resolved fields.

All modes use the same physical clock, planetary and orbital coordinates,
units, and conservation diagnostics. M1 orbit and solar geometry are analytic
functions of physical time and do not vary by mode. M1 establishes these
mode-independent inputs; it does not implement the future mode scheduler,
reference solver, climate solver, or weather-window path.

### 4.3 Coupling contract between modes

- **The slow state is authoritative.** A weather window is seeded by downscaling `S` and `X`; it returns only **accumulated fluxes** (energy, water, momentum at the surface, and carbon) over its period.
- **No double counting.** While a window is open for a region, climate mode does not also apply its statistical fluxes there. The window's accumulated fluxes replace them, and the difference is recorded as a diagnostic.
- **No discontinuity.** Closing a window may not move any field in `S` by more than the tolerance in §6, V3. A larger jump is a bug, not a rounding artifact.
- **Statistics are fitted, not invented.** Every parameterisation in climate mode that stands in for resolved weather (rain-out efficiency, extreme rates, storm genesis) carries coefficients fitted against reference runs, versioned with the fit that produced them (Design Record §30).

### 4.4 Scheduler

A multi-rate scheduler drives all of it:

```
player speed setting  ──►  target simulated years / second
                               │
                    ┌──────────┴──────────┐
                    │  frame budget (ms)  │
                    └──────────┬──────────┘
                               ▼
        ┌─── climate mode: N steps of 1 month ────────────────┐
        │      land/surface → ocean mixed layer → circulation │
        │      → composition → derived climatology X          │
        └─────────────────────┬───────────────────────────────┘
                              │ hazard trigger / player zoom
                              ▼
              weather window (bounded region, own step)
                              │ accumulated fluxes only
                              ▼
                        slow state S
                              │  double-buffered
                              ▼
                     snapshot  ──►  Godot (interpolated)
```

- The simulation runs on worker threads; the render thread never blocks on it.
- Snapshot cadence is independent of solver cadence; presentation interpolates.
- Sub-stepping is adaptive but **bounded and deterministic**: the step sequence is a function of state and seed, never of wall-clock time or frame rate.

### 4.5 Determinism

Each mode draws from its own RNG stream, derived from `(world seed, stream id, simulated time, cell index)` — never from a global counter. A weather window therefore produces the same result whether or not the player opened it, and whether it ran in one frame or twenty. Replay is guaranteed through recorded scenario, seed, code version and player commands (ADR-0003).

## 5. Performance budget

Measured on the target desktop machine, from milestone M3 onward, and gated in CI:

| Budget | Development resolution (level 5, 3 layers) | Shipped resolution (level 6, 5 layers) |
|---|---|---|
| Climate mode | ≥ 20 simulated years / minute | ≥ 5 simulated years / minute |
| Weather window | ≥ 1 simulated day / second over a regional subset | ≥ 1 simulated day / second |
| Reference mode | no budget (offline) | no budget (offline) |
| Snapshot | ≤ 5 ms to produce, ≤ 20 MB in memory | ≤ 15 ms, ≤ 80 MB |
| CI scenario: 250 years headless | ≤ 4 minutes | ≤ 10 minutes |
| Memory, whole simulation | ≤ 1 GB | ≤ 4 GB |

Two instances run simultaneously when the counterfactual planet is enabled (Design Record §29), so **the budget applies to half the machine's available time**.

## 6. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | Energy, water and carbon budgets close in every mode | drift < 0.1 % per simulated century |
| V2 | Climatology parity: climate mode vs reference under identical forcing | mean temperature ± 0.3 K, rainfall ± 10 %, extreme-event rates ± 20 % |
| V3 | Mode-switch continuity: open and close a window, compare against not opening it | no field in `S` moves more than the V2 tolerance |
| V4 | Performance budget (§5) | CI fails on regression > 20 % |
| V5 | Determinism and replay: same seed, same commands, different window schedules | bit-identical `S` for a given build and platform |
| V6 | Acceptance targets of Design Record §30 | within stated ranges |

V2 and V3 are the ones that keep this ADR honest: they are what prevents accelerated play from quietly becoming a different planet from the validated one.

## 7. Consequences

**Positive.** Centuries become playable without abandoning physics. Consumers see one state whatever the mode. The reference solver stays small, slow and trustworthy, which is exactly what a calibration oracle should be. The counterfactual planet becomes affordable. An ML surrogate can later replace climate mode's parameterisations behind the same contract.

**Negative.** Three code paths to maintain, with their reconciliation tests. Fitting climate-mode statistics to reference runs is genuine research work, not a weekend task, and it must be redone whenever a transport process changes. Hazards during accelerated play are sampled rather than resolved, so a player who zooms in on a storm is looking at a different level of description from the one that produced it.

**Risks and mitigations.**

- *The fit drifts as the model evolves* → the calibration harness and the acceptance table run in CI, and coefficients are versioned with the fit.
- *Window and climate fluxes double count* → single ownership per region while a window is open, plus the V3 continuity test.
- *Accelerated mode becomes the only tested path* → reference runs stay in CI at small resolution, permanently.

## 8. Milestone mapping

Milestone numbers follow the P0 plan in `docs/DEVELOPMENT_SPEC_v0_4.md` §13 (unchanged since v0.2),
not the Design Record v0.4 §25 table, which places snow/ice at M9.

| Milestone | What this ADR requires |
|---|---|
| M0–M2 (skeleton, orbit, geology) | State partition in the field layout; scheduler skeleton with mode enum; RNG stream discipline |
| M3 (surface energy) | First performance measurement; CI gate created even if generous |
| M4–M6 (snow/ice, atmosphere, wind) | Reference mode for the surface, cryosphere and atmosphere; V1 conservation tests |
| M7–M9 (humidity, clouds/precipitation, hydrology) | Climate-mode parameterisations for moisture and rainfall; first V2 parity tests |
| M10–M12 (vegetation, ocean, carbon) | Reference mode for the ocean at M11; weather windows; V3 continuity; hazard sampling from `X` |

*Amendment 2026-09-28:* the original table assigned "reference mode for the
surface and ocean" to M4–M6, but the ocean is introduced at M11 in both the
design and specification plans. Ocean reference mode now sits with M11. Rows
carry their topics so a future renumbering is visible.

## 9. Open questions

- ~~Does climate mode resolve the seasonal cycle explicitly (12 steps/year) or carry seasonal statistics?~~ Resolved by ADR-0006: explicitly, at twelve orbital sub-steps per year.
- What is the smallest regional subset for a weather window that still behaves physically at its boundaries?
- Do storms during accelerated play need tracks, or only strike locations and intensities? Tracks are better for legibility, and imply object-based storms (Design Record §34, stage S1).
- Is the counterfactual planet run at reduced resolution to halve its cost, and if so, does that break the comparison's credibility?

## 10. Implementation record: the M3 performance gate (2026-09-29)

§8 asks M3 for the first performance measurement and a CI gate "even if
generous". `planet_cli run` reports the stepping rate in simulated years per
wall-clock minute and the total wall time including terrain generation and
spin-up, and exits non-zero if `--min-years-per-minute` or `--max-seconds` is
missed. CI's Release jobs run the §5 scenario, 250 climate-mode years, at L5
(≥ 20 years/min, ≤ 240 s) and L6 (≥ 5 years/min, ≤ 600 s), and replay the
L5 run (ADR-0003 V2). Measured with Clang 14 Release (task M3-03), the only
physics being ADR-0007's surface columns: L5 2,981 years/min on 4 workers
(250 years in 5.3 s), L6 774 years/min on 4 workers (20.2 s). The gates are
therefore the budget itself, with a margin of two orders of magnitude.
V4's regression comparison (> 20 %) needs a stable runner baseline and is
deferred to the first milestone whose cost approaches the budget; the
measured rates above are its first data points.

## 11. Amendment: gating the budget on hosted CI runners (2026-09-30)

**Context.** §5 measures the budget "on the target desktop machine" and
gates it in CI. The M3-03 gate (§10) ran the 250-year scenario on the
hosted runners with §5's wall-clock limits. That held while a monthly step
cost about 1 ms. With the transport of ADR-0009 (M4-02) and sea ice
(ADR-0008 §4.4, M4-03), a monthly step costs about 39 ms at L5 and 132 ms at
L6 on four workers. The target machine still meets every §5 budget: the
250-year scenario takes 117 s at L5 (limit 240 s) and 395 s at L6 (limit
600 s). A hosted runner, however, is about 2.5 times slower: at `bd6030d` it
ran L5 at 51.8 years/min against 128.6 on the target machine, taking 290 s,
and CI failed from `ca3fa64` onwards. The limits were being applied to the
wrong machine.

**Decision.**

- The budgets of §5 are unchanged and remain defined on the target machine.
  Each task that changes the cost measures the full 250-year scenario at L5
  and L6 there, with four workers, and records it in its implementation
  record (V4 evidence).
- CI gates the equivalent **rate**. A scenario limit of 250 years in 240 s
  (L5) or 600 s (L6) means at least 62.5 or 25 simulated years per minute on
  the target machine. CI runs 25 years at each level on four workers and
  requires those rates divided by the **runner factor** `2.5`: 25
  years/min at L5 and 10 at L6. The climate-mode budgets (20 and 5
  years/min) are weaker than these and are implied.
- The runner factor is a measured constant, recorded like a calibration
  constant (specification §24): target machine versus hosted runner on the
  same commit and scenario. It is re-measured when the runner image or the
  target machine changes, and never raised to make a failing gate pass.
- CI replays the 25-year L5 run (ADR-0003 V2).

**Consequences.** CI time for the gate falls from about 15 minutes per
Release job to about 3. A 25-year run overstates the long-run rate slightly
while ice builds up (L5: 140 against 129 years/min), well inside the
factor-of-two margin the gates leave at `bd6030d` (L5 about 56 against 25;
L6 about 16 against 10). The > 20 % regression comparison of V4 remains
deferred (§10). The first regression it would catch is the next physics
module, so the L6 margin is the number to watch.
