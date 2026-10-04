# Task M6-04 — Climate-mode balanced circulation

- **Milestone:** P0 / M6 (fourth task; M6-01 to M6-03 are complete)
- **Status:** in progress — steps A, B and C done; ADR-0011 §14 and §15 accepted, §16 proposed (2026-10-04)
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

## Step B, part 1 — the zonal-mean model (2026-10-04)

**Code.**
- `ZonalCirculation` (`sim/planet/dynamics/zonal_circulation.{hpp,cpp}`)
  implements §14 on 36 bands and N σ layers.
- `BandedMatrix` and `BandedLU` (`sim/core/math/banded_lu.{hpp,cpp}`): LU
  with partial pivoting, as in LAPACK's dgbtrf.
- `planet_cli zonal-circulation` solves it under Held–Suarez forcing and,
  with `--reference FILE.csv`, prints it beside a reference run's zonal
  means.
- Tests: `tests/physics/test_zonal_circulation.cpp`,
  `tests/unit/test_banded_lu.cpp`.

**Discretisation, as built.** These choices are within §14; each was
forced by a measured failure:
- **E is an unknown.** The column eddy kinetic energy is an unknown of
  each band, beside ū_k and θ̄_k, so the screened equation stays local.
  The Jacobian is then banded, with 3N + 2 unknowns per band (§14
  estimated 3N + 1).
- **Exact Jacobian.** The residual is one template, evaluated on doubles
  or on forward-mode dual numbers. Columns coloured five blocks apart
  give the exact banded Jacobian in 5(3N + 2) evaluations. It agrees with
  central differences to 3e-7 of the row scale, converging as h².
- **Limited fluxes.** The meridional fluxes of M and θ̄ use limited MUSCL
  face values (van Albada, which is differentiable), not first-order
  upwind.
  - First-order upwind diffuses the planetary angular momentum by
    ½|F| ΔM, and ΔM ≈ 2e8 m²/s per band.
  - That is a spurious torque of about 1e-5 m/s² wherever |v̄| varies, as
    large as the friction. It left ±1 m/s two-band noise in the bottom
    wind, which the latitude C-grid's averaged Coriolis term does not
    restore.
  - The limiter keeps the face values upwind at extrema of M, which is
    Hide's constraint.
- **Smoothed |F|.** The upwind |F| is F² / √(F² + ε²) with ε = μ × 0.01
  m/s, which is zero at rest.
  - The first choice, √(F² + ε²), diffused M and θ̄ with no flow, so a
    resting atmosphere felt a torque.
  - The eddy and gust speed is √(E + √(E² + w⁴)), smooth through E = 0.
    That matters at Ω = 0, where E vanishes.
- **Isobaric gradients.** ∂θ̄/∂y in the closure and in the numerical
  diffusion is taken on pressure surfaces:
  `∂lnθ/∂y|_p = ∂lnθ/∂y|_σ − (∂lnθ/∂ln p) ∂ln p/∂y|_σ`.
  - Along σ, an isothermal atmosphere at rest over terrain generated eddy
    energy and heat fluxes.
  - The correction is exact for an isothermal column. That rest state is
    now the exact steady state (residual below 1e-9, wind below 1e-9 m/s)
    at Ω = 0 and at Earth's Ω.

**Solution, as built.**
1. **Newton first.** Newton from §14's initial state, with an Armijo line
   search on the scaled RMS residual: at most 12 iterations and 4 halvings
   of the step.
2. **Continuation as fallback.** If Newton fails, pseudo-transient
   continuation starts again from the same initial state.
   - Backward Euler in ū, θ̄ and v̄, with E and the lid held.
   - The step grows as the RMS residual falls, at most 2× per iteration.
   - A step that would raise the RMS by half is rejected, and the
     pseudo-step cut fourfold.
3. **Newton polish.** Every 10 accepted continuation steps, Newton is
   tried from a copy of the iterate.
4. **Convergence.** The scaled maximum residual (m/s and K per day) falls
   below 1e-8.

Findings:
- **Newton alone rarely converges from §14's initial state**, even when
  T⁰ is the equilibrated temperature. That state is thermal wind with no
  Hadley cell and E = 0, and line searches stall at 7 m/s/day. Plain
  Newton diverges.
