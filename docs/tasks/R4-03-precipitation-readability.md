# Task R4-03 — Precipitation readability

- **Track:** rendering R4 (specification §31.8, §31.10), in parallel with
  P0 / M7
- **Status:** complete (2026-10-09; ADR-0018 accepted 2026-10-04)
- **Scope:** extend the existing offscreen readability gate to the shared R4
  precipitation overlay.
- **Governing decisions:** ADR-0018 §4.9 and V8; specification §31.8
- **Must not touch:** solvers, snapshots, registered field meanings or M7 work

## 1. Gate

The production shared-overlay shader is rendered with controlled semantic
inputs at the same fixed camera, image size and lighting as the R1/R2 gate.
The pair compares zero precipitation with the channel's 50 mm/day endpoint.
It is measured by the existing CIEDE2000 analyzer under normal vision,
full-severity deuteranopia, protanopia and tritanopia, and luminance alone.
Every mean must remain at least ΔE00 10 over the central 96×96 region.

The heavy-rain image retains the production shader's deterministic per-cell
pulse. Readability therefore covers the actual animated palette rather than
a duplicated static approximation. PNGs, PPMs and JSON analytics remain CI
artifacts.

## 2. Acceptance and report

- The manifest gains the `dry_vs_heavy_precipitation` pair while preserving
  its versioned schema and all earlier pairs.
- The existing CI software-rendering job analyzes the new pair automatically;
  a failure in any vision variant fails the job.
- No simulation or snapshot code changes in this task.
