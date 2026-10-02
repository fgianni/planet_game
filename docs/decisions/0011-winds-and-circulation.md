# ADR-0011 — Winds: a C-grid dynamical core in reference mode and a balanced circulation in climate mode

- **Status:** Accepted
- **Date:** 2026-10-02
- **Accepted:** 2026-10-02
- **Amended:** 2026-10-02 — §12: V2 gates for the TRiSK operators, as measured in M6-01
- **Amended:** 2026-10-02 — §13: the orography the winds see; the sub-step rule's wind bound
- **Milestone:** P0 / M6 (wind and Coriolis)
- **Context document:** `docs/DEVELOPMENT_SPEC_v0_4.md` §6 (`AtmosphereState`), §8 (multi-rate), §9.2, §13 M6, §15.5 (zero-rotation experiment), §23, §24; Planetary Civilization Simulator — Design Record v0.9, §5.4, §15.2–15.5, §25 (M5 row: "planetary circulation direction and latitude dependence"), §28
- **Related:** ADR-0001 (modes, partition, budget, §8 milestone mapping), ADR-0002 (mesh, operators, §4.2 advection, field layout), ADR-0003 (determinism, replay), ADR-0006 (sub-steps), ADR-0009 (implicit transport, coarse graph), ADR-0010 (layered atmosphere; its §7 M6 row)
- **Amends on acceptance:** ADR-0002 §4.2 (climate-mode advection, §4.7 here); closes the vector-placement question left open by ADR-0002 §9

## 1. Context

Specification §13 M6 asks for "pressure-gradient response, tangent-space
wind, Coriolis, friction and stable transport". The design record (§5.4)
asks pressure gradients, Coriolis and friction to "create large-scale
circulation", with "recognizable trade winds, mid-latitude westerlies, polar
circulation and moving weather systems". Its §25 validation target for the
wind milestone is "planetary circulation direction and latitude
dependence". The prototype oracle prescribed a three-cell wind pattern; the
oracle notes say that pattern "must come from the M5/M6 solver".

ADR-0010 §7 assigns M6 three things: surface pressure becomes prognostic,
winds replace the atmospheric share of the diffusion, and `D` is refitted
or removed. ADR-0001 adds two constraints:
- 3-D winds belong to the **fast state** (§4.1). They exist only in
  reference runs and weather windows and are never needed to rebuild the
  slow state.
- M4–M6 deliver **reference mode** for the surface, cryosphere and
  atmosphere (§8). Climate-mode parameterisations are fitted to reference
  runs (§4.3), and the first parity tests are an M7–M9 obligation.

### 1.1 What the repository has (audit, 2026-10-02, at `89dcac2`)

Available to M6:
- **A true Voronoi mesh** (ADR-0002 §4.1). Corners are circumcentres, so
  every edge is the perpendicular bisector of the segment between its two
  cell centres. This orthogonality is exactly what C-grid schemes need.
  Edges store their two cells, length and centre distance; cells store
  outward normals in their east/north basis and their corner lists.
- **Operators.** Divergence of edge fluxes is exact; the least-squares
  gradient and the two-point Laplacian are validated and converge at second
  order (ADR-0002 §9).
- **Field types.** `EdgeField<T>` and the `edge` layout exist.
- **The atmosphere** (ADR-0010). N equal-mass σ layers in `float64`,
  hydrostatic `p_s`, layer heights from the hypsometric equation, sea-level
  pressure as a diagnostic, and a coupled implicit column. No Coriolis or
  rotation rate is used anywhere in the physics yet; `PlanetParameters`
  carries the sidereal period.
- **Transport.** ADR-0009's implicit Newton, with a multigrid-preconditioned
  conjugate-gradient inner solve, on the graph one level coarser (§12, accepted). It
  carries the column-mean θ_c with the heat entering the bottom layer
  (ADR-0010 §11).
- **Modes.** The scheduler runs climate sub-steps and 10-tick reference
  steps. Reference steps already run the column physics at instantaneous
  insolation.

Missing for M6:
- **Partition and layout.** `FastState` is an empty struct with lazy
  allocation; nothing has ever lived in it. There is no layered edge layout.
- **Mesh geometry.** No edge-to-corner map, dual-cell (triangle) areas,
  kite areas or tangential-reconstruction weights are stored.
- **Solvers.** The linear solver is CG, so it is symmetric only. Advection
  makes the transport Jacobian non-symmetric.
- **Prior decisions to revisit.**
  - ADR-0002 §4.2 prescribes semi-Lagrangian advection in climate mode.
    A month's trajectory at 10 m/s is 26,000 km, so that choice does not
    survive a monthly step (§3.5).
  - ADR-0002 §9 leaves open where vector fields live.

Budget (ADR-0001 §5, 4 workers):
- L5: 250 years in 146 s against a 240 s gate.
- L6: 447 s against a 600 s gate, so climate-mode winds may cost at most
  about 150 s over 3,000 sub-steps, about 50 ms per sub-step at L6.

### 1.2 The time-scale problem

Geostrophic adjustment takes about a day and surface friction a few days.
A climate sub-step is a month, so winds cannot be integrated in climate
mode. At L6, an explicit core needs steps of about two minutes (the
external gravity wave at ~320 m/s across ~80 km). That is ADR-0001 §1's
three orders of magnitude over budget. In climate mode the wind must
therefore be the **balanced, time-mean circulation** of the month, derived
from the slow state. That is what ADR-0001 §4.2 means by "derives `X` from
the resolved circulation".

