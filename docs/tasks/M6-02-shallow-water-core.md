# Task M6-02 — Shallow-water core

- **Milestone:** P0 / M6 (second task; M6-01 is complete)
- **Scope:** ADR-0011 §4.3 for one layer:
  - the TRiSK shallow-water equations on the C-grid of M6-01;
  - RK3 time stepping and the sub-step rule;
  - ∇⁴ hyperviscosity;
  - mass, energy and potential-enstrophy diagnostics;
  - Williamson et al. (1992) tests 2 and 5.

  This is the horizontal core that M6-03 stacks into N layers. It holds no
  planetary state.
- **Governing decisions:** ADR-0011 §3.2–3.3, §4.3, V3, V11, §12 (the V3
  decision on TRiSK's accuracy)

## 1. Decisions made for this task

1. **Equations** (Ringler et al., 2010), with layer thickness h, bottom
   height b, edge velocity u and `F_e = ĥ_e u_e`, where
   `ĥ_e = (h_1 + h_2)/2`:

   ```text
   ∂h/∂t   = −div(F)
   ∂u_e/∂t = (1/d_e) Σ_e' W_ee' l_e' F_e' (q_e + q_e')/2 − ∂_n(K + g(h + b))_e − ν₄ L(L(u))_e
   ```

   The terms are:
   - `q_v = (ζ_v + f_v) / h_v` at corners, where `h_v` is the kite-weighted
     corner thickness, and `q_e` is the mean of an edge's two corners;
   - `K` is the TRiSK kinetic energy of M6-01;
   - `L(u)_e = ∂_n δ − ∂_t ζ` is the C-grid vector Laplacian, where δ is
     the cell divergence and ζ the corner vorticity.

   The Coriolis term's sign follows from `f k × u`: its normal component is
   `−f u·t`, so the tendency is `+f u⊥`.

   This is TRiSK's energy-conserving form. The symmetric PV average keeps
   the weights' antisymmetry, so the Coriolis and PV-flux term does no work
   (V1 of M6-01).
2. **Rotation.**
   - `f = 2Ω (ω̂ · x)` for a rotation axis ω̂, so test 2 can run with a
     tilted axis, as Williamson's α does.
   - The planet's axis is `+Z` (ADR-0004).
3. **Time stepping.** RK3 of Wicker and Skamarock (2002):
   `y₁ = y₀ + Δt/3 F(y₀)`, `y₂ = y₀ + Δt/2 F(y₁)`, `y₃ = y₀ + Δt F(y₂)`.
   Every stage is in flux form, so mass is exact.
4. **Sub-step rule** (ADR-0011 §4.3):
   - A span of time is cut into `n = ⌈T / Δt_max⌉` equal steps, where
     `Δt_max = C d_min / (c + U)`.
   - `d_min` is the mesh's smallest centre distance. `c` and `U` are the
     stated wave and wind bounds, and `C = 0.5`.
   - A step whose maximum wind exceeds U throws. The rule never depends on
     wall clock or on the state, apart from that check.
5. **Hyperviscosity.** `ν₄ = d̄⁴ / (30² τ)` damps the fastest mode of the
   C-grid vector Laplacian in no less than τ.
   - That mode is rotational, on the dual triangles. Its measured eigenvalue
     is −27.2/d̄² at L4 and −28.9/d̄² at L5.
   - A first form, `d̄⁴ / (64 τ)`, assumed the scalar Laplacian's 8/d̄² and
     would have damped that mode 13 times faster than stated.
   Tests 2 and 5 run with ν₄ = 0, as Williamson's tests are posed, and the
   dissipation has its own test.
6. **Diagnostics:**
   - mass `Σ A h`;
   - energy `Σ A h K + Σ A g h (h/2 + b)`;
   - potential enstrophy `Σ A_v h_v q_v² / 2`;
   - maximum wind.

   All are deterministic block reductions.
7. **The `edge_layers` layout** and the first fast field move to M6-03,
   where the N-layer wind becomes planetary state. The shallow-water model
   is a validation core with its own state, so nothing here could test
   them.
8. **Placement:** `sim/planet/dynamics/shallow_water.{hpp,cpp}`, and
   `planet_cli shallow-water` for the record.

## 2. Acceptance

- **Mass** is conserved to rounding (relative 1e-13) in every run.
- **Test 2, steady geostrophic flow**, 5 days, tilted axis:
  - the normalised L2 error of h is ≤ 1e-3 at L6;
  - the L2 order is ≥ 1 from L4 to L6. This decides ADR-0011 §12.
- **Test 5, flow over an isolated mountain**, 15 days, L5:
  - mass exact;
  - energy drift and potential-enstrophy drift recorded (no dissipation);
  - the L5 and L6 solutions agree to a recorded difference.
- **Energy:** without dissipation, the relative energy change over a day
  falls by at least a factor of 4 when Δt halves. TRiSK is exact in space,
  and RK3's error is of third order in time.
- **Hyperviscosity:** it only removes energy, and the fastest mode (found
  by power iteration) decays in between τ and 1.25 τ.
- **Determinism:** bit-identical on 1, 2 and 8 workers.

## 3. Results

Measured with `planet_cli shallow-water` (Clang Release, 8 workers) and
`tests/physics/test_shallow_water.cpp`. All runs below are without
hyperviscosity.

**Test 2**, 5 days, axis tilted 40° (α = 0.7 rad):

| Level | Steps (Δt) | h L2 error | h max error | Energy change | Wall |
|---|---|---|---|---|---|
| L3 | 245 (1,763 s) | 2.0e-3 | 5.3e-3 | −2.8e-8 | 0.06 s |
| L4 | 504 (857 s) | 5.1e-4 | 1.4e-3 | −3.2e-9 | 0.23 s |
| L5 | 1,038 (416 s) | 1.3e-4 | 5.8e-4 | −6.2e-10 | 1.4 s |
| L6 | 2,139 (202 s) | 3.5e-5 | 5.0e-4 | −1.4e-10 | 10.8 s |

- The L2 error converges at second order (orders 1.95, 1.97, 1.92) and is
  far inside the 1e-3 gate at L6.
- The maximum error stalls at 5e-4. That is TRiSK's pentagon-ring
  inconsistency (ADR-0011 §12) appearing in the solution's maximum, as
  the operators predicted.
- This **decides ADR-0011 §12: TRiSK is kept, and Peixoto's fallback is not
  needed.**
- Mass changes by at most 1e-15.

**Test 5**, 15 days:
- Mass is exact. At L4 and L5, energy changes by −2.5e-7 and −5.3e-8, and
  potential enstrophy by +1.7e-3 and +5.3e-4. TRiSK does not conserve
  enstrophy in the energy form.
- The maximum wind is 38.7 m/s.
- The solution differs from the L6 run (sampled at the nearest L6 cell) by
  2.0e-3 at L4 and 7.9e-4 at L5 in normalised L2.

**Energy against Δt** (test 5, L4, one day): the relative energy change is
2.4e-8, 3.3e-9 and 4.2e-10 for Δt = 665, 332 and 166 s, ratios of 7.3 and
7.9. That is RK3's third order: the spatial scheme adds nothing, as
TRiSK's V1 identities promise.

**Hyperviscosity:**
- The fastest mode of the vector Laplacian is rotational, with λ d̄² =
  −27.2 at L4 and −28.9 at L5. With `ν₄ = d̄⁴ / (30² τ)` it decays in
  1.22 τ and 1.07 τ.
- The first form, `d̄⁴ / (64 τ)`, assumed the scalar Laplacian's 8/d̄² and
  was corrected before use.
- In test 5 with τ = 6 h, energy falls at every step and ends below the
  inviscid run's.

**Sub-step rule:** with the atmospheric bounds of ADR-0011 §4.3 (350 m/s
waves, 100 m/s wind, Courant 0.5) a 10-minute reference step needs 3
sub-steps at L5 and 6 at L6 (2 at L4). The Williamson runs above used their own wave
bounds (180 and 250 m/s). A wind above the bound throws.

**Determinism and checks:** bit-identical on 1, 2 and 8 workers. 67 tests
pass (GCC); the new tests also pass with Clang Release, and the
floating-point policy check is clean.

**Not yet optimised.** Each RK3 stage allocates its work fields, and fields
are read through the bounds-checked accessors. Test 2 at L6 costs about
5 ms per RK3 step on 8 workers. Reference mode has no budget (ADR-0001
§5), so this is left until weather windows need speed.
