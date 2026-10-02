# Task M6-03 — Primitive equations in reference mode

- **Milestone:** P0 / M6 (third task; M6-01 and M6-02 are complete)
- **Scope:** ADR-0011 §4.3 in two steps.
  - **Step A, the dry dynamical core.** N σ layers on the C-grid with
    prognostic `p_s`; Held–Suarez forcing; V4, V5, V9 and V11 for the core.
  - **Step B, reference mode.** The fast-state wind, the `edge_layers`
    layout, mode switches, surface drag and frictional heating, and the
    coupling to the column physics. V6 and V9 in reference mode.
- **Governing decisions:** ADR-0011 §3.3, §4.1, §4.3, §4.9, V4–V6, V9, V11;
  ADR-0010 §4.1–4.4 (layers, `p_s`, temperatures); ADR-0001 §4.1 (fast
  state)

## 1. Decisions made for this task

1. **Vertical discretisation.** This is a Hamiltonian form derived for this
   task. It has the structure of Arakawa and Suarez (1983), in the
   prognostic variables `(p_s, Θ_k, u_k)`, where `Θ_k = μ_k θ_k` and
   `μ_k = Δσ p_s / g`. Layer 0 is the bottom one, and the layers have equal
   mass (ADR-0010).
   - **Interfaces.** Interface m (0 at the surface, N at the top, where
     p = 0) has `p_m = (1 − m/N) p_s` and `π_m = (p_m/p₀)^κ`, with
     `κ = R_d / c_p`.
   - **Layer Exner.** The layer's mass-mean Exner function is
     `π̄_k = (p_k π_k − p_{k+1} π_{k+1}) / ((1 + κ)(p_k − p_{k+1}))`.
     Within an isentropic layer this is exact, so the layer's enthalpy is
     `c_p μ_k θ_k π̄_k = c_p μ_k T_k` with `T_k = θ_k π̄_k`. That is
     ADR-0010's layer temperature and heat capacity.
   - **Geopotential.** It is hydrostatic within each layer
     (`dΦ = −c_p θ dπ`): `Φ_0 = Φ_s = g z_s`, and
     `Φ_{m+1} = Φ_m + c_p θ_m (π_m − π_{m+1})` at the interfaces. At the
     layer's mean-Exner level, `Φ̄_k = Φ_k + c_p θ_k (π_k − π̄_k)`.
   - **Energy.** The total energy is
     `E = Σ A [Σ_k (μ_k K_k + c_p Θ_k π̄_k) + p_s Φ_s / g]`, with K the TRiSK
     kinetic energy. Its derivatives are
     `δE/δμ_k = K_k + Φ̄_k` (at fixed Θ), `δE/δΘ_k = c_p π̄_k` and
     `δE/δu_ke = l_e d_e F_ke`.
   - **Equations.** F_k is the layer's edge mass flux `μ̂_k u_k`, and `W_m` is
     the upward mass flux through interface m, found from continuity
     (`W_0 = W_N = 0`):

     ```text
     ∂p_s/∂t = −g Σ_k div F_k
     ∂Θ_k/∂t = −div(θ̂_k F_k) − (W_{k+1} θ̃_{k+1} − W_k θ̃_k)
     ∂u_k/∂t = PV flux − ∂_n(K_k + Φ̄_k) − θ̂_k ∂_n(c_p π̄_k)
               − (1/(2 μ̂_k)) [Ŵ_{k+1}(u_{k+1} − u_k) + Ŵ_k (u_k − u_{k−1})]
     ```

     Here θ̂ is the edge's potential temperature, `θ̂ = T̂ · ln(π̄_b/π̄_a) /
     (π̄_b − π̄_a)`, where T̂ is the mean of the two cells' T = θ π̄ (see
     below). Ŵ is the mean of an edge's two cells' W, and θ̃ at an interface
     is the Exner-weighted value
     `θ̃_m = [θ_{m−1}(π̄_{m−1} − π_m) + θ_m (π_m − π̄_m)] / (π̄_{m−1} − π̄_m)`.
     That value is a convex combination of the two layers' θ.
   - **Why these choices.** They are exactly the ones for which dE/dt = 0
     in space:
     - the horizontal terms cancel by summation by parts;
     - the vertical terms cancel because of θ̃ and because Ŵ is the edge
       mean of the cells' W;
     - the PV flux does no work, by the weights' antisymmetry.
   - **The pressure-gradient force** `−∇Φ̄ − θ ∇(c_p π̄)` is the σ-coordinate
     `−∇Φ − R T ∇p / p` at the layer's mean-Exner level.
   - **The edge θ̂.** The energy identity holds for any symmetric θ̂ shared
     by the Θ flux and the pressure-gradient force, so θ̂ is chosen for
     hydrostatic consistency.
     - The arithmetic mean of θ leaves a residual force of `c_p T (ln r)³/6`
       on an isothermal atmosphere at rest, where r is the Exner ratio
       between the cells. Across a 5 km terrain step between cells that is
       about 1.5e-3 m/s². The first V4 run reached 37 m/s in a day.
     - The logarithmic form makes the isothermal rest state exact over any
       terrain.
2. **Time stepping.** RK3 with the M6-02 sub-step rule (350 m/s waves,
   100 m/s wind, Courant 0.5), applied to `(p_s, Θ, u)`. Every stage is in
   flux form, so mass and Θ are exact.
3. **Dissipation.**
   - Hyperviscosity acts on u, and surface drag on the bottom layer.
   - The kinetic energy each removes at an edge, `l_e d_e F_ke ∂_t u`,
     returns as heat. Half goes to each of the edge's cells, in the same
     layer, as `∂_t Θ = heat / (c_p π̄)`. Total energy then changes only
     through forcing and RK3's truncation.
4. **Held–Suarez forcing** (Held and Suarez, 1994), for V5:
   - Newtonian relaxation to `T_eq(φ, p)` at the layer's pressure
     `σ_k p_s`, with `k_a = 1/40 d`, `k_s = 1/4 d` and `σ_b = 0.7`.
   - Rayleigh friction `k_f = 1/d` below `σ_b`.
   - Unlike the original, the friction's kinetic energy returns as heat
     (decision 3). The benchmark's circulation is insensitive to this, and
     the core keeps one rule.
5. **Placement:** `sim/planet/dynamics/primitive_equations.{hpp,cpp}`, a
   core with its own state like the shallow-water model, and
   `planet_cli dynamics` for the benchmarks. Step B wires it to the planet.

## 2. Acceptance (step A)

- **Energy identity.** For random states, the dE/dt computed from the
  tendencies (unforced, inviscid) is zero to rounding: relative 1e-12 of
  its gross terms.
- **Mass** is exact to rounding in every run.
- **V4, rest over terrain.** An isothermal atmosphere at rest over the
  Earth-like terrain, with no forcing, may reach at most 0.5 m/s of wind
  after 10 days at L5.
- **V5, Held–Suarez**, N = 3 and 5, L5, 1,000 days with the last 800
  averaged:
  - a top-layer zonal-mean jet of 15–45 m/s at 25–55° in both hemispheres;
  - bottom-layer zonal-mean wind easterly over 5–20° and westerly over
    35–55°;
  - transient eddy kinetic energy present.

  This is a CLI run, recorded here. CI runs a short smoke test.
- **Determinism:** bit-identical on 1, 2 and 8 workers.

## 3. Results

### Step A — the dry dynamical core

`sim/planet/dynamics/primitive_equations.{hpp,cpp}`,
`tests/physics/test_primitive_equations.cpp`, and `planet_cli dynamics`
(Clang Release, 8 workers).

- **Energy identity.** For random states over random 3 km terrain, the dE/dt
  from the tendencies is 1e-17 of its gross terms for N = 1, 3 and 5.
  This held before and after the θ̂ change below, as it must for any
  shared symmetric θ̂.
- **Mass** is exact: changes of at most 8e-16 in every run.
- **V4, rest over terrain.**
  - Isothermal (250 K) over the Earth-like terrain at L5, N = 3: 8.4e-11 m/s
    after 10 days (gate 0.5 m/s). The rest state is exact by the choice of
    θ̂.
  - The arithmetic-mean θ̂ first used reached 37 m/s in a day and 78 m/s
    in ten.
- **A stratified rest state** (T = 288 K − 6.5 K/km, the exact layer means)
  is not balanced, and is recorded rather than gated:
  - At L5, N = 3, the maximum wind is 10.9 m/s after one day and 20.5 m/s
    after ten. The global kinetic energy is 1.6e18 J, about 0.3 % of
    Earth's.
  - N = 5 (21.6 m/s) and L4 (21.5 m/s) are no better. The residual is
    second order in the jump in ln p_s between neighbouring cells, so it
    is horizontal, the σ-coordinate pressure-gradient error.
  - The generated terrain is steep at the grid scale. Neighbouring cells
    differ by up to 5.0 km at L5, 5.9 km at L4 and 3.3 km at L6, and 2.6 %
    of L5 edges cross more than 1 km.
  - Step B will need a remedy where real stratification meets this terrain
    (see §4).
- **Held–Suarez, L4, N = 3,** 300 days with the last 200 averaged, and
  hyperviscosity of τ = 8 h:
  - top-layer jets of 26.2 m/s at 32.5° N and 25.6 m/s at 32.5° S;
  - the bottom layer easterly over 5–20° (−1.5 to −0.2 m/s) and westerly
    over 35–55° (+0.2 to +1.0 m/s);
  - eddy kinetic energy up to 66 m²/s² in the middle layer at 37°;
  - global kinetic energy steady at 3.8e20 J from day 100.

  Every V5 criterion holds at L4. The surface westerlies are weak and the
  jet sits equatorward of Held–Suarez's 45°, which is plausible for three
  layers at 440 km.
- **V5 gate, Held–Suarez at L5, N = 3,** 1,000 days with the last 800
  averaged and τ = 8 h; 51 minutes on 8 workers:
  - top-layer jets of 30.4 m/s at 37.5° S and 30.2 m/s at 37.5° N (gate:
    15–45 m/s at 25–55°);
  - the bottom layer easterly over 5–20° (−0.6 to −1.1 m/s), westerly over
    35–55° (up to 3.2 m/s at 37.5°), and weakly easterly again poleward of
    57° (−0.9 m/s at 62.5°), so trades, westerlies and polar easterlies;
  - eddy kinetic energy up to 65 m²/s² in the middle layer at 37.5°;
  - the hemispheres agree to within 0.3 m/s, and mass is exact.

  From L4 to L5 the jet moves poleward (32.5° → 37.5°) and the surface
  westerlies triple (1.0 → 3.2 m/s), towards Held–Suarez's many-level
  solution. **V5 passes at N = 3.** The N = 5 run is pending.
- **Determinism:** bit-identical on 1, 2 and 8 workers with Held–Suarez
  forcing and hyperviscosity.
- **Cost:** about 2 s per simulated day at L5 with N = 3 on 8 workers, and
  0.5 s per day at L4. Nothing is optimised yet.
- **Tests:** the CI tests run at L3 and L4, and take 11 s each in Debug.
  The larger levels are recorded with the CLI.

## 4. Decided for step B (ADR-0011 §13, 2026-10-02)

- **Terrain the winds see.**
  - The dynamics uses `limit_dynamics_orography_steps`: conservative
    averaging passes until no two neighbouring cells differ by more than
    800 m. That takes 8, 13 and 19 passes at L4, L5 and L6.
  - The column physics keeps the true heights.
  - A stratified atmosphere at rest then reaches 0.20, 0.25 and 0.55 m/s
    after 10 days, against 20.5 m/s unsmoothed at L5.
  - `tests/unit/test_dynamics_orography.cpp` checks the exact mean, that no
    new extremes appear, the fewest-passes rule and determinism.
- **The sub-step rule's wind bound** is 150 m/s. The first 5-layer
  Held–Suarez run at L5 stopped at 100.85 m/s in its spin-up burst. A trace
  at L4 showed ordinary baroclinic spin-up, with eddies gusting to
  60–80 m/s and nothing numerically unstable. The 5-layer gate run is
  repeated with the new bound.

### Step B — reference mode

`sim/planet/dynamics/atmosphere_dynamics.{hpp,cpp}`, the first fast field
`atmosphere_edge_normal_wind_m_s` (`0x0006'0001`, `edge_layers`),
`tests/physics/test_reference_winds.cpp`, and `planet_cli reference`.