A balanced *linear* model is not enough on its own.
- Take frictional-geostrophic balance in every layer (pressure gradient,
  Coriolis and Rayleigh drag, as in ocean planetary-geostrophic models).
  Its zonal mean gives `v̄ = −r ∂_yΦ / (r² + f²)` and `ū = −f ∂_yΦ / (r² + f²)`.
- Warm tropical columns raise the upper-level geopotential at the equator.
  Upper air therefore flows poleward, mass continuity sends the return
  flow equatorward at the surface, and surface pressure rises at the
  poles.
- The result is surface **easterlies at every latitude** and a single
  overturning cell. That leaves a net torque on the planet, which violates
  angular-momentum balance.

On Earth, surface westerlies exist because transient eddies converge
westerly momentum into mid-latitudes. A climate mode without resolved
eddies must parameterise that flux, and the parameterisation must be fitted
to something that resolves eddies. That something is reference mode.

## 2. Decision drivers

- **D1 — Emergent, not prescribed** (specification §1, §9.2; oracle).
  - No wind pattern, belt latitude or cell count is imposed.
  - Trades, westerlies and Hadley edges come from pressure gradients,
    Coriolis, friction and fluxes with documented physics.
  - Every fitted constant has a physical meaning and an invariant assertion
    (specification §23, lesson 2).
- **D2 — Conservation.**
  - Atmospheric mass is exact to rounding in every mode.
  - Climate mode keeps ADR-0007's V2 energy gate.
  - Reference mode meets ADR-0001 V1 (drift < 0.1 % per century). Dissipated
    kinetic energy returns as heat.
  - Global angular momentum is diagnosed. In a steady state the surface
    torque must vanish.
- **D3 — Two paths, one physics** (ADR-0001 §4.3). The reference core is
  the ground truth: climate mode's eddy closures are fitted to it and
  compared with it.
- **D4 — Tangent, on this mesh** (specification §9.2). Velocities stay in
  the sphere's tangent plane by construction. The operators keep the
  mesh's exact discrete conservation and must not inherit the pentagon-ring
  inconsistency of ADR-0002 §9.
- **D5 — Inside the budget.** About 50 ms per climate sub-step at L6 (§1.1).
  Reference mode has no budget (ADR-0001 §5), but CI must run it at L4.
- **D6 — Determinism.** Bit-identical for any worker count, in both modes.
  A climate step is a function of the slow state (ADR-0010 M5-04: no warm
  start is carried across steps).
- **D7 — Interfaces for M7–M11.**
  - Humidity (M7) needs layer mass fluxes and vertical motion.
  - Precipitation (M8) needs ascent, including orographic ascent.
  - Ocean currents (M11) need surface stress.
  - Cyclones (M15) need layer shear.
  - All of these are needed in both modes.

## 3. Options considered

### 3.1 Where the wind lives

**A. Prognostic winds in the slow state, every mode.** Rejected: too costly
(§1.2) and contrary to ADR-0001 §4.1.

**B. Prognostic winds in the fast state in reference mode, and a balanced
diagnostic circulation in climate mode — chosen.** This is ADR-0001 as
written. Climate-mode winds are *derived* fields, recomputed each sub-step
from the slow state and never persisted. Reference mode starts its fast
winds from that balanced state.

**C. A diagnostic wind in both modes.** Rejected:
- there would be no eddies and no moving weather systems (design §5.4);
- there would be nothing to fit climate mode to (D3);
- weather windows (M10–M12) need a prognostic core anyway.

### 3.2 Where velocities live on the mesh (closes ADR-0002 §9)

**A. Cell centres (A-grid), east/north components.** Uses the existing
gradient, and NICAM shows it works at scale. Its drawbacks:
- the A-grid's computational pressure modes need divergence damping;
- cell-to-edge reconstruction shares the 2 % pentagon-ring inconsistency
  that ADR-0002 §9 measured and declined to adopt.

**B. Edge-normal velocities (C-grid), the TRiSK scheme — chosen**
(Thuburn et al., 2009; Ringler et al., 2010; MPAS):
- it needs exactly what the mesh is, an orthogonal Voronoi–Delaunay pair;
- mass is conserved by the existing divergence;
- the Coriolis term conserves energy;
- potential vorticity is consistent;
- steady geostrophic modes are exact;
- the velocity is a scalar per edge, so it is tangent by construction (D4).

East/north cell winds are derived by Perot's reconstruction for output,
coupling and the specification §6 fields.

Known risk: TRiSK's tangential reconstruction is not consistent on general
geodesic grids (Peixoto, 2016). The centroidal optimisation helps, and V2
measures it the way ADR-0002 §9 measured the Laplacian.

**C. Vertex (B-grid) or triangular velocities.** No conservation advantage
here, and a second staggering. Rejected.

### 3.3 Reference-mode dynamics

The hydrostatic primitive equations, on the σ layers ADR-0010 already
defines (Lorenz staggering: T and u at layer centres, σ̇ at interfaces):
- **horizontal discretisation:** vector-invariant momentum with TRiSK;
- **vertical discretisation:** the mass- and energy-conserving σ
  differencing of Arakawa and Suarez (1983);
- **continuity:** flux form, so `p_s` is prognostic and mass is exact.

This is the dynamical core of a simple GCM. Held and Suarez (1994) define
its benchmark. Phillips (1956) obtained a Ferrel cell and surface
westerlies with two levels, so three layers suffice to resolve baroclinic
eddies at L5 (~220 km cells against a ~4,000 km eddy wavelength).

