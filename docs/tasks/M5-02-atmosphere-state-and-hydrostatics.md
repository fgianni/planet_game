# Task M5-02 — Atmosphere state and hydrostatic initialisation

- **Milestone:** P0 / M5 (second task; M5-01 is complete)
- **Scope:** ADR-0010 §4.1–4.3 and §4.7:
  - the atmosphere's slow state, with a layer count set per scenario;
  - the presets, the hydrostatic initialisation and the pressure and
    height diagnostics;
  - PSNAP schema 5 with the 4 → 5 migration, and a v5 golden save.

  No physics steps the atmosphere yet (M5-03).
- **Governing decisions:** ADR-0010 §4.1–4.3, §4.7, V1, V8; ADR-0003 §3.4,
  §3.6

## 1. Decisions made for this task

1. **Fields.** `atmosphere_surface_pressure_Pa` (`0x0005'0001`, cell,
   float64) and `atmosphere_temperature_K` (`0x0005'0002`, cell_layers,
   float64, layer-major, bottom first).
2. **Scenario-layered fields** (moved here from M5-01):
   - The registry declares them with `scenario_layer_count` (0 layers).
     Only `cell_layers` fields may do so, which is statically checked.
   - Each snapshot's manifest states the stored count, and the reader
     checks it against the bytes and against the bound
     `max_scenario_layer_count` (64).
   - All scenario-layered fields of one snapshot must agree.
   - The registry dump prints `scenario` in the layers column, and the CI
     registry check accepts it.
   - A delta's unchanged chunk keeps its parent's count, so the decoder
     derives the count from the validated length.
3. **The state's N is its data.** `PlanetState` starts with zero layers
   and zero surface pressure. `initialise_atmosphere` sets the scenario's
   N, as a migration does, and `diagnose_atmosphere` refuses parameters
   whose N differs from the state's. The run manifest will record N when
   runs step the atmosphere (M5-03).
4. **Hydrostatics** (`sim/planet/atmosphere/atmosphere.{hpp,cpp}`):
   - `g = G M / R²` (9.8203 m/s² for the development Earth).
   - The surface height is the land fraction times the land tile's mean
     height above sea level, the same mean the drainage of ADR-0005 uses.
   - The surface temperature is the tile mean of the land surface and
     ocean mixed layer.
   - Every column reduces to exactly `p₀` at sea level. ADR-0010 §4.3's
     "single global factor" is therefore identically one and is not
     applied.
5. **Layer heights** use the hypsometric equation with the trapezoidal mean
   temperature in ln p, starting from the surface air temperature (layer 0
   extrapolated along Γ_c). A first version used layer 0's temperature from
   the surface and was 6 % off the analytic Γ_c profile; the trapezoid is
   within 0.23 %.
6. **Migration.** `planet_snapshot_migration(planet, surface, atmosphere)`
   (`sim/planet/planet_migration.{hpp,cpp}`) replaces
   `surface_energy_migration`. It supplies the 1 → 2, 3 → 4 and new
   4 → 5 initialisers; the last initialises the atmosphere with the
   scenario's N.

## 2. Results

- `planet_cli atmosphere --subdivision 5` (seed 1, earth_like, N = 3)
  reports:
  - mass 5.135 × 10¹⁸ kg (Earth: 5.148 × 10¹⁸);
  - mean surface pressure 98.9 kPa (Earth: about 98.5), ranging from
    44.8 kPa at 6.3 km to 105.6 kPa in an inland depression;
  - layer centres at about 1.7, 5.7 and 13.8 km.
- V1: the closed-form surface pressure agrees with a numerical integration
  of the hydrostatic equation to 1.6e-13. Isothermal heights are exact to
  4e-16, and Γ_c profile heights are within 0.23 % (gate 0.5 %). The
  sea-level reduction of every initialised column returns `p₀` to 1e-12,
  and the diagnostics are bit-identical on 1 and 8 workers.
- V8: schema 5 round trips are byte-identical for N = 0, 3 and 5 under
  both codecs. Golden saves v1–v4 load through the chain, including the
  4 → 5 step, and arrive at rest. The new v5 golden save loads exactly and
  steps ten years.
