# Task M4-03 — Sea ice

- **Milestone:** P0 / M4 (third task; M4-01 and M4-02 are complete)
- **Requested:** 2026-09-30
- **Scope:** ADR-0008 §4.4 (ocean tile with zero-layer sea ice), §4.5
  (budgets, ice terms), V1–V6 and V8 for the ocean tile, and the ice
  diagnostics. The seasonal experiment, climatology and the refit of `D`
  and `g` with ice are M4-04; ADR-0003's history work M4-05.
- **Governing decisions:** ADR-0008 (§4.1–4.5), ADR-0009 (transport and
  shared air act on the radiating surface), ADR-0007 §4.3–4.4,
  specification §13 M4

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Thickness and mass.** `h = m / ρ_i` with the stored mass `m`
   (`sea_ice_mass_kg_m2`). Albedo from the ice at the start of the step,
   `α = α_ocean + (α_ice − α_ocean) · min(1, h / h_r)`; no shortwave
   penetrates. The ice surface radiates with the ocean's emissivity.
2. **Open water** (`m = 0`): the ADR-0007 step with the tile's source and
   exchange. If the mixed layer would fall below `T_f`, it is held at `T_f`
   (the deep layer updated with it), and the deficit `−surplus(T_f)` freezes
   `deficit · Δt / L_f` of ice.
3. **Ice-covered water** (`m > 0`): the mixed layer is held at `T_f`; the
   ocean heat flux to the ice base is
   `F_o = k (T_d' − T_f) + C_s (T_m − T_f) / Δt` (the second term is zero
   while the invariant holds). The transport source and the air exchange act
   on the ice surface. For a trial thickness `h'`, the surface temperature
   solves `r T⁴ + (γ + k_i/h') T = (1 − α) Q + s + k_i T_f / h'` with the
   ADR-0007 Newton solver, held at `T_m` if it would exceed it (the surplus
   melts the top). The new thickness solves the backward-Euler growth
   equation `ρ_i L_f (h' − h) / Δt = F_top(h') − F_o`, where `F_top` is the
   conduction minus top melt; it is strictly increasing in `h'`, so a
   safeguarded Newton–bisection finds it to full precision.
4. **Complete melt.** If no positive `h'` exists, all the ice melts: the
   open-water step runs with the constant sink `L_f m / Δt`, and freezes
   again if it still ends below `T_f`.
5. **Radiating temperature and slope.** The ocean tile's radiating
   temperature (the cell's `T̄`, the budget's emission) is the ice surface
   over ice and the mixed layer over open water. Its slope for the
   transport solve is `1 / (a + 4 r T³)` of the solve that set it
   (`a` including `k_i/h'` over ice), 0 when held at a melting or freezing
   point; the slope only affects convergence.
6. **Budgets.** `latent = L_f (melted − frozen)`; energy closes as
   `storage + latent = Δt (absorbed − emitted + source)` per tile and
   globally. Water: `Δ(snow + ice) = snowfall − snow melt + frozen − ice melt`;
   the net ocean freshwater flux `ice melt − frozen` is reported. Diagnostics
   add ice mass, area (mass > 0) and volume per hemisphere.
7. **Code.** `sim/planet/surface/sea_ice.{hpp,cpp}` with the constants of
   ADR-0008 §4.2; the surface step and the cell solve use it for the ocean
   tile.

## 2. Steps

1. The ocean tile with ice, and unit tests: open water bit-identical to the
   ADR-0007 column when it stays above `T_f`; freezing onset against the
   closed form; Stefan growth (surface pinned by a large exchange, no ocean
   flux) exact for the discrete scheme and converging to the analytic law;
   complete melt; per-tile energy and water closure; invariants.
2. Integration with the cell solve and the mesh step; diagnostics; physics
   tests: global closure, worker bit-identity, the ice–albedo sign (V5),
   ice forming at the poles of the Earth-like and aqua planets.
3. CLI output, documentation (ADR-0008 implementation record, README,
   audit, status), full CI matrix and performance gates.

## 3. Acceptance

ADR-0008 V1–V6 and V8 for the ocean tile, with the gates stated there, plus
the unchanged suite, warnings as errors, the floating-point policy, the
registry check and the ADR-0001 performance gates.