Rejected alternatives:
- a non-hydrostatic core: there is no use for it below ~10 km;
- spectral transform: it does not live on this mesh;
- semi-implicit gravity waves: they need a global elliptic solve every
  dynamics step, whereas the explicit core needs none. Reference mode has
  no budget, so it is deferred to weather windows, where cost matters.

### 3.4 The climate-mode circulation

**A. Frictional-geostrophic balance in every layer, on the mesh.** Local
and cheap, but it yields surface easterlies everywhere and no
angular-momentum constraint in the tropics (§1.2). Rejected alone.

**B. Integrate the reference core for a few days each sub-step.** At L6
this costs more than 10¹¹ cell updates over 250 years (ADR-0001 §1), and a
few days do not give monthly statistics. Rejected.

**C. A statistical–dynamical circulation — chosen.** This is the lineage of
CLIMBER-2 (Petoukhov et al., 2000), Aeolus (Coumou et al., 2011) and the
MIT two-dimensional model (Sokolov and Stone, 1998). The circulation is
split into its zonal mean and the departures from it:
- **Zonal mean:** an axisymmetric model on fixed latitude bands and the N
  layers.
  - It conserves absolute angular momentum except where surface drag and
    eddy momentum fluxes act. Hadley cells, the subtropical jet and
    angular-momentum-limited cell edges therefore emerge (Held and Hou,
    1980).
  - Transient eddies enter as down-gradient fluxes of heat and potential
    vorticity. Their diffusivity scales with the temperature gradient
    (Green, 1970) and has one fitted coefficient. The momentum flux
    follows from the potential-vorticity flux, so the surface westerlies
    the eddies drive are not imposed.
- **Azonal departures:** frictional-geostrophic balance on the mesh's
  edges, per layer (§4.6). This is Gill's (1980) damped response away from
  the equator, which gives monsoon lows, continental highs and
  Walker-like tropical cells. Its zonal mean is zero by construction. It
  therefore adds no net torque where the drag is uniform along a latitude;
  land–ocean drag differences leave a small torque, which V9 measures.

**D. A learned emulator of reference mode.** ADR-0001 §3 option D:
premature. Rejected for P0.

### 3.5 Climate-mode transport by the circulation

ADR-0002 §4.2's semi-Lagrangian advection fails at a monthly step: a
trajectory of a month circles the planet several times. The balanced
circulation's time-mean mass fluxes are instead applied by **implicit
(backward-Euler) flux-form upwind transport**. Over a month this is the
steady-state limit, and it is unconditionally stable. Its fluxes are
antisymmetric, so energy closes exactly as in ADR-0009.

The quantity carried is each layer's **dry static energy** `s = c_p T + Φ`,
not θ:
- the energy flux of the overturning, `Σ_k F_k s_k`, is the small
  difference between large poleward and equatorward branches (Neelin and
  Held, 1987);
- carrying s makes that difference exact.

Transient eddies keep ADR-0009's implicit diffusion, but D becomes the
eddy closure's diffusivity (§3.4 C). It is small where gradients are weak,
so the tropics are left to the Hadley cell. That avoids the double counting
of specification §23 lesson 1.

## 4. Decision

### 4.1 State and fields

No slow field is added, and PSNAP stays at schema 5. New registry group
`0x0006`:

| Field | Partition | Layout, type | Unit |
|---|---|---|---|
| `atmosphere_edge_normal_wind_m_s` | fast | edge × N, `float64` | m/s |
| `atmosphere_eastward_wind_m_s` | derived | cell × N, `float32` | m/s |
| `atmosphere_northward_wind_m_s` | derived | cell × N, `float32` | m/s |
| `atmosphere_vertical_mass_flux_kg_m2_s` | derived | cell × N (top of each layer; top = 0), `float32` | kg/m²/s |
| `sea_level_pressure_Pa` | derived | cell, `float32` | Pa |
| `surface_wind_stress_east_N_m2`, `_north_N_m2` | derived | cell, `float32` | N/m² |
| `climatology_surface_eastward_wind_mean_m_s`, `_northward_` | climatology | cell × 12 | m/s |
| `climatology_sea_level_pressure_mean_Pa` | climatology | cell × 12 | Pa |

Notes:
- **Layout.** A new `edge_layers` layout is layer-major `[layer][edge]`.
  It is scenario-layered like ADR-0010's fields.
- **Precision.** The fast wind is `float64`, against ADR-0002 §4.4's
  `float32` default. It is integrated together with `float64` temperatures
  and surface pressure, and a single precision keeps the energy budget free
  of conversion rounding. Memory at L6 with five layers is 4.9 MB.
- **Surface pressure** stays slow state, as in ADR-0010.
  - Reference mode evolves it prognostically by continuity.
  - Climate mode replaces it each sub-step with the balanced `p_s` of
    §4.6. Total mass stays exactly constant.
  - So `p_s` is continuous across mode switches. This refines ADR-0010 §7's
    "p_s prognostic".
- **Rotation.** `Ω = 2π / sidereal_rotation_period_s`, and
  `f = 2Ω sin φ` at edges and corners.

### 4.2 Mesh geometry for the C-grid

These are derived once per mesh, alongside the existing geometry. The
mesh generator version does not change, because cell geometry and order are
untouched.
- Per edge: its two corners, ordered so that the tangent is `k × n`.
- Per corner: its three cells and three edges, and its dual-triangle
  area.
- Per edge–cell pair: the kite areas.
- The TRiSK weights `W_ee'` (Thuburn et al., 2009), checked against
  their antisymmetry identity.

The operators:
- the tangential velocity `u⊥` (TRiSK);
- relative vorticity on dual triangles (circulation over area);
- kinetic energy at cells (the TRiSK form);
- the Perot reconstruction of the east/north cell vector.