- **Small pseudo-steps stall too.** Continuation alone stalls with
  residuals of about 1 m/s/day in the polar bands (N = 5), where small
  pseudo-steps follow slow dynamics that never settle.
- **The steady state is unstable to time-marching.** From that stalled
  iterate, Newton converges quadratically: 9.6 → 7.1 → 3.4 → 1.2 → 4e-2 →
  3e-4 → 6e-8 → 9e-12. Hence the polish.
- **The answer is the same whichever path reaches it.** At Ω and 2Ω,
  starting from T_eq or from the equilibrated temperatures gives the same
  steady state to 5e-9 m/s.
- **At Ω/2 it is not unique.** The two starts reach states 12–16 m/s apart
  (N = 3 and N = 5). The N = 3 Ω/2 solution also has two-band noise in
  the polar bottom wind.

**Held–Suarez** (`planet_cli zonal-circulation --reference
tools/zonal_mean_prototype/data/hs_L4_N3.csv`; c_E = 1.7e12, the HS N = 3
fit, ν = 5e4 m²/s):

| | Model, N = 3 | Reference, L4 N = 3 | Model, N = 5 | Reference, L4 N = 5 |
|---|---|---|---|---|
| Jet | 24.3 m/s at 27.5° | 25.8 m/s at 32.5° | 46.4 m/s at 32.5° | 30.4 m/s at 42.5° |
| Bottom winds, 2.5–87.5° N | EEWEWWWWWWW0EEEEEE | EEEEWWWWWW0EEEEEEE | EWEEWWWWWEEEEEEEEW | EEEEEWWWWWWWEEEEEE |
| Bottom-layer contrast | 41.7 K | 38.7 K | 41.1 K | 38.5 K |

- **N = 3 is close.** The jet is 5° equatorward. The trades reach 7.5°
  (reference 17.5°), and 12.5° is westerly where the Hadley cell's descent
  brings the upper branch's momentum into the bottom layer.
- **N = 5 is not.** The jet is half as strong again and 10° equatorward,
  and the westerlies stop at 42.5°. Raising c_E strengthens the jet
  (76 m/s at 3e12) instead of moving it poleward, and at 4.35e12 (the N = 5
  fit without χ) no steady state is found.
- **The cause is not yet diagnosed.** The momentum closure's vertical
  distribution is a candidate: uniform over σ < 0.7, where the reference
  concentrates it near the jet. The Earth-like preset runs on three
  layers (M5-04), so this does not block the climate path, but c_E's
  calibration (§4.8) must be rechecked at N = 5.

**V8 and V9** (N = 3, from T_eq):

| Ω × | Hadley edge | Max \|ψ\| (1e9 kg/s) | Jet | Surface torque / gross |
|---|---|---|---|---|
| 0 | 90° (one cell) | 2,181 | 0 (exactly) | 0 |
| ½ | 30° | 875 | 43.5 m/s at 32.5° | 2e-15 |
| 1 | 25° | 231 | 24.3 m/s at 27.5° | 8e-16 |
| 2 | 20° | 55 | 13.2 m/s at 22.5° | 1e-15 |

At Ω = 0 the wind and the eddy energy are zero to rounding. The torque is
the rounding of a converged solution: the transport and the eddies are in
flux form.

**Cost** (GCC Release, one core; first figure from T_eq, second from the
equilibrated temperatures):

| | Ω × 0 | Ω × ½ | Ω × 1 | Ω × 2 |
|---|---|---|---|---|
| N = 3 | 72 / 180 ms | 63 / 19 ms | 23 / 17 ms | 10 / 7 ms |
| N = 5 | 263 / 671 ms | 430 / 163 ms | 113 / 132 ms | 56 / 108 ms |

- **Above the estimate.** §14 estimated well under 50 ms at N = 5; the
  measured cost is 2–3 times that at Earth's rotation.
- **Where the time goes.** Each Jacobian costs 5(3N + 2) dual
  evaluations of the residual. A Jacobian that evaluates only the rows
  each colour touches would cut that roughly fivefold.
- **Optimisation is deferred to step D's L6 timing, where the budget is
  set.**

**Tests** (`test_zonal_circulation`, 0.3 s):
- **Rest.** Over terrain, the rest state is exact at Ω = 0 and Ω.
- **Jacobian.** It matches central differences; nothing lies outside
  the band.
