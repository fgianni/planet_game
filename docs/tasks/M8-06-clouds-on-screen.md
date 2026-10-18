# Task M8-06 — Clouds on screen

- **Milestone:** P0 / M8 (sixth task)
- **Status:** in progress (started 2026-10-18)
- **Governing decisions:** ADR-0023 §4.8, V12; ADR-0018 (channels, style
  packs, the readability gate); ADR-0022 §4.8–§4.9 (altitude bands,
  procedural detail keyed by position)

## Done

- **The column's cloud optical depth** as the step's radiation saw it,
  Σ f_k τ_k (`CloudOptics::column_optical_depth`), is kept in a new derived
  field `cloud_optical_depth` (0x0008'0006, cell, float32, "1"; never
  persisted, so PSNAP is unchanged). The presentation cannot rebuild it
  from the layers' fractions: τ needs r_e and the layers' mass.
- **Presentation snapshot schema 6** (`StateSnapshot`): `cloud_cover`, the
  column fraction 1 − Π (1 − f_k) of the layers' `cloud_fraction` (random
  overlap, as the radiation's), and `cloud_optical_depth`; both empty until
  a step with clouds has run. PFRAME02 frames of schema 6 carry them;
  schema 3–5 frames still read.
- **The channels** (`make_visual_frame`): `cloud_cover` = the column
  fraction, clamped to 0–1; `cloud_thickness` = 1 − exp(−τ / 10). Absent
  without clouds.
- **Tests:**
  - `test_visual_frame` (V12, headless): clear, half-covered and overcast
    cells give cover 0, 0.5 and 1 and thickness 0, 1 − e⁻¹ and more;
    a frame without clouds has neither channel; schema 6 frames round-trip
    their clouds, a schema 5 frame reads without them;
  - `test_cloud_radiation_run`: after three L3 months the snapshot's cover
    is the layers' random overlap in every cell, its area mean the step's
    global cover, and τ > 0 exactly where there is cover;
  - `test_climate_circulation`: the frames are schema 6.

## Remaining

- The cloud layer in both styles, in every altitude band, procedural
  detail keyed by position (ADR-0023 §4.8). Decided with the user
  (2026-10-18): a **separate shell**, a second instance of the cell mesh
  lifted to cloud height with a transparent cloud shader per style
  (opacity from `cloud_cover`, brightness and density from
  `cloud_thickness`), drawn over the terrain tiles and seen from below at
  ground level; the readability harness composites it over the surface.
- V12's readability: clouded against clear terrain through the
  readability harness, in both styles.