### 4.3 Reference mode: the dynamical core

Per layer k (Lorenz grid, equal-mass σ layers of ADR-0010):

```text
∂u_e/∂t = − [q F⊥]_e − ∂_n(K + Φ)_e − [R_d T ∂_n ln p_s]_e − [σ̇ ∂u/∂σ]_e + D_e
∂p_s/∂t = − Σ_k Δσ_k div(p_s u_k)
∂(p_s T_k)/∂t = − div(p_s T_k u_k) − [vertical flux] + κ T ω / σ + Q_k / c_p   (Arakawa–Suarez)
```

Definitions: `q = (ζ + f) / (Δσ p_s / g)` is the potential vorticity at
corners, `F` is the layer's edge mass flux, and `Φ` comes from
hydrostatics. Implementation choices:
- **Time stepping.** RK3 (Wicker and Skamarock, 2002).
- **Sub-steps.** The count per 10-tick reference step is a function of the
  mesh alone: the smallest centre distance, a 350 m/s wave speed and
  100 m/s of wind. A step that exceeds the wind margin fails loudly; it
  never adapts to wall clock (ADR-0001 §4.4).
- **Dissipation `D_e`.** ∇⁴ hyperviscosity on divergence and vorticity
  (MPAS form), with a grid-scale damping time of a few hours (V3 fixes
  it).
- **Surface drag.** In the bottom layer, `τ = ρ_s C_D |V| V`, with C_D for
  land and ocean tiles weighted by their fractions.
- **Frictional heating.** Kinetic energy removed by drag or
  hyperviscosity enters the layer's heat, so total energy
  (`c_p T + K` plus surface geopotential) closes up to the RK3 truncation.
  D2 gates that by drift.
- **Physics coupling.** The column physics (ADR-0010 §4.4) runs once per
  reference step, as now, and supplies `Q_k`. The diffusion of ADR-0009
  is switched off in reference mode, because the core resolves the
  transport.
- **Start.** Fast winds start from the climate-mode balanced circulation
  (§4.6), normal components at the edges. Switching to climate mode
  releases them.

### 4.4 Climate mode: what the circulation provides

Each sub-step, before the coupled column and transport solve:
1. Zonal means of layer temperature, `p_s` and geopotential on fixed
   latitude bands. The band count is independent of mesh level, so the
   circulation does not change with resolution.
2. The zonal-mean circulation (§4.5): `ū_k(φ)`, the meridional mass flux
   `ψ_k(φ)` and the eddy diffusivity `D_e(φ, k)`.
3. The azonal circulation and balanced `p_s` (§4.6).
4. The layer edge mass fluxes `F_ke` (the zonal-mean overturning mapped to
   edges, plus the azonal part) and the vertical fluxes from discrete
   continuity, cell by cell. Mass then closes exactly per cell.
5. The derived winds, sea-level pressure and surface stress. The monthly
   climatology accumulates them.

Steps 2–4 run on the coarse mesh of ADR-0009 §12. That mesh is a
`PlanetMesh` one level down, so the C-grid geometry of §4.2 applies to it.
Fluxes act on the agglomerated graph, as the transport already does.

### 4.5 Zonal-mean circulation

- **The model.** An axisymmetric primitive-equation model on the bands and
  the N layers.
  - Momentum conserves absolute angular momentum
    `M = (Ω a cos φ + u) a cos φ`, except for surface drag (bottom layer,
    the drag of §4.3 linearised) and eddy momentum-flux convergence.
  - Its heating is the zonal-mean diabatic heating that the column physics
    computes from the current slow state.
  - It gives the zonal-mean surface-pressure profile that balances its
    surface wind. With §4.6's azonal departure, that profile makes up the
    balanced `p_s`.
- **The eddy closure.**
  - Heat and potential vorticity are diffused down-gradient with
    `D_e = c_e |∂θ̄/∂y|` (Green, 1970).
  - The momentum flux follows from the potential-vorticity flux through the
    Taylor identity.
  - `c_e` is the fitted coefficient. It replaces `D` as the second
    calibration constant beside `τ₀`.
- **Solution.** The model is solved to its steady state for the sub-step
  from a state defined by the current slow state (thermal-wind balance with
  zero surface wind). Nothing is carried across steps (D6).
- **Torque.** The steady state's global surface torque is reported and
  gated (V9).

**Deferred to M6-04, by amendment:** the solution method and the discrete
closure.
- The candidates are a Kuo–Eliassen elliptic diagnosis of `ψ` and an
  explicit march to quasi-steady state; both are well defined.
- The choice is made against reference-mode zonal means (V6 data). The
  equatorial inertial-stability limit is where the candidates differ.
- The amendment records the equations, the band count and the measured
  cost before the climate path is switched on.

### 4.6 Azonal circulation and balanced surface pressure

On each edge and layer the departures from the zonal mean satisfy a
frictional-geostrophic balance. It is local to the edge, because both
gradient components are compact on the Voronoi pair:

```text
r_k u_n − f u_t = − ∂_n Φ'_k − R_d T_k ∂_n ln p_s'
r_k u_t + f u_n = − ∂_t Φ'_k − R_d T_k ∂_t ln p_s'      (∂_t from corner values)
```

The drag `r_k` is large in the bottom layer (linearised surface drag) and
small aloft. Aloft it is a damping time of a few days, the Gill (1980)
regularisation that keeps the equator finite. Both are documented
constants.

