# ADR-0024 — The climate transport's eddies driven by moist enthalpy

- **Status:** Accepted
- **Date:** 2026-10-18
- **Accepted:** 2026-10-18
- **Amended:** 2026-10-21 — §9 (accepted with the user): an unconverged coupled month falls back to the diffusive transport (amends ADR-0011 §17's fourth decision; investigation INV-03)
- **Milestone:** P0 / M8 (task M8-05)
- **Context documents:** `docs/DEVELOPMENT_SPEC_v0_8.md` §13 M8, §23, §24; ADR-0023 §5 V11
- **Related:** ADR-0009 (the implicit transport's Newton, its floors and line search, §12's coarse groups), ADR-0010 §11 (the source enters the bottom layer), ADR-0011 §17 (the coupled climate transport: eddies on θ_c, advection of s_k), ADR-0021 §4.5 (humidity carried by the same fluxes and eddies), ADR-0023 (moist convection, cloud water, precipitation)
- **Amends:** ADR-0011 §17.1 (what drives the eddies' heat, with moist convection on); ADR-0009 §4.1 and §11 (the diffused temperature, in the fallback month, with moist convection on)

## 1. Context

ADR-0023's physics cannot run at L6 (task M8-05, V11). In the first
climate month, before any cloud forms, the coupled transport's Newton
(ADR-0011 §17.1) does not converge in 30 iterations. Its residual stays
near 7 000 W/m², and the source it then applies breaks a column
(−1 216 W/m², a bottom layer at 5 K). L5 runs: 250 years in 345 s. M7's
physics runs at L6.

**The cause, measured.** The transport's unknown is one heat source h per
coarse group, entering the bottom layer. The eddies diffuse the column
mean potential temperature θ_c, and Newton's matrix and its preconditioner
(ADR-0009's) take dθ_c/dh > 0. With moist convection a column's θ_c is not
monotone in h. Two L6 columns, scanned over h at the month's start:

| Cell 2634, h (W/m²) | 0 | 140 | 160 | 180 | 200 |
|---|---|---|---|---|---|
| θ_c (K) | 334.5 | 345.4 | 340.9 | 338.2 | 340.9 |
| Precipitation (mm/day) | 4.52 | 1.57 | 0.76 | 0.16 | 0.11 |
| Layer 2 (K) | 256.1 | 268.9 | 262.6 | 258.5 | 259.3 |

Cell 3857 does the same between 120 and 160 W/m². Heating a convecting
column raises Betts–Miller's reference humidity, so its drying, the
convective rain, falls. Across the band the rain lost, about 40 W/m² of
latent heat released in the upper layers, matches the heat added at the
bottom. θ_c weights the upper layers by 1/Π, so it falls by 7 K. A column
that stops raining keeps the added energy as vapour, which θ_c cannot see:
the transport sees a colder column and sends it more heat. Where such a
column's group sits in the band, the transport is anti-diffusive, and no
Newton converges. L3–L5's groups hold several columns and average the
bands out; L6's do not.

**What is monotone.** Moist convection, the condensation pass and the
microphysics conserve each column's moist enthalpy Σ m_k (c_p T_k + L q_k)
(ADR-0023 V2), and rain leaving as liquid changes nothing in it. The
column's moist enthalpy therefore changes only by the source, the
radiation and the surface's fluxes. All three are evaluated at the
implicit solve's temperatures, where the column's Jacobian is an M-matrix,
so the moist enthalpy rises with h.

## 2. Decision drivers

1. One Newton per climate month that converges at every level, L3 to L7.
2. Energy and water close exactly, globally and per cell (ADR-0001 V1).
3. What the transport carries stays physical: the eddies carry moist
   static energy, as the atmosphere's do.
4. M7's path, and the preset while moist convection is off in it, do not
   change.

## 3. Options considered

1. **Iterate on moist enthalpy, eddies still on θ_c.** Newton's unknown
   becomes each group's moist enthalpy; the equation h = H(θ_c(h)) is
   unchanged, so is its anti-diffusive band. *Rejected* (proposed in the
   session, then withdrawn).
2. **Coarser groups at L6** (two levels, L4's). Averages the bands away
   at L6 and lowers its transport's resolution; L7 would need three.
   *Rejected* as a fix.
3. **Smooth Betts–Miller's shut-off** so that dP/dh never takes back more
   than the heat added. Changes the convection to suit a solver, and
   monotonicity is not guaranteed elsewhere. *Rejected.*
4. **The eddies carry moist enthalpy; water carries its own latent part.**
   *Chosen* (decided with the user; accepted 2026-10-18).
5. **The eddies carry moist enthalpy as heat, and no water.** Removes the
   eddies' moisture transport, most of the poleward latent flux, from the
   water cycle. *Rejected.*

## 4. Decision

With moist convection on (ADR-0023), in climate mode:

1. **The driving temperature.** Each column's moist enthalpy temperature
   after its physics,

   T_m = (1/N) Σ_k (T_k + L q_k / c_p),

   on equal-mass layers (q_k the vapour after convection and the
   microphysics; L the column's latent heat, as the condensation's). A
   group's T_m is its cells' area- and mass-weighted mean, as θ_c's is.
2. **The eddies' energy.** Across an edge the eddies carry
   G (T_m,a − T_m,b), with ADR-0011 §17.1's conductance G = c_p μ̄ D̄_e.
3. **The latent part travels as water.** The humidity transport
   (ADR-0021 §4.5) runs first, as now, and its eddy part moves the water
   G/(c_p N) (q′_a,k − q′_b,k) per layer, with q′ its implicit solution.
   Its latent heat, L times each group's eddy water convergence (W/m²), is
   fixed for the step: E_a.
4. **The heat source.** The transport's source is the moist enthalpy the
   eddies bring, less what the water brought, plus the overturning's
   advection as now:

   h_a = (1/A_a) Σ_b G_ab (T_m,b − T_m,a) − E_a + (1/A_a) Σ_b Σ_k F_k s_k,up.

   Σ_a A_a h_a = 0 exactly: each eddy term is antisymmetric, E sums to
   zero (the tracer solve conserves water), and the advection cancels as
   before.
5. **The response.** The column returns T_m and its slope
   dT_m/dh = (1/(c_p N m)) Σ_k [C_k r_k + L m q_sat′(T_k) r_k [k condensed
   in the solve]], with r_k the implicit solve's response (J r = e₀), and
   the evaporation's change neglected. The slope is positive. The layers'
   s_k and their slopes stay as now, for the advection.
6. **Newton** (ADR-0011 §17.1) is unchanged in form: one source per group,
   T_m and its slope in place of θ_c and its, BiCGSTAB with the multigrid
   on the eddies' symmetric part. The fallback month's diffusion
   (ADR-0011 §17.4) drives its D with T_m the same way.
7. **Off,** the transport is ADR-0011 §17's, bit for bit.

**How it differs from §17 where columns do not cross the band.** With q
unchanged by the column (q = q′), h_a = (1/A_a) Σ G (T̄_b − T̄_a) + advection:
the eddies see the layers' mean temperature, not θ_c, about a sixth less
contrast at three layers. Where a column's vapour changes in the month,
the dry heat the eddies deliver is corrected by L G Δ(q − q′)/c_p: a column
that stops raining exports more heat. τ_d and κ_v are refitted with
moist convection on (ADR-0023 §4.6); c_E stays reference mode's fit
(ADR-0011 §17.7).

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Energy: Σ A h = 0 to rounding every step; each cell's and the globe's budget close as before; water as before | unit and run tests |
| V2 | Monotone: over h in [−400, 400] W/m², a convecting column that crosses Betts–Miller's shut-off has T_m rising with h, while its θ_c does not | unit test |
| V3 | Off, the transport and the run are bit-identical to ADR-0011 §17's | regression |
| V4 | L6 and L5, 250 years with moist convection, within ADR-0023 V11's 1.5 times M7's times; the coupled Newton converges (reported: its iterations, failed months) | performance |
| V5 | The refit (288 K at L4) and V9's reports; V8 parity measured again | calibration |

## 6. Consequences

- Moist convection can be turned on in the preset again once V4 passes
  (decided with the user at that point).
- The climate transport's eddies carry moist static energy, as Frierson
  et al. (2006) do in a moist energy balance, while the water still moves
  as water.
- T_m replaces θ_c only where moist convection is on; ADR-0009's and
  ADR-0011's dry and M7 runs keep θ_c.

## 7. Tasks

Within M8-05: the response's T_m and slope, the latent term E from the
humidity transport, the source's assembly in both transport solves, the
fallback, the tests (V1–V3), then V4 and the refit (V5).

## 8. References

- Frierson, D. M. W., Held, I. M. and Zurita-Gotor, P. (2006), A gray-radiation aquaplanet moist GCM. Part I, *J. Atmos. Sci.* 63, 2548–2566.
- Betts, A. K. (1986), A new convective adjustment scheme. Part I, *Q. J. R. Meteorol. Soc.* 112, 677–691.

## 9. Amendment: an unconverged coupled month falls back to diffusion (accepted 2026-10-21 with the user, investigation INV-03)

**9.1 What fails.** With ADR-0026's moistening and INV-02's refit in the
preset, L6 stops in its first month from the initial state (INV-02).
- The coupled transport's Newton ends at 30 iterations about 2.2 kW/m²
  from consistency. Applying that unconverged source drives a column's
  bottom layer to about 5 K, and the column solve throws.
- M8-05's L6 first month also ended unconverged (§4: about 1 900 W/m²)
  and survived. What breaks now is the application, not the lack of
  convergence.
- Three solver prototypes did not meet the convergence gate (INV-03):
  ramping the transport strength from zero, blending the column
  response from moistening off to on, and a damped residual-gradient
  fallback.
- The failure is not moistening's alone: with the refitted constants
  and the old response, L6's first month still caps (2 511 W/m²).

**9.2 Decision (amends ADR-0011 §17's fourth decision).**
1. **A month whose coupled transport ends unconverged** falls back to
   ADR-0009's diffusive transport with its calibrated D. That is a
   consistency residual above R_f after its Newton, whether at the
   iteration cap or stopped by the line search. §17 already does the
   same for a month whose circulation fails.
   - With moist convection on, the fallback diffuses T_m, as ADR-0024
     §4.5's fallback does.
2. **The latent energy moves once.** In such a month the humidity
   transport has already run on the circulation's fluxes, unlike a
   failed circulation month, where no humidity moves. So the eddies'
   latent heat E, which moved as water, comes off the fallback's heat as
   it comes off the coupled transport's (§4).
3. **R_f = 100 W/m²,** to be confirmed by V2 below. ADR-0009's diffusion
   converges to its own gate in every month measured.
4. **Determinism:** the fallback depends on the state alone (the
   residual of a deterministic solve), so runs stay bit-identical across
   worker counts and on continuing from a snapshot (§17's fourth
   decision). Each fallback month is counted and reported, as the failed
   circulation months are.

**9.3 The prototype** (INV-03; it does not yet subtract E). L6, the
refitted preset, R_f = 100 W/m², ten years from the initial state:
- 9 of 120 months fell back, 4 and 3 in the first two years as the
  planet cooled from its hot start, then none until one each in years 7
  and 10;
- the energy closed every month (about 1e-5 of its gate), and no
  circulation month failed;
- settled months converged on their own in 4–6 iterations, to
  1e-5–1e-4 W/m².

**9.4 Validation.**

| ID | Check |
|---|---|
| V1 | With R_f off (0), every run is bit-identical to today's |
| V2 | L6, 250 years from the initial state: completes; energy and water close every month; fallback months counted. V11's time within 1.5 × M7's (2 025 s), measured on an idle machine. Rechecked after INV-03's refit: 1 061.1 s, energy 1.39e-5 and water 3.23e-4 of their gates, 10 fallback months, zero unconverged columns and zero failed circulation months. |
| V3 | L4 and L5 measured again. Their first months also exceed 100 W/m² (INV-02: up to 107 and 203 W/m²), so they change slightly; INV-02's 288 K fit and V9's targets are re-checked, and refitted only if they leave their tolerances. L4 was 288.086 K on the INV-02 pair; INV-03 refit to τ_d = 1.111, κ_v = 0.005555 m²/kg, giving 287.984 K with energy/water gates and V9 cloud targets passing. L5/250 on this pair passes in 308.3 s, with energy 1.11e-5 and water 2.29e-4 of their gates. |
| V4 | The latent energy moves once: in a fallback month, E is subtracted and the global energy closes to its gate |
| V5 | Determinism: one and four workers, and a run continued from a snapshot taken before a fallback month, end with the same state hash. INV-03 rechecked worker-count determinism on the refitted pair: two L6 years on one and four workers match at `479957516275377720` through eight fallback months. The L4 month-2 fallback case also restarts from a PSNAP at that month's start: the restored climate scheduler reproduces the fallback and matches the uninterrupted six-month hash with four versus one workers, using INV-02's L4 radiation pair. A refitted-pair L6 snapshot restart through a fallback remains unverified; `PlanetRun` has no resume constructor. |

**9.5 Consequences.**
- L6 runs from the initial state with the moistening preset.
- The fallback is a safety net, not a cure. The coupled Newton still
  fails occasionally after spin-up, usually where the line search stops
  at a kink in the columns' response (INV-03). A solver that handles the
  kinks (INV-03's damped least squares, or a smoother response) remains
  open and would make the fallback rarer, not unnecessary.
- In a fallback month the circulation's heat transport is replaced by
  diffusion, so that month's poleward heat transport is ADR-0009's. That
  was already true of §17's failed months.
- **Not changed:** the 30-iteration cap, the coupled Newton's tolerance
  and the circulation itself.