- **Held–Suarez, N = 3.**
  - The jet is within 22.5–37.5° at 18–35 m/s.
  - Trades at 2.5° and 7.5°; westerlies over 27.5–42.5°; easterlies over
    67.5–77.5°. Each holds in both hemispheres.
  - The bottom-layer contrast is 33–45 K.
  - The solution is symmetric about the equator to 1e-6 m/s.
  - A second solve repeats it bit for bit.
- **Newton's convergence.** From a perturbed solution, Newton converges
  quadratically.
- **V8.** At Ω = 0: no wind, no eddy energy, one cell. The Hadley edge
  narrows monotonically with Ω and lies within 20–35° at Ω.
- **V9.** The total torque is at most 1e-9 of the gross for every Ω.
- **N = 5** converges.
- **Validation.** Invalid forcing and a single layer are refused.

The solve is sequential, so the plan's "1, 2 and 8 workers" check does not
apply to it. It applies to the band means and the mapping to the mesh,
which step B part 2 adds.

## Step B, part 2 — heating, convection and the Earth-like planet (2026-10-04)

**Code.**
- `compute_atmosphere_heating` (`sim/planet/surface/surface_energy.{hpp,cpp}`)
  gives Q⁰ and Λ per layer and cell at the slow state.
- `sim/planet/dynamics/zonal_coupling.{hpp,cpp}`:
  - `zonal_circulation_parameters` sets the model up for a planet.
  - `zonal_forcing_from_state` takes the band means.
- `ZonalCirculation` gains convective relaxation and vertical momentum
  diffusion.
- `planet_cli zonal-circulation --planet` runs a climate year and solves
  each month.
- Tests: `tests/physics/test_zonal_coupling.cpp`, and a convection case in
  `test_zonal_circulation`.

**The heating.**
- **Definition.** Q⁰_k is the column's longwave heating, plus the sensible
  heat in the bottom layer, at the slow state's layer temperatures, in
  K/s.
- **The surface is held.** Its tiles step over one minute under the
  sub-step's insolation. Land and open water then keep their slow-state
  temperatures, while sea-ice floes, which store no heat, take their
  balance temperature.
  - The first version stepped the tiles over the whole month with the air
    held. The surface ran ahead of the air, and the sensible heat gave a
    different Q⁰.
- **Λ_k** holds the surface too: only its reflection follows the air, and
  the sensible heat follows only the air.
  - It is negative in every cell.
  - Its damping time is about 2.5 days in the bottom layer (the sensible
    exchange), 24–34 days in the middle layer and 110–160 days in the top
    layer.
- **Checks.**
  - Σ_k Q⁰_k C equals U + H − D − OLR to 1e-9 W/m² in every cell.
  - The result is identical for 1 and 4 workers.
- **The net heating is positive.** At the slow state the atmosphere's
  Q⁰ sums to +48 W/m² globally (bottom layer +60, others −13).
  - The cause is convection, not a fault. The slow state has just been
    adjusted convectively, which leaves the bottom layer cooler than its
    radiative and sensible balance.
  - The zonal model's own convection carries that excess up (an excess of
    about 0.3 K at τ_c = 3 h), and Λ takes the remainder.
  - Because Λ is diagonal, the zonal model's temperature departs from T⁰ by
    a few kelvin where the column convects. Step C's coupling should
    measure this.

**Band means.** Taken in cell order, so independent of the worker count:
- p_s and the dynamics' smoothed height by area;
- T⁰, Q⁰ and Λ by mass (area × p_s);
- C_D from the land fraction (ocean 1.5e-3, land 4e-3).

The bands' mass equals the atmosphere's to 1e-12.

**Convection.**
- **Where.** Between adjacent layers where θ_c = T / σ^κ_c falls upward
  (κ_c = R Γ_c / g, as ADR-0010).
- **How.** The exchange δ that would make the pair neutral moves up at
  the rate ρ(δ)/τ_c, with τ_c = 3 h and ρ(δ) = δ² / √(δ² + δ₀²) for δ > 0.
  This conserves the pair's enthalpy.
- **Test.** A uniform superadiabatic column (about 9 K/km) settles with
  Σ_k (T − T⁰) = 0 to 1e-6, and its instability is more than halved.