**Balanced `p_s`.** The monthly-mean column mass divergence vanishes:
`Σ_k div(F_k) = 0`. That gives one elliptic equation for the azonal
surface-pressure departure:
- The friction part is the two-point Laplacian.
- The Coriolis part telescopes to a small β term, so the operator is
  non-symmetric. The solver is BiCGSTAB with the existing multigrid as
  preconditioner, using block-ordered reductions.
- Its null space, a constant, is fixed by holding total mass exactly.
- The resulting `p_s` is written to the slow state.

**Energy.** Moving mass between columns carries its layers' dry static
energy, and the budget books it as transport, summing to zero.

### 4.7 Climate-mode transport

ADR-0009's outer Newton is kept, with its response callback and the
column's slopes.
- **The operator.** It becomes advection–diffusion:
  - the overturning and azonal mass fluxes carry upwind dry static energy
    between columns (§3.5);
  - `D_e` diffuses θ̄_c, with heat entering the bottom layer as in
    ADR-0010 §11.
- **Linear solver.** The Jacobian is non-symmetric, so the inner solve
  becomes BiCGSTAB with the same multigrid preconditioner.
- **Mass fluxes.** They are fixed for the step, from §4.4, so the transport
  is linearly implicit in the fluxes and fully implicit in temperature.
- **Diagnostics.** The consistency residual and ADR-0009's diagnostics are
  kept. The northward energy transport is split into overturning,
  stationary-azonal and eddy parts.

This amends ADR-0002 §4.2: in climate mode, tracer advection is implicit
flux-form upwind on the balanced mass fluxes, not semi-Lagrangian.
Reference mode keeps flux-form upwind with a slope limiter, as ADR-0002
prescribes. M7's humidity uses the same two paths.

### 4.8 Presets and calibration

| Preset | Winds | Transport |
|---|---|---|
| `dead_rock`, `aqua_planet` | none (N = 0) | none |
| `earth_like` | both modes | climate: overturning + azonal + `c_e` eddies; reference: resolved |

- **Fit.** `τ₀` and `c_e` are fitted jointly to the ADR-0010 §4.6
  targets: 288 ± 0.5 K and a 42 ± 1 K equator-to-pole difference, at L4,
  seed 1, N = 3. The record follows specification §24.
- **Physical constants.** `C_D` (land and ocean), `r_k` and the
  hyperviscosity are documented physical constants, not fits.
  - They are set from the literature: Held–Suarez for the drag, Gill for
    the upper damping.
  - They are checked against reference mode.
  - They are not tuned to the targets.
- **Invariant assertions.** `c_e > 0`, every `r_k > 0`, `D_e ≥ 0`, and
  the radiative damping stays positive everywhere (ADR-0010 §4.6).

### 4.9 Determinism and replay

- **Parallel work.** All of it runs over fixed blocks, with block-ordered
  reductions, in both modes. Latitude-band zonal means are block reductions
  in fixed band order.
- **Reference-mode replay.** It replays from tick 0 (ADR-0003 §3.3), so
  fast winds are re-created identically.
- **Snapshots in reference mode.** A snapshot taken during a reference run
  holds no fast state. Continuing from it restarts the winds from the
  balanced state: that is a statistically equivalent continuation, not a
  bitwise one. This follows ADR-0001 §4.1 and is stated in the run
  manifest documentation.

## 5. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | C-grid geometry: edge–corner orientation, kite areas sum to cell and triangle areas, `W_ee'` antisymmetry identity, Coriolis term energy-neutral, `div(k×∇ψ)` = 0 | exact to rounding |
| V2 | Operator accuracy L3–L7 on analytic fields: `u⊥`, vorticity, kinetic energy, Perot reconstruction; seam and pentagon maxima as in ADR-0002 §9 | L2 order ≥ 1 for each; values recorded, maxima bounded |
| V3 | Shallow water (one layer): Williamson et al. (1992) test 2 (steady geostrophic flow) and test 5 (mountain), 5 and 15 days | mass exact; test 2 normalised L2 error of h ≤ 1e-3 at L6 and converging; energy drift recorded |
| V4 | Primitive equations at rest over terrain, isothermal, no forcing: spurious winds | max ≤ 0.5 m/s after 10 days at L5 |
| V5 | Held–Suarez (1994) forcing, N = 3 and 5, L5, 1,000 days, last 800 averaged | top-layer zonal-mean jet 15–45 m/s at 25–55° in both hemispheres; bottom-layer zonal wind easterly averaged over 5–20°, westerly over 35–55°; transient eddy kinetic energy present |
| V6 | Reference mode, Earth-like, coupled to the column physics, L4–L5, 3 years | energy drift ≤ 1e-5 per year (ADR-0001 V1); mass exact; zonal means recorded as the climate-mode comparison |
| V7 | Climate mode, Earth-like, L5, last decade | bottom-layer zonal-mean wind easterly over 5–20°, westerly over 40–55° (both hemispheres, annual mean); Hadley edges at 20–40°; top-layer jet maximum at 25–50° |
| V8 | Rotation experiment (design §15.5): Ω × {0, ½, 1, 2} | Ω = 0: zonal-mean zonal wind ≤ 1 m/s and one cell per hemisphere; Hadley edge latitude decreases monotonically with Ω |
| V9 | Angular momentum: annual-mean global surface torque over the sum of its magnitudes | ≤ 0.05 in climate mode; ≤ 0.1 in reference mode over a year |
| V10 | Energy closure of every climate step with the circulation, N = 3 and 5 | ADR-0007 V2 gate |
| V11 | Determinism: workers 1/2/8/16 bit-identical in both modes; replay of the L5 performance run | bit-identical |
| V12 | Calibration: `τ₀` and `c_e` fitted; the §4.8 invariants; climate against reference zonal means (V6) | 288 ± 0.5 K, 42 ± 1 K; parity recorded, gated from M7 (ADR-0001 §8) |
| V13 | Performance: ADR-0001 250-year climate runs, N = 3, 4 workers | L5 ≤ 240 s, L6 ≤ 600 s |

