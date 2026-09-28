# Task M2-01 — State partition and persistent snapshots

- **Milestone:** P0 / M2 (in progress; gate G2-M2 finite-volume operators is complete)
- **Requested:** 2026-09-28
- **Scope:** infrastructure only. Do not implement geology, terrain, drainage or any physics.
- **Governing decisions:** ADR-0001 §4.1, ADR-0002 §4.4, ADR-0003 §3.4 and §3.6, ADR-0005 §4.3

## 1. Read first

1. `AGENTS.md`
2. `docs/DEVELOPMENT_SPEC_v0_2.md` §6 (core data model), §7 (authoritative state and presentation), §13 M2
3. `docs/decisions/0001-time-acceleration.md` §4.1 (state partition)
4. `docs/decisions/0003-determinism-snapshots-migration.md` §3.4 (snapshot schema), §3.6 (migration), §4 (validation)
5. `docs/decisions/0005-coastlines-and-drainage.md` §4.1 and §4.3 (the first slow-state fields)
6. `docs/audit/2026-09-ADR-conformance.md` rows C1, C2, D4 and G5

Accepted ADRs take precedence over the specification where they conflict.

## 2. Goal

By the end of M2, ADR-0001 requires the state partition and ADR-0003 requires
a persistent snapshot writer and reader. The geology task that follows will
store its output in the slow state and must be able to save and reload a
generated planet. This task delivers that foundation:

1. `PlanetState` split into slow state `S`, lazily allocated fast state `F`,
   and climatology `X`, with the existing forcing kept as derived data.
2. The field registry extended so that partition, layout and layer count are
   declared per field, and the two slow fields of ADR-0005 registered.
3. A persistent, chunked, canonical snapshot file that round-trips the slow
   state byte-for-byte and detects corruption per field.
4. A small golden-save corpus, started with one file.

## 3. Current code (as of commit `0a25432`)

- `sim/planet/planet_state.hpp`: `PlanetState` owns a shared immutable
  `PlanetMesh` handle and a `ForcingState` (orbit plus the diagnostic
  `Field2D<float> top_of_atmosphere_insolation_W_m2`). There is no partition.
- `sim/core/fields/field_registry.hpp`: `FieldId`, `FieldKind`
  (diagnostic, prognostic, reservoir), `FieldDataType` (float32, float64),
  `FieldDescriptor` with a `persistent` flag, a single registered field
  (`0x0001'0001`, insolation, not persistent), and a compile-time uniqueness
  check.
- `sim/core/fields/field.hpp`: `Field2D<T>`, layer-major `Field3D<T>`,
  `EdgeField<T>`, all 64-byte aligned.
- `sim/core/serialization/state_snapshot.{hpp,cpp}`: `StateSnapshot` is the
  **in-process presentation snapshot** consumed by the Godot adapter (schema
  version 2). It is not a save format. Leave its behaviour unchanged.
- Tests use the header-only harness in `tests/test_support.hpp` and are
  registered in `tests/CMakeLists.txt` with `add_planetsim_test`.

## 4. Decisions already made for this task

These resolve points the ADRs leave open. Apply them; do not reopen them
without stopping to ask.

1. **No new third-party dependencies.** The default build has none and must
   stay that way. Consequently, for M2:
   - chunks are written uncompressed, with `"compression": "none"` in the
     manifest; the reader rejects any other value. zstd is deferred until
     snapshot sizes are measured against ADR-0003 V7 (M4);
   - per-chunk checksums are **CRC-32C** (Castagnoli), implemented in-tree
     and tested against the standard check value (`"123456789"` →
     `0xE3069283`). The xxHash3 `state_hash` of ADR-0003 §3.3 belongs to M3
     and is not part of this task.
   Record both points in ADR-0003 §8 (implementation record).
2. **Single-file container**, little-endian, written byte by byte (never by
   copying structs):

   ```
   offset 0   8 bytes   magic "PSNAPv1\0"
   offset 8   u64 LE    manifest length in bytes (M)
   offset 16  M bytes   manifest, UTF-8 JSON
   16 + M     ...       chunk area: field chunks back to back, in field_id order
   ```

   `byte_range` in the manifest is `[offset, length]` relative to the start
   of the chunk area.
3. **Manifest content and canonical form.** Keys in a fixed order, no
   insignificant whitespace, integers only (no floating-point numbers in
   JSON; floating-point state goes in chunks). Keys:
   `format`, `schema_version`, `engine_version`, `tick`, `mesh_level`,
   `cell_count`, `parent_snapshot_id` (empty string for a base snapshot),
   `fields` (array ordered by `field_id`, each with `field_id`, `name`,
   `partition`, `layout`, `dtype`, `layers`, `compression`, `byte_range`,
   `checksum`). `field_id` and `checksum` are written as integers.
   The reader is a strict, hand-written parser for this subset (objects,
   arrays, strings without escapes other than `\"` and `\\`, non-negative
   integers). It rejects anything else with a clear error.
4. **Persistent schema version** is a new constant (start at 1), separate
   from the presentation snapshot's version. `engine_version` is the CMake
   `PROJECT_VERSION`, passed in by the build, not hard-coded.
5. **Names.** The persistent format lives in
   `sim/core/serialization/snapshot_file.{hpp,cpp}` with functions such as
   `write_snapshot` and `read_snapshot`. Do not reuse the name
   `StateSnapshot`.
