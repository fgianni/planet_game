# Task M5-04 — Calibration, preset switch, gates and the close of M5

- **Milestone:** P0 / M5 (fourth and last task; M5-01 to M5-03 are complete)
- **Scope:** ADR-0010 §4.6 and V6, V9, V10:
  - the joint refit of τ₀ and D, and the `earth_like` preset switched to
    the three-layer atmosphere (the grey layer retired);
  - the layer count recorded in the run manifest;
  - the plateau experiment, the ADR-0001 performance gates, and the
    records that close M5.
- **Governing decisions:** ADR-0010 §4.2, §4.5–4.6, §11 (amended during
  this task); ADR-0009 §4.4; ADR-0001 §5; ADR-0003 §3.3; specification §23,
  §24

## 1. Decisions made for this task

1. **Warm start.** Under an atmosphere, `initialise_surface_temperatures`
   starts from the grey-layer equilibrium with g = 1 − exp(−τ₀), capped at
   0.95. The start is warm and free of ice, so the spin-up cools into the
   climate rather than starting inside the ice–albedo feedback. A start at
   g = 0 is near 255 K under sea ice.
   - `initialise_climate` initialises the surface temperatures, the
     cryosphere and the atmosphere together. Runs, the CLI and the test
     fixtures use it.
2. **ADR-0010 §11 (transported heat enters the bottom layer).** Under §3.4 C
   the fit could not reach 42 K at any D: 59–63 K for D from 1.5 to 12,
   with the transport saturating near 3.3 PW and the polar surface near
   232 K. The choice was put on 2026-10-02 and the amendment was taken.
3. **Transport floor.** The floor is the source at which the bottom layer
   would balance at 100 K, with the longwave and sensible heat the surface
   then supplies.
   - The first floor ignored that supply. It clipped the outer Newton,
     which stalled during the first steps from rest at low τ₀ and applied
     a 900 W/m² inconsistency to one cell.
   - With the fix, every τ₀ from 0 upward runs cleanly. At τ₀ = 0 the mean
     is 258 K.
4. **Fit** (`planet_cli thermal --subdivision 4 --years 150 --layers 3
   --calibrate 288 --calibrate-gradient 42`). The fit alternates between
   (τ₀, D) = (1.3581, 0.6371), giving 287.92 K and 41.96 K, and
   (1.3624, 0.6458), giving 288.02 K and 41.67 K. The first is kept.
   - At L4: peak poleward transport 3.82 PW (1.8 PW for M4's grey
     planet); sea ice 6.1–9.5 million km² in the north and 24.4–25.6 in
     the south; layers at 265, 241 and 226 K; convection over 99 % of the
     area.
   - At L5 (150 years): 288.52 K, 40.9 K, 3.71 PW; northern sea ice
     4.6–9.9 million km², southern 22.5–24.0. The relative imbalance is
     −7.5e-3, with perennial ice still thickening.
   - The record is in `sim/planet/surface/surface_energy.hpp`.
5. **Run manifest.** The scenario entry `atmosphere_layers` records the
   resolved count. A manifest written before M5 lacks it and takes the
   preset's count. A preset without an atmosphere refuses a non-zero
   count.
6. **Tests that changed with the preset:**
   - The two ADR-0007 and ADR-0008 equivalence checks compare the mesh step
     with per-tile grey-layer steps. They now run on an explicit grey
     configuration (N = 0, g = 0.4965, M4's last fit).
   - The history fork perturbs τ₀ (+0.2) instead of the retired `g`.
   - The sea-ice planet's "lowest ice latitude" ignores cells without ocean
     area. Their ocean tile follows the surface air (ADR-0009 §10); on 6 km
     plateaus at 15–27° that air is cold enough to freeze it, but it
     carries no weight.
7. **Performance.** The first L6 gate failed at 742 s (limit 600). Two
   changes fixed it without changing any decision:
   - The scalar surface Newton (`solve_column_surface`) ran a fixed ten
     iterations on every call and took 38 % of a step. It now stops once
     its step reaches the rounding level. The iterates fall monotonically
     onto the root, and no printed digit of the climate changes.
   - Within a step each column solve starts from the linear prediction
     T* + (dT*/dh) Δh of its previous implicit solution. Before, it started
     from the convectively adjusted temperatures, which are not a solution
     of the system it solves. Column iterations fell from up to 9 to 2.
     This start is never carried across steps, so a step is still a
     function of the state.
   - The outer transport Newton converges only linearly in its tail
     (about 10–100× per iteration below 1 W/m²). A 10⁴× tighter CG
     tolerance does not change that, so the cause is kinks in the
     columns' response (mixed pools joining, floes reaching full cover,
     ice melting away) rather than the inexact linear solves. It is
     recorded, not changed.

## 2. Results

- **V6 plateau** (`test_atmosphere_plateau`, L4, 30 years). An aqua
  planet with one equatorial dome (500 m at its coast to 4,000 m) cools
  with height at 6.0 K/km over 153 cells within ±20° (gate 4–9.8 K/km).
  No lapse rate is imposed anywhere.
- **V9 calibration.** As in §1.4; the invariants of ADR-0010 §4.6 hold
  (every ε_k in (0, 1), τ₀ > 0).
- **V10 performance** (4 workers, development machine):
  - 250 years at L5 in 146 s (gate 240 s; 176 s before M5);
  - 250 years at L6 in 447 s (gate 600 s; 572 s before M5);
  - the L5 run replays bit for bit (251 checkpoints).
- **The seasonal experiment** (ADR-0008 V7) passes on the new climate:
  288.6 K, northern ice 5.7–9.4 million km², stationary to 0.7 % between
  decades.
- The full suite passes (64 tests).