V3–V5 are standard dynamical-core benchmarks. Their numeric gates are
provisional until the task that implements them measures the scheme on this
mesh. A gate may be tightened by that task, and loosened only by
amendment.

## 6. Consequences

**Positive.**
- The wind belts, Hadley edges and their response to warming and to
  rotation are emergent. That covers the §23 rows on circulation slowing and
  dry belts moving poleward, which M7–M8 make measurable.
- The reference core is the ground truth ADR-0001 requires, and the start
  of the weather-window solver (M10–M12).
- The mesh's orthogonality, chosen for ADR-0002's operators, is used to
  the full. The vector-placement question is closed.
- Humidity (M7) receives mass fluxes and vertical motion in both modes.
  Surface stress is ready for the ocean (M11) and layer shear for cyclones
  (M15).
- The diffusion's constant D becomes an eddy closure with a physical
  scaling, so the tropics and extratropics are no longer carried by one
  number.

**Negative.**
- Two circulation codes to maintain, plus their comparison. This is the
  cost ADR-0001 §7 already accepted.
- The climate-mode circulation is a reduced model with fitted eddy
  closures. It reproduces statistics, not weather, and it is research
  work (ADR-0001 §7).
- Every calibrated number moves again (`D` → `c_e`; `τ₀` refitted).
- Reference-mode snapshots do not continue bitwise (§4.9).

**Risks and mitigations.**
- *TRiSK inconsistency on the geodesic grid* (Peixoto, 2016). Measured in
  V2 and V3, as ADR-0002 §9 measured the Laplacian. If it is unacceptable,
  the fallback is Peixoto's (2016) consistent reconstruction, by amendment.
- *Three layers are too few for eddies.* Measured in V5 at N = 3 and 5.
  Climate mode's fit is then made against N = 5 reference runs.
- *The zonal-mean closure cannot reach V7 and V12 together.* The
  amendment of §4.5 is written with the data in hand.
  - The prepared fallback keeps ADR-0009's diffusion as the climate-mode
    transport and uses the balanced winds only for output and for M7's
    moisture. That is a safe but weaker M6.
  - The fallback is decided by amendment, never silently.
- *The L6 gate.* The circulation runs on the coarse mesh and is measured in
  M6-04. Non-symmetric solves converge more slowly than CG. If needed, the
  zonal-mean model runs on fewer bands.

## 7. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M6 | §4.1–4.9; V1–V13 |
| M7 | Humidity advected by the same two paths; first climate/reference parity gates (ADR-0001 §8); vapour in τ (ADR-0010 §7) |
| M8 | Ascent and orographic lift drive condensation; moist convection replaces Γ_c adjustment |
| M10–M12 | Weather windows run the §4.3 core regionally (ADR-0001 V3) |
| M11 | Surface stress drives the ocean; the ocean's heat transport forces a refit of `c_e` (specification §23 lesson 1) |
| M15 | Layer shear and vorticity for cyclone genesis |

## 8. Open questions

- **Zonal-mean solution method** (§4.5): Kuo–Eliassen or a march to
  steady state. Decided by amendment in M6-04.
- **Persisting fast winds in reference-mode snapshots.** Current position:
  no (ADR-0001 §4.1). Revisit when weather windows need a bitwise fork.
- **Stationary-wave momentum fluxes.** The azonal circulation is linear,
  so it does not feed the zonal-mean jets. Earth's northern jet partly
  depends on such fluxes. Current position: accept, and measure the
  climate–reference difference in V12.
- **Presentation.** Pressure and animated wind (design §15.3) need the
  derived fields in `StateSnapshot`. Current position: add the fields to
  the presentation snapshot in M6; the Godot view waits for a later task.

## 9. Proposed tasks

1. **M6-01 C-grid geometry and vector operators.** §4.2: the edge–corner
   maps, dual areas, kites, TRiSK weights, vorticity, kinetic energy and
   Perot reconstruction; the `edge_layers` layout. V1, V2. No physics.
2. **M6-02 Shallow-water core.** One layer: TRiSK momentum, RK3, the
   sub-step rule, hyperviscosity. Williamson tests 2 and 5. V3, V11 for the
   core.
3. **M6-03 Primitive equations in reference mode.** §4.3:
   - the N-layer core, flux-form `p_s`, Arakawa–Suarez vertical
     differencing, surface drag and frictional heating;
   - fast-state allocation and mode switches;
   - Held–Suarez, then coupling to the column physics.
   - V4, V5, V6, and V9 in reference mode.
4. **M6-04 Climate-mode balanced circulation.** §4.4–4.6:
   - the zonal-mean model (its amendment first), the azonal balance and
     the balanced `p_s`;
   - the derived fields, climatology and presentation fields;
   - the first L6 timing.
   - V7 provisional, V8, V9.
5. **M6-05 Transport by the circulation, refit and close.** §4.7–4.8:
   - advection–diffusion in the implicit transport, with BiCGSTAB;
   - the `τ₀` and `c_e` fit;
   - the performance gates and the records.
   - V7, V10, V12, V13.

## 10. References