6. **Field IDs** use the scheme high 16 bits = subsystem, low 16 bits =
   index. Existing: `0x0001'0001` forcing (insolation). New, from ADR-0005:

   | Field | ID | Partition | Layout | Type | Layers | Units |
   |---|---|---|---|---|---|---|
   | `hypsometry_m` | `0x0002'0001` | slow | cell, layered | float32 | 9 | m |
   | `sea_level_m` | `0x0002'0002` | slow | global scalar | float64 | 1 | m |

   IDs are never reused or renumbered (ADR-0003 §3.6).
7. **Empty partitions stay empty.** `FastState` and `Climatology` have no
   physical fields yet. Do not invent any. Provide the types, lazy allocation
   of `FastState`, and the rule that neither is ever serialised.

## 5. Implementation steps

Work in small, reviewable commits. Build and run `ctest` after each step.

1. **Registry.** Add `FieldPartition` (slow, fast, climatology, derived) and
   `FieldLayout` (cell, cell_layers, edge, global) to `FieldDescriptor`, plus
   a fixed layer count. Replace the `persistent` flag: a field is persistent
   exactly when its partition is slow. Register the two ADR-0005 fields. Keep
   the compile-time uniqueness check and add one that the registry is sorted
   by `field_id`.
2. **Partitioned state.** Give `PlanetState` a `SlowState` (holding
   `hypsometry_m` as `Field3D<float>` with 9 layers and `sea_level_m` as
   `double`, default-initialised to zero elevation and zero sea level), a
   `FastState` that is absent until explicitly opened and can be released,
   and an empty `Climatology`. Keep `ForcingState` as derived data outside
   the slow state. Update existing callers and the Godot adapter only as far
   as needed to compile.
3. **CRC-32C** in `sim/core/serialization/` with its known-vector test.
4. **Writer.** Serialise every slow field in `field_id` order, cells in mesh
   order, layers layer-major as stored, values as little-endian IEEE-754.
   Two snapshots of equal state must be byte-equal.
5. **Reader.** Validate magic, manifest, schema version, mesh level and cell
   count against the target mesh, every registered slow field present
   exactly once with matching layout, type and layer count, and every chunk
   checksum. On any failure throw an exception whose message names the
   offending `field_id` where one applies; never crash, never read out of
   bounds, never fill a missing field with zeros (ADR-0003 §3.6).
6. **CLI.** `planet_cli snapshot write --subdivision L --out FILE` (writes a
   deterministic synthetic slow state, see §6) and
   `planet_cli snapshot inspect FILE` (prints the manifest summary and
   verifies checksums). Add a ctest smoke test.
7. **Golden save.** Commit one L0 snapshot under `tests/data/golden/` and a
   test that loads it and checks every value. This starts the ADR-0003 V5
   corpus.
8. **Documentation.** Update ADR-0003 §8, ADR-0001 (implementation note for
   §4.1), the audit rows C1, C2, D4 and G5 with evidence, the README
   (snapshot section and CLI), and `AGENTS.md`/`CLAUDE.md` status lines.

## 6. Tests and acceptance criteria

Every item needs an automated test. Use a deterministic synthetic slow state
built from `keyed_random_unit_double` with `RandomStreamId::validation`, for
example hypsometry quantiles as a sorted set of random elevations per cell.

| # | Check | Gate |
|---|---|---|
| A1 | Round trip at L0, L4 and L6: write → read → write | byte-identical files (ADR-0003 V4) |
| A2 | Canonical form: two independently built equal states | byte-identical files |
| A3 | Values survive the round trip | bit-identical fields |
| A4 | Corruption: flip one byte inside each chunk in turn | read fails, message names that `field_id` (V8) |
| A5 | Corruption in the header or manifest, truncated file, empty file | read fails cleanly |
| A6 | Wrong mesh level or cell count, unknown schema version, unknown or duplicate `field_id`, missing slow field, `compression` other than `none` | read fails cleanly |
| A7 | Fast state is absent by default, can be opened and released, and is never written; forcing is never written | tested |
| A8 | CRC-32C check value | `0xE3069283` |
| A9 | Golden L0 save loads with expected values | exact |
| A10 | Registry: IDs unique and sorted; persistent ⇔ slow | compile-time |
| A11 | Size and time at L6 reported (ADR-0003 V7: ≤ 15 ms) | measured and recorded, not gated |

The whole suite must stay green in the CI matrix (GCC and Clang, Debug and
Release, ASan+UBSan) with warnings as errors, and
`python3 tools/ci/check_fp_flags.py build` must pass. New code follows the
style of the surrounding files: `snake_case`, strong ID types, `[[nodiscard]]`,
SI units in names, no exceptions swallowed.

## 7. Out of scope

- Geology, terrain, hypsometry generation, sea-level solve, drainage (next
  task, ADR-0005).
- The multi-rate scheduler and simulation-mode enum (ADR-0001 §4.4; separate
  task, still due by the end of M2).
- Run manifests, command logs, checkpoint `state_hash` and replay (M3).
- Delta chains and forks (M4); the migration framework (M5). Do reserve
  `parent_snapshot_id` in the manifest.
- zstd compression and xxHash3.
- Any change to the presentation `StateSnapshot` beyond keeping it compiling.

## 8. Environment notes

- Configure and test: `cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON`,
  `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- On Linux 6.x kernels with 32-bit mmap randomisation (the maintainer's
  machine), sanitizer binaries built by Clang 14–17 hang at start-up; run
  them as `setarch -R ctest --test-dir build-sanitize`. CI lowers
  `vm.mmap_rnd_bits` instead.
- Stop and ask before adding a dependency, changing an accepted ADR's
  decision, or weakening an existing test.

## 9. Report when done

- Files added and changed, per step.
- Test results for each acceptance item, with the A11 size and timing numbers.
- Any requirement not met, and why.
- Anything in the ADRs that turned out to be ambiguous or wrong.