**Vertical momentum diffusion** (K_v = 1 m²/s, Δz²/K_v ≈ 140 days). It
is not in §14's text, but it is needed:
- **The problem.** Where nothing moves and no drag acts aloft, any
  solid-body wind aloft is steady. The steady problem then has a null
  space, and Newton picks an arbitrary U.
  - The uniform-column test reached 46 m/s.
  - The no-eddy Earth case had a 24 m/s solid-body westerly aloft.
- **The fix.** The diffusion conserves the column's angular momentum, so
  V9 is unchanged.
- **Effect on Held–Suarez, N = 3.** Essentially none: a 23.9 m/s jet at
  27.5°. The Hadley edge moved from 25° to 30°, so V8 now reads 40°, 30°
  and 20° for Ω × ½, 1, 2.
- **What remains without eddies.** A 14 m/s equatorial westerly aloft
  (2 m/s at K_v = 10). The lateral viscosity ν diffuses subtropical
  westerly momentum into a near-stagnant equator; with eddies on, the
  equator turns easterly.

**c_E for the Earth-like planet.** It is 7.8e11, reference mode's own fit
(`momentum.py` on `ref_L4_N3.csv`). It is provisional until §4.8's joint
fit with τ₀.
- **HS's value fails here.** With 1.7e12, continuation never settles:
  sharp multiple jets (50 m/s at 52.5° S and 32.5° S, E up to 80)
  wander.
- **At 7.8e11** the months converge, most by Newton directly.
- **At 1.2e12** all converge, mostly by continuation, at 20–50 ms each.

**Earth-like, climate year** (L4, seed 1, two years' spin-up; the mean of
the twelve monthly solves; `planet_cli zonal-circulation --planet
--reference tools/zonal_mean_prototype/data/ref_L4_N3.csv`):