- Arakawa, A. and Suarez, M. J. (1983). Vertical differencing of the primitive equations in sigma coordinates. *Mon. Wea. Rev.* 111.
- Coumou, D., Petoukhov, V. and Eliseev, A. V. (2011). Three-dimensional parameterizations of the synoptic scale kinetic energy and momentum flux in the Earth's atmosphere. *Nonlin. Processes Geophys.* 18.
- Gill, A. E. (1980). Some simple solutions for heat-induced tropical circulation. *Q. J. R. Meteorol. Soc.* 106.
- Green, J. S. A. (1970). Transfer properties of the large-scale eddies and the general circulation of the atmosphere. *Q. J. R. Meteorol. Soc.* 96.
- Held, I. M. and Hou, A. Y. (1980). Nonlinear axially symmetric circulations in a nearly inviscid atmosphere. *J. Atmos. Sci.* 37.
- Held, I. M. and Suarez, M. J. (1994). A proposal for the intercomparison of the dynamical cores of atmospheric general circulation models. *Bull. Amer. Meteor. Soc.* 75.
- Neelin, J. D. and Held, I. M. (1987). Modeling tropical convergence based on the moist static energy budget. *Mon. Wea. Rev.* 115.
- Peixoto, P. S. (2016). Accuracy analysis of mimetic finite volume operators on geodesic grids and a consistent alternative. *J. Comput. Phys.* 310.
- Petoukhov, V. et al. (2000). CLIMBER-2: a climate system model of intermediate complexity. Part I. *Climate Dynamics* 16.
- Phillips, N. A. (1956). The general circulation of the atmosphere: a numerical experiment. *Q. J. R. Meteorol. Soc.* 82.
- Ringler, T. D., Thuburn, J., Klemp, J. B. and Skamarock, W. C. (2010). A unified approach to energy conservation and potential vorticity dynamics for arbitrarily-structured C-grids. *J. Comput. Phys.* 229.
- Sokolov, A. P. and Stone, P. H. (1998). A flexible climate model for use in integrated assessments. *Climate Dynamics* 14.
- Thuburn, J., Ringler, T. D., Skamarock, W. C. and Klemp, J. B. (2009). Numerical representation of geostrophic modes on arbitrarily structured C-grids. *J. Comput. Phys.* 228.
- Wicker, L. J. and Skamarock, W. C. (2002). Time-splitting methods for elastic models using forward time schemes. *Mon. Wea. Rev.* 130.
- Williamson, D. L. et al. (1992). A standard test set for numerical approximations to the shallow water equations in spherical geometry. *J. Comput. Phys.* 102.

## 11. Implementation record

- **M6-01 (2026-10-02).** Covers §4.2, V1 and V2.
  - `CGridGeometry` is built from the mesh, which is unchanged.
  - Every TRiSK identity of V1 holds to 4e-16 at L2–L7.
  - The kites tile the cells and dual triangles to 1.7e-12 at L6. That
    residual is the rounding of the mesh's circumcentres and grows as 4ᴸ
    (§12).
  - Details are in `docs/tasks/M6-01-c-grid-geometry-and-operators.md`.
- **M6-02 (2026-10-02).** Covers the one-layer core of §4.3, V3 and V11.
  - Williamson test 2 converges at second order in L2, with an error of
    3.5e-5 at L6 (gate 1e-3). Its maximum error stalls at 5e-4, as §12
    predicted. **This decides §12: TRiSK is kept.**
  - Test 5 conserves mass exactly and energy to 5e-8 over 15 days at L5.
  - The energy error is RK3's, of third order in Δt.
  - The hyperviscosity constant follows the measured fastest mode of the
    vector Laplacian (λ d̄² ≈ −29, rotational), not the scalar 8.
  - Details are in `docs/tasks/M6-02-shallow-water-core.md`.
- **M6-03 (2026-10-02).** Covers §4.3, V4–V6, V9 in part and V11.
  - **Energy.** The vertical discretisation conserves total energy exactly
    in space, to 1e-17, including the heat returned by dissipation.
  - **V4.** An isothermal atmosphere at rest stays at rest to 8e-11 m/s;
    the logarithmic edge θ̂ is what makes it exact.
  - **V5, Held–Suarez at L5.**
    - With N = 3: jets of 30 m/s at 37.5° over trades, westerlies and
      polar easterlies.
    - With N = 5: jets of 35 m/s at 47.5°, surface westerlies of 8 m/s and
      trades of −4 m/s, close to the many-level benchmark.
  - **V6.** Reference mode on the Earth-like planet conserves mass to
    6e-15. After spin-up, energy drifts by 4e-8 per year. The spin-up from
    rest costs 9e-5 of the energy in its first month; M6-04's balanced
    start removes it.
  - Amendment §13 records the smoothed orography and the 150 m/s wind
    bound.
  - Details are in `docs/tasks/M6-03-primitive-equations.md`.

## 12. Amendment: V2 as measured for the TRiSK operators (accepted 2026-10-02)

**Finding (task M6-01).** On the analytic field `u = ∇g + r × ∇h`, L3–L7:

| Operator | L6 relative L2 | L2 order L4→L5, L5→L6, L6→L7 | relative max, L4–L7 |
|---|---|---|---|
| Perot reconstruction | 1.7e-4 | 2.00, 2.00, 2.00 | converges (order ≥ 1) |
| Normal gradient | 8.4e-5 | 1.99, 1.99, 1.99 | converges |
| Tangential gradient (barycentric corners) | 2.9e-4 | 1.97, 1.98, 1.98 | converges |
| TRiSK tangential velocity `u⊥` | 3.6e-4 | 1.89, 1.70, 1.40 | 7.6e-3 → 6.8e-3, stalls |
| Kinetic energy | 6.2e-4 | 1.62, 1.34, 1.15 | 1.7e-2 → 1.6e-2, stalls |
| Vorticity at corners | 5.2e-3 | 0.92, 0.93, 0.94 | 6.1e-2 → 6.2e-2, stalls |