- **Coupling.** Each 10-minute reference step runs the column physics and
  then the dynamics, a process split.
  - The dynamics reads `p_s` and the layer temperatures, and the winds from
    the fast state; it advances the step and writes them back.
  - The winds see the step-limited orography (§4), and bulk drag acts on
    the bottom layer, with C_D = 1.5e-3 over ocean and 4e-3 over land, and
    its kinetic energy returned as heat.
  - Reference mode no longer runs ADR-0009's diffusion; climate mode keeps
    it.
  - Climate steps release the fast state, and the first reference step
    opens it with the winds at rest. The balanced start is M6-04's.
- **Energy identity with dissipation.** With drag and hyperviscosity on,
  the semi-discrete dE/dt is 1e-17 of its gross terms for N = 1, 3 and 5,
  so the heat return is exact.
- **Spin-up from rest** is a violent geostrophic adjustment. In pure
  dynamics from the L3 climate state, kinetic energy reaches 9.4e20 J in two
  days. RK3's energy error over those two days converges at third order:
  7.1e-5, 1.2e-5, 1.6e-6 and 2.1e-7 of the total at Δt = 600, 300, 150 and
  75 s.
- **V6, Earth-like at L4, N = 3,** after 2 climate spin-up years, then 1,096
  reference days; 8 orography passes; 24 minutes on 4 workers:
  - **Mass:** relative changes below 6e-15 throughout.
  - **Energy:** the dynamics changed the total by −1.198e20 J, which is
    −3.1e-5 per year over the whole run. Of that, −1.1964e20 J came in the
    first 30 days, the spin-up from rest. From day 30 to day 1,096 the
    change is −1.6e17 J, a drift of **4e-8 per year**, inside the 1e-5 gate
    by a factor of 250. The equilibrated drift passes. The run's total
    fails only through the initial shock, which M6-04's balanced start
    removes.
  - **Circulation** (zonal means of the last 730 days, recorded for the
    climate-mode comparison):
    - top-layer jets of 26.6 m/s at 55° S and 25.8 m/s at 55° N;
    - upper tropical easterlies of 4–7 m/s;
    - bottom-layer westerlies of 1.4–2.2 m/s over 35–65° in both
      hemispheres;
    - weak or absent trades (−0.7 m/s at 15° N, slightly westerly within
      10° of the equator). That is plausible for a dry model at L4 with
      three layers, without the latent heating that drives a strong Hadley
      cell (M7–M8).
  - **Surface temperature** still falls, from 293.1 K in the first month to
    285.8 K by the end, about 2 K per year. Two spin-up years are not
    equilibrium, and the resolved winds carry heat differently from the
    calibrated diffusion. Reconciling the two is M6-05's refit, not a gate
    here.
  - **Cost:** about 1.3 s per simulated day at L4 on 4 workers, with the
    column physics included.
- **Determinism.** `test_run_replay` switches the Earth-like planet into
  reference mode for two days and back. It replays bit for bit on 1, 2 and
  8 workers and with chunked runs, with the winds included.

