# Task M3-01 — Sub-step mean insolation

- **Milestone:** P0 / M3 (first task). M2 is complete.
- **Requested:** 2026-09-29
- **Scope:** the climate-mode forcing of ADR-0006 §4.3: the time mean of
  top-of-atmosphere insolation over one orbital sub-step, per cell. No
  surface energy, no new slow state (that is M3-02).
- **Governing decisions:** ADR-0006 §4.3 and §5 (V3, V4, V6); ADR-0004
  (orbit, frames); ADR-0003 L0 determinism; ADR-0002 §4.6 (deterministic
  blocks)

## 1. Read first

1. `AGENTS.md`
2. `docs/decisions/0006-seasonal-climate-steps.md` (§4.3, §5, §9)
3. `docs/decisions/0004-keplerian-orbit-and-coordinate-frames.md` and
   `docs/M1_TECHNICAL_SPEC.md`
4. `sim/planet/orbit/orbit_state.hpp` (`evaluate_orbit`),
   `sim/planet/orbit/solar_forcing.hpp`, `sim/planet/orbit/climate_calendar.hpp`,
   `sim/core/scheduler/orbital_calendar.hpp`

## 2. Current code (as of the commit that adds this task)

- `evaluate_orbit(parameters, simulation_time_s)` returns, among others,
  `solar_declination_rad` and `incident_solar_flux_W_m2` (inverse-square flux
  at the instantaneous distance).
- `update_solar_forcing` fills the derived field
  `top_of_atmosphere_insolation_W_m2` with the **instantaneous** insolation
  at a tick; reference mode and weather windows keep using it.
- `climate_substep(k)` gives a sub-step's `[begin_tick, end_tick)`.
- Cell latitude follows from the body-fixed centre, north on +Z
  (`latitude_rad` in `sim/planet/coordinates/local_tangent_basis.hpp`).

## 3. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Diurnal mean, analytic.** For latitude φ, declination δ and incident
   flux S at one instant, the daily-mean insolation is

   ```text
   Q_day = (S/π) · (h0 · sinφ · sinδ + cosφ · cosδ · sin h0)
   cos h0 = −tanφ · tanδ, clamped: h0 = π in polar day, 0 in polar night
   ```

   Evaluate the clamp from `sinφ·sinδ` and `cosφ·cosδ` (never from `tanφ` at a
   pole), so the poles are exact.
2. **Orbital mean by fixed Gauss–Legendre quadrature** over the sub-step's
   exact time span `[begin_tick, end_tick) × 60 s`: `N` nodes, `N` a
   compile-time constant starting at 8, raised only if V3 or V4b fails, and
   recorded. Nodes and weights are exact constants in code.
3. **Validity guard.** The synodic day `1 / (1/T_sidereal − 1/P)` must be
   shorter than a tenth of the sub-step (ADR-0006 §6); otherwise throw
   `std::domain_error`. The default Earth passes by a factor of about 3.
4. **Derived field, registered.** Register
   `substep_mean_insolation_W_m2` as a new `derived`, `cell`, `float32`
   field with the next free forcing ID `0x0001'0002` (units `W/m2`), add it to
   the registry baseline in the same commit, and store it in
   `ForcingState`. It is never persisted.
5. **API.**

   ```cpp
   // sim/planet/orbit/substep_forcing.hpp
   void update_substep_mean_insolation(PlanetState& state,
                                       const PlanetParameters& parameters,
                                       const ClimateSubstep& substep,
                                       std::size_t worker_count = 1U);
   // Pure helpers, for tests and diagnostics:
   [[nodiscard]] double daily_mean_insolation_W_m2(double latitude_rad,
                                                   double declination_rad,
                                                   double incident_flux_W_m2) noexcept;
   ```

   The per-node orbit states are evaluated once per call (serially, fixed
   order); the per-cell sums run on the deterministic blocks. Accumulate in
   `double`, store `float`.