| | Zonal model | Reference mode (L4, two-year mean) |
|---|---|---|
| Jet | 30.2 m/s at 52.5° | 27.7 m/s at 52.5° |
| Top wind, 2.5° / 32.5° / 62.5° / 82.5° | −3.0 / 19.0 / 14.4 / 2.4 | −7.2 / 16.0 / 18.5 / 4.0 |
| E maximum | 22 at 52.5° | 52 at 62.5° |
| Bottom winds, 2.5–87.5° N | EWWEEEE0WWWWEEEEEE | WWEEEWWWWWWWWWWWWW |
| Bottom-layer contrast | 33.4 K (the slow state's) | 49.8 K |

- **What matches.** The upper-level jet follows reference mode from the
  subtropics to the pole.
- **What doesn't.**
  - The eddy energy is half the reference's.
  - The surface westerlies are confined to 42.5–57.5°, with polar
    easterlies beyond; reference mode is westerly from 27.5° to the pole.
  - The equatorial easterlies aloft are weaker.
- **Not comparable yet.** The temperature contrast is the climate-mode
  slow state's, still shaped by the diffusive transport. A like-for-like
  comparison waits for the circulation to carry the climate's heat.
- **Cost.** Every month converges: 8 of 12 by Newton directly (10–12
  Jacobians), 4 by continuation (29–46). A month takes 8–38 ms, 15 ms on
  average, at N = 3.
- **Torque.** The mean monthly torque is 1e-13 of the gross.

**N = 5 at Ω: two starts no longer agree.** Since K_v, the start from T_eq
and the start from the equilibrated temperatures end 0.16 m/s apart
(previously 5e-9). This is either another case of non-uniqueness or a
poorly conditioned direction; to be looked at with the N = 5 jet bias.

## Open points after step B (2026-10-04)

1. **Cost (N = 5 at 110–150 ms).**
   - The per-iteration cost is close to its floor at about 1.8 ms: a
     Jacobian takes 1.6 ms (85 dual evaluations, each entry computed
     once) and the LU 0.2 ms. The total is set by the iteration count.
   - Abandoning a Newton attempt that has not cut the residual tenfold in
     3 iterations was tried and rejected. Newton often needs damped steps
     first, and two cases lost convergence.
   - The Earth-like preset (N = 3) averages 15 ms a month, within the
     20-years-per-minute gate (about 250 ms per month for everything).
     N = 5 is not.
2. **Non-uniqueness.** It is confined to the polar caps (up to 15 m/s at
   87.5° at Ω/2). The polar band's momentum budget is divided by
   cos φ × band area ≈ 2e-4.
   - K_v gives no reliable fix: the solution is unique at 2 and 10 m²/s,
     not at 1, 3 or 5.
   - K_v = 10 also removes the Ferrel cell, which hands the surface
     westerlies to vertical diffusion, so it was rejected. K_v stays 1.
   - L = 2,000 km (below) makes Held–Suarez at N = 3 unique at every Ω.
3. **The N = 5 jet** is 44 m/s at 32.5° against 30 m/s at 42.5°.
   - A momentum-flux shape ∝ sin πσ, which matches the N = 5 reference,
     weakens it to 36 m/s but moves it further equatorward. The Earth-like
     reference has the opposite shape (top-heavy), so the uniform shape
     stays.
   - The eddy energy is too weak (c_E, below), and the subtropical jet at
     the Hadley edge dominates.
4. **The Earth-like eddy energy is half the reference's.** c_E had been
   fitted without χ and the screening. Fitted in the model's form it is
   2.6 times larger, which needs L = 2,000 km to converge. **ADR-0011
   §15 was accepted on 2026-10-04.** L = 2,000 km and c_E = 2.91e12 (HS)
   and 1.39e12 (Earth-like) are now the defaults. The step B records above
   keep the values they were measured with (L = 1,000 km).

`planet_cli zonal-circulation` gained `--eddy-scale L` for these runs.

## Step C — azonal balance and balanced p_s (2026-10-04)

**Code.**
- `BalancedCirculation` (`sim/planet/dynamics/balanced_circulation.{hpp,cpp}`)
  implements §4.6 and §4.4 steps 3–4 on the coarse mesh of ADR-0009 §12.
- `GraphMultigrid` and `agglomerated_mesh` (`heat_transport.{hpp,cpp}`):
  - `GraphMultigrid` makes the transport's aggregation multigrid usable by
    other solves. Its code is unchanged, so the transport's results stay
    bit-identical.
  - The agglomeration cache keeps the coarse `PlanetMesh`, whose C-grid
    the balance needs.
- `planet_cli zonal-circulation --planet --balanced` adds the balance to
  every month.
- Test: `tests/physics/test_balanced_circulation.cpp`.

**The discretisation.**
- **Coarse fields.** The groups' means, as the transport's: p_s, height
  and land fraction by area; T by mass. Layer geopotentials follow task
  M6-03's vertical structure.
- **Zonal means.** Band means (the most of 36, 18, 12, 9 or 6 equal bands
  that each hold three coarse cells), interpolated linearly in latitude
  between band centres. Plain band means are step functions, and gave
  steps in s at the band edges.
- **The edge balance**, per layer:
  u_n = (r G_n + f G_t) / (r² + f²),  G = −∇Φ' − R T̂ ∇s.
  - f is taken at the edge midpoint.
  - The normal derivative uses the two cells; the tangential one uses the
    barycentric corner values (`interpolation_weight`), as the C-grid
    convention requires.
- **The equation for s.** Σ_k div(μ̂ u_n) = 0, area-integrated, is
  assembled in the coarse graph's CSR pattern. The corner cells are all
  in the row's ring.
  - The rows sum to zero for any s (flux form), so the right-hand side is
    projected onto that direction's complement.
  - BiCGSTAB is right-preconditioned by one V-cycle of the multigrid on
    the symmetric (friction) part. Its diagonal is raised by 1e-8 to
    regularise the constant mode.
  - s is then given zero area mean, and the balanced p_s = the zonal
    model's profile (interpolated in latitude) × exp(s), scaled so the
    groups hold the atmosphere's mass.
- **Layer fluxes.** The azonal flux, plus the overturning
  μ̂ v̄_k(φ_e) n_e·ê_north with v̄ interpolated in latitude (zero at the
  poles). The overturning's column sum is zero on every edge, because
  Σ_k v̄_k = 0 (the zonal model's lid).
- **Vertical fluxes.** W_{k+1} = W_k − div F_k per cell, so every layer's
  mass closes exactly. W_N is the column divergence the solver leaves.
- The zonal-mean zonal wind is not part of the edge fluxes, as §4.4
  lists them (overturning plus azonal). Whether the transport should
  carry it is §4.7's question.

**Damping.**
- **Aloft:** 1/r = 5 days (§4.6's "a few days").
- **Bottom layer:** adds the bulk drag linearised at 8 m/s,
  C_D V g N / (R T): about 1/(2.7 days) over the ocean and 1/(1 day)
  over land at N = 3.
- **The deep tropics are the problem.** The slow state, shaped by the
  diffusive transport, carries temperature departures of about 4.5 K rms
  within 15° of the equator. With f → 0 the balance turns them into
  frictional flow u = G/r.

  Top-layer azonal winds at L4, rms:

  | Damping aloft | 0–15° | 15–30° | 30–60° | Max | BiCGSTAB iterations |
  |---|---|---|---|---|---|
  | 5 days | 105 m/s | 15.5 | 9.3 | 869 m/s | 133 |
  | 1 day | 43 | 14.4 | 8.1 | 260 | 28 |
  | 0.25 day | 16.5 | 12.1 | 7.4 | 80 | 20 |
  | 5 days, plus 0.25 day × exp(−(φ/10°)²) | 21.6 | 15.0 | 9.3 | 122 | 130 |

- **Chosen: the last row.** It keeps the extratropical, nearly
  geostrophic flow and its realistic 7–15 m/s, and holds the deep tropics.
  It is proposed as ADR-0011 §16.
- **This is a stopgap for the uncoupled input.** The tropical departures
  belong to the uncoupled slow state. Once the circulation carries heat
  (§4.7), the coupled step should flatten them (the weak-temperature-
  gradient tropics), and step D must check how the fixed-flux transport
  behaves with them.

**Measured** (Earth-like, two years' spin-up, a climate year with
`--balanced`):

| | L4 (coarse L3, 642 cells, 18 bands) | L5 (coarse L4, 2,562 cells, 36 bands) |
|---|---|---|
| BiCGSTAB iterations | 124–140 | 103–114 |
| Balance time per month | 12–15 ms | 27–44 ms (one 143 ms outlier) |
| Relative column divergence | 3e-11 – 4e-10 | 2e-10 – 8e-10 |
| Balanced p_s range | 783–1046 hPa | 745–1043 hPa |
| Top-layer azonal rms, < 20° / 20–60° | 20–22 / 8–9 m/s | 27–28 / 15–16 m/s |

- **Why so many iterations.** The operator is genuinely non-symmetric.
  Away from the equator the Coriolis (β-like) term outweighs the friction
  term about tenfold, as in Stommel's problem, so a preconditioner built
  from the symmetric part leaves 100–140 iterations.

**Tests** (`test_balanced_circulation`, 0.2 s):
- **Isothermal rest over terrain.** The balance recovers s + g z/(R T) as
  a function of latitude alone (to 1e-8, over more than 100 pairs of
  cells on equal latitudes). Every flux stays below 1e-6 m/s.
- **The Earth-like month.**
  - Identical for 1 and 4 workers.
  - Residual below 1e-9; column divergence below 1e-8 of the layers'.
  - The groups hold the atmosphere's mass to 1e-13.
  - W_N below 1e-8 of the interior W.
  - Top-layer azonal rms of 3–30 m/s at 30–60°.
- **The overturning.**
  - Its column sum is zero on every edge to 1e-12.
  - It carries the zonal model's mass transport across 30° N to within
    10%.

**Not in step C:**
- Writing the balanced p_s back to the slow state, and mapping it to the
  fine cells.
- The transport by these fluxes (§4.7).
- The derived winds (step D).

## Plan for steps B–D

1. **`ZonalCirculation`** (`sim/planet/dynamics/zonal_circulation.{hpp,cpp}`)
   — done, step B part 1 above:
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
2. **Column heating** — done, step B part 2 above:
   - Q⁰ and Λ from the column physics at the slow state, as a function
     beside the column solve;
   - convective relaxation;
   - the Earth-like zonal solve at L4 and L5, compared with the V6 data.
3. **Azonal balance and balanced `p_s`** (§4.6) on the coarse mesh — done,
   step C above:
   - BiCGSTAB with the existing multigrid as preconditioner;
   - total mass exact;
   - the layer mass fluxes and the vertical fluxes from discrete
     continuity, so that mass closes per cell.
4. **Derived fields** (§4.1) and the climatology fields; reference mode
   starts from the balanced winds; the first L6 timing.
