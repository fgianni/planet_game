# Task M3-03 — Ocean precision, replay, performance gate: closing M3

- **Milestone:** P0 / M3 (third and last task; M3-01 and M3-02 are complete)
- **Requested:** 2026-09-29
- **Status:** Complete
- **Scope:** the three kinds of work accepted ADRs still assigned to M3 after
  M3-02, plus the ADR-0007 §9 open finding:
  1. the ocean mixed layer stored as `double` (ADR-0007 §10);
  2. run manifest, command log, checkpoint state hashes and replay, with
     ADR-0003 V1–V2 in CI (ADR-0003 §3.3, §7);
  3. the golden saves' ten-simulated-year step (ADR-0003 V5, deferred from
     M2 in §8 "until M3 provides physics");
  4. the first climate-mode performance measurement and its CI gate
     (ADR-0001 §5, V4, §8).
- **Governing decisions:** ADR-0001 §5, §8; ADR-0003 §3.1, §3.3, §3.6, §4
  V1, V2, V4, V5; ADR-0007 §4.1, §9, §10; specification §7, §8, §24, §26

## 1. Decisions made for this task

1. **Mixed-layer field identity.** A registered field's type may not change,
   so `0x0003'0003` (`float32`) is retired and the mixed layer is registered
   again as `0x0003'0005` (`float64`, same name). PSNAP schema 3; the
   v2 → v3 step widens the stored values exactly in the core reader. v1, v2
   and v3 golden files are kept and tested.
2. **State hash.** XXH3-64 (seed 0, default secret), implemented in-tree
   like CRC-32C, over the canonical slow-state chunks, each preceded by its
   field ID and byte length. Verified against the xxHash 0.8 library for
   every input length 0–3000 and a 2 MB input; fifteen reference vectors
   covering every code path are pinned in `tests/unit/test_xxh3.cpp`.
3. **Manifest format.** Line-oriented text, `PRUNv1`, one tab-separated
   record per line, so a live run can append commands and checkpoints
   (ADR-0003 §3.7 later). It records the engine version, mesh generator
   version, the scenario as ordered key/value pairs with their XXH3 hash,
   the commands, the checkpoints and the end tick. The core stores the
   scenario without interpreting it.
4. **Scenario.** `preset`, `seed`, `subdivision`, `spin_up_years` and
   `initial_mode`. With the build, it determines the initial slow state;
   replay rejects unknown or missing keys, so a manifest from a build with a
   different scenario never replays silently.
5. **Commands.** P0 climate-lab controls that exist physically at M3:
   `set_mode` (`climate` | `reference`) and `set_solar_luminosity_factor`
   (a multiple of the scenario's luminosity in (0, 10]). A command takes
   effect at the start of the first step that begins at or after its tick;
   the manifest records that effective tick. In climate mode that is the
   next sub-step boundary, so a request can wait up to a month. Pacing
   (`run_until` chunking, pause) is not a command and cannot change the
   record.
6. **Checkpoints.** At tick 0 (after spin-up) and at the first step boundary
   at or after the start of every orbital year. Replay compares every
   checkpoint, the commands' effective ticks and the end tick, and reports
   the earliest divergent tick.
7. **Performance gate.** `planet_cli run` measures the stepping rate
   (simulated years per wall-clock minute) and the total wall time,
   including terrain generation and spin-up. CI's Release jobs run the
   ADR-0001 §5 scenario (250 years) at L5 (≥ 20 years/min, ≤ 240 s) and L6
   (≥ 5 years/min, ≤ 600 s), then replay the L5 run. The gates are the
   budget itself, which is generous at M3. The V4 "regression > 20 %"
   comparison needs a stable runner baseline and is left to the first
   milestone whose cost approaches the budget.

## 2. Code

| Concern | Code |
|---|---|
| Retired and new mixed-layer field, schema 3 | `sim/core/fields/field_registry.hpp`, `sim/core/serialization/snapshot_file.{hpp,cpp}`, `tools/ci/field_registry_baseline.tsv` |
| XXH3-64 | `sim/core/serialization/xxh3.{hpp,cpp}` |
| `slow_state_hash` | `sim/core/serialization/snapshot_file.{hpp,cpp}` |
| Run manifest | `sim/core/serialization/run_manifest.{hpp,cpp}` |
| Scenario, commands, checkpoints, replay | `sim/planet/run/planet_run.{hpp,cpp}` |
| `planet_cli run`, `planet_cli replay` | `apps/planet_cli/main.cpp` |
| CI gate and replay | `.github/workflows/ci.yml` |

## 3. Acceptance

| Check | Test | Result |
|---|---|---|
| Reference-mode ocean without storage rounding | `test_surface_energy` | 30 days of ten-minute steps equal an all-`double` column bit for bit |
| v1, v2, v3 golden saves load | `test_golden_snapshot` | exact; v2 widened exactly |
| ADR-0003 V5: golden saves step ten years | `test_golden_snapshot` | 120 sub-steps each; worst closure ratio 5.1e-5 of the ADR-0007 V2 gate |
| XXH3 reference vectors | `test_xxh3` | 15 pinned vectors |
| Manifest round trip and rejection of edited, malformed or disordered records | `test_run_manifest` | exact |
| ADR-0003 V1: 1/2/8/16 workers, one `run_until` or uneven chunks ending inside a reference window and inside sub-steps, a command submitted late | `test_run_replay` | identical manifests (commands, checkpoints, hashes, end tick) |
| ADR-0003 V2: replay from a manifest file on other workers | `test_run_replay`, `planet_cli_replay_smoke`, CI | matched; an altered command diverges at the next checkpoint, an altered hash at its own tick, a shifted command before the following checkpoint |
| ADR-0001 §5: 250 years | CI Release | see below |

Measured on the development machine (Clang 14 Release):

| Level | Workers | Stepping rate | 250-year total | Budget |
|---|---|---|---|---|
| L5 | 4 | 2,981 years/min | 5.3 s | ≥ 20 years/min, ≤ 240 s |
| L5 | 24 | 5,727 years/min | 2.8 s | idem |
| L6 | 4 | 774 years/min | 20.2 s | ≥ 5 years/min, ≤ 600 s |
| L6 | 24 | 1,962 years/min | 8.2 s | idem |

The L5 replay of the 250-year run compared 251 checkpoints and matched in
3.0 s. The suite passes 51/51 in GCC 11 Debug and Release, Clang 14 Debug and
Release, and ASan+UBSan on Clang 14 and GCC 12, with the registry check
(8 current fields, 1 retired) and the floating-point policy check
(75 translation units).

## 4. Not in scope

Delta chains and forks (ADR-0003 M4), autosave and the continuously
appended manifest (ADR-0003 §3.7), the migration framework beyond the
declared chain (M5), weather windows (M10–M12), and state hashes on
`GeologyState` (not persisted; regenerated from the scenario).