6. **Refinement of ADR-0006 V3 and V4, to be recorded in its §9.**
   - *V3 isolates the time quadrature.* It integrates the daily-mean formula
     over latitude with a high-order quadrature (not over the mesh), so it
     measures the calendar and orbital quadrature, not the mesh's spatial
     sampling error. The mesh-sampled global mean is also reported and gated
     more loosely (§5, E4).
   - *V4 is split.* V4a compares the analytic daily mean with the
     instantaneous M1 forcing averaged over whole synodic days. V4b compares
     the Gauss–Legendre sub-step mean with a dense time average of the daily
     mean. A dense average of *instantaneous* forcing over a sub-step would
     include partial days at both ends (sub-steps are 30.44 days), an error of
     up to about 1.7 % that the diurnal averaging of §4.3 removes on purpose.

## 4. Implementation steps

Small, reviewable commits; build and run `ctest` after each.

1. `daily_mean_insolation_W_m2` with unit tests: equator at equinox equals
   `S/π`; polar night is 0; polar day at the solstice equals `S·sinδ` at the
   pole; symmetry `Q(φ, δ) = Q(−φ, −δ)`; continuity across the polar-day and
   polar-night edges.
2. Gauss–Legendre nodes and weights, the validity guard, and
   `update_substep_mean_insolation`; registry field, baseline line and
   `ForcingState` member.
3. Tests of §5.
4. CLI: `planet_cli solar --substep K` prints the sub-step's span and the
   global, zonal (every 10° band) and extreme mean insolation, plus a ctest
   smoke test. Existing `solar` output is unchanged without `--substep`.
5. Documentation: ADR-0006 §9 (M3-01 part: node count, results, the V3/V4
   refinement), README solar section, audit if a row changes, task index and
   `AGENTS.md`.

## 5. Acceptance criteria

| # | Check | Gate |
|---|---|---|
| E1 | Daily-mean formula unit cases (§4 step 1) | 1e-12 relative, or exact where stated |
| E2 | ADR-0006 V3: length-weighted mean over the twelve sub-steps of one orbital year of the latitude-integrated daily mean (≥ 2,000-point Gauss–Legendre in sin φ) equals `L / (16π a² √(1 − e²))` | relative error ≤ 1e-6 |
| E3 | ADR-0006 V4a: at 12 dates spread over the year and 7 latitudes, the analytic daily mean equals the instantaneous insolation averaged over one synodic day with one-tick sampling | abs. error ≤ 1e-4 · S₀/4 |
| E4 | Mesh global mean: at L5, the area-weighted mean of the twelve sub-step fields, length-weighted, matches the analytic annual mean; report the value | relative error ≤ 1e-4 |
| E5 | ADR-0006 V4b: per cell at L4, each sub-step mean against a dense (one-hour) time average of the daily mean over the same span | abs. error ≤ 1e-4 · S₀/4 |
| E6 | ADR-0006 V6 (forcing part): the field is bit-identical for 1, 2, 8 and 16 workers | bit-identical |
| E7 | Values are finite, non-negative, zero in polar night, and bounded by the instantaneous maximum over the sub-step | exact |
| E8 | Seasonality: at the June-solstice sub-step the northern polar cap receives more than the equator; at the December one the southern cap does | as stated |
| E9 | Validity guard: a synodic day longer than a tenth of a sub-step throws | exact |
| E10 | Registry check, the full suite, warnings-as-errors and the floating-point policy pass in the CI matrix | pass |

## 6. Out of scope

- Surface energy, temperatures, albedo and any new slow state (M3-02).
- Using the scheduler to drive a physics process (M3-02).
- Changing the instantaneous forcing or the Godot adapter.

## 7. Environment notes

- Configure and test: `cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON`,
  `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- On Linux 6.x kernels with 32-bit mmap randomisation, sanitizer binaries
  built by Clang 14–17 hang at start-up; run them as
  `setarch -R ctest --test-dir build-sanitize`. CI lowers `vm.mmap_rnd_bits`.
- Work alone in the checkout. Stop and ask before adding a dependency,
  changing an accepted ADR's decision, changing a §3 decision, or weakening a
  test.

## 8. Report when done

- Files added and changed, per step.
- The result of each acceptance item E1–E10, with measured errors and the
  final Gauss–Legendre node count.
- `planet_cli solar --substep 3` output at L5.
- Anything not met, and why; any ambiguity found.