- **Where the errors are.** The maxima that stall sit in the first ring
  of hexagons around the pentagons. The icosahedron's seams carry no more
  error than the interior. The TRiSK perp operator and kinetic energy are
  not consistent there, which is Peixoto's (2016) finding, the risk named
  in §6.
- **Why the L2 orders fall.** The pentagon rings are a fixed number of
  cells, so as they shrink the L2 order tends to one.
- **Vorticity.** It is first order. The circulation divided by `A_v` is
  the mean over the dual triangle, but it is compared with the value at
  the circumcentre, which is not the triangle's centroid.
- **The original gate.** V2's "L2 order ≥ 1 for each" therefore holds
  for the three consistent operators. It fails for the vorticity (0.93),
  and for the other two it holds now but not in the limit.

**Change.**
- **Consistent operators** (reconstruction, normal and tangential
  gradient): L2 order ≥ 1.5 and maximum order ≥ 0.9 at every step from
  L3 to L6.
- **TRiSK operators** (`u⊥`, kinetic energy, vorticity):
  - L2 order ≥ 0.9 from L3 to L6;
  - from L4 on, maxima bounded: `u⊥` ≤ 1e-2, kinetic energy ≤ 3e-2,
    vorticity ≤ 8e-2;
  - L6 L2 errors ≤ 5e-4, 8e-4 and 6e-3;
  - seam maxima at most twice the interior's.
- **Acceptability** is decided by V3. TRiSK's conservation properties (V1)
  are exact, and what the dynamics needs is that shallow-water solutions
  converge, not the operators pointwise. That is how ADR-0002 §9 treated
  the two-point Laplacian. If Williamson test 2 does not converge at order
  ≥ 1 in M6-02, the §6 fallback (Peixoto's consistent reconstruction) is
  adopted by a further amendment.
- **Kite-area gate.** The V1 gate becomes `1e-15 · 4ᴸ` relative.
  - The mesh's circumcentre is the normalised cross product of two
    differences of length h, so it is accurate to ε/h. The tiling's
    misfit then grows as 4ᴸ: 3.7e-13 at L5, 1.7e-12 at L6.
  - The TRiSK weights take their kite fractions over the kite sum, so
    they stay antisymmetric to 4e-16 regardless.

## 13. Amendment: the orography the winds see, and the wind bound (accepted 2026-10-02)

**Finding (task M6-03, step A).** The isothermal rest state is exact (V4).
A stratified atmosphere at rest (T = 288 K − 6.5 K/km) over the generated
Earth-like terrain is not:
- At L5 with N = 3 the wind reaches 10.9 m/s after one day and 20.5 m/s
  after ten.
- N = 5 and L4 are no better. The residual is second order in the jump in
  ln p_s between neighbouring cells, the σ-coordinate pressure-gradient
  error.
- The generated terrain is steep at the grid scale. Neighbouring cells
  differ by up to 5.9, 5.0 and 3.3 km at L4, L5 and L6, mostly where an
  ocean cell meets a mountain cell.

**Change (option 1 of the three put on 2026-10-02).**
- **What sees the smoothed heights.** The dynamics (§4.3, and the
  balanced circulation of §4.4–4.6) uses a smoothed copy of the surface
  height, while the column physics, the surface tiles and ADR-0010's
  hydrostatic initialisation keep the true heights. This is the standard
  practice of GCMs, whose dynamics see filtered orography.
- **The filter.** Conservative two-point averaging passes,
  `z_i ← z_i + (1/A_i) Σ_e (1/8) l_e d_e (z_j − z_i)`.
  - Each pass is a convex combination, so it creates no new extremes.
  - The coefficients are symmetric, so the area-weighted mean height is
    exact.
- **How many passes.** As few as leave no two neighbouring cells more than
  800 m apart (`limit_dynamics_orography_steps`), at most 256. The rule
  depends on the terrain and the level only, so it is deterministic.

| Level | Passes | Highest peak seen | RMS change | Stratified rest, max wind after 10 days |
|---|---|---|---|---|
| L4 | 8 | 2,595 m (true 6,337) | 378 m | 0.20 m/s |
| L5 | 13 | 2,906 m (true 6,616) | 297 m | 0.25 m/s |
| L6 | 19 | 3,916 m (true 6,842) | 221 m | 0.55 m/s |

**Consequences.**
- The winds feel mountains at most about 3–4 km high. The generated
  peaks are narrow, often one cell, and resolved relief survives better at
  finer levels.
- Orographic effects of later milestones (rain shadows, M8) act on the
  smoothed heights for the flow, but the true heights for the columns.
- In reference mode, `p_s` adjusts from ADR-0010's true-height
  hydrostatic initial state towards balance with the smoothed orography.
  Task M6-03 step B measures that adjustment.
- **Gate.** The stratified rest state may reach at most 1 m/s after 10 days
  at L4–L6 (recorded with `planet_cli dynamics`). The isothermal V4 gate is
  unchanged.

**The wind bound of the sub-step rule (§4.3)** rises from 100 to
150 m/s:
- A 5-layer Held–Suarez spin-up at L5 reached 100.85 m/s in its first
  baroclinic burst, and the rule stopped it, as designed.
- Earth's jets exceed 100 m/s at times.
- The cost is about 11 % more steps.

