# Task M2-01b — Foundation hardening before terrain

- **Milestone:** P0 / M2 (in progress; G2-M2 and M2-01 are complete)
- **Requested:** 2026-09-28
- **Scope:** the outstanding items of `docs/DEVELOPMENT_SPEC_v0_3.md` §26, which
  the specification requires before M2 terrain work starts, plus three
  documentation gaps found while reviewing M2-01. No physics, no terrain.
- **Blocks:** `docs/tasks/M2-02-plates-terrain-and-sea-level.md`
- **Governing decisions:** specification §4 (determinism, identity and time),
  §6 (state partition), §26; ADR-0001 §4.1; ADR-0002 §4.4, §4.6; ADR-0003 §3.4,
  §3.6, §8

## 1. Read first

1. `AGENTS.md`
2. `docs/DEVELOPMENT_SPEC_v0_3.md` §4, §5 (mesh acceptance tests), §6, §26
3. `docs/decisions/0002-mesh-and-field-layout.md` §4.4 (precision), §4.6 (parallelism and determinism), §9
4. `docs/decisions/0003-determinism-snapshots-migration.md` §3.3, §3.4, §3.6, §4, §8
5. `sim/core/fields/field_registry.hpp`, `sim/core/fields/field.hpp`,
   `sim/core/random/counter_rng.hpp`, `sim/core/scheduler/deterministic_executor.hpp`,
   `sim/planet/orbit/solar_diagnostics.cpp`, `sim/planet/planet_state.{hpp,cpp}`

Accepted ADRs take precedence over the specification where they conflict.

## 2. Current code (as of commit `3d493bc` plus the documentation commit that adds this task)

- `field_registry.hpp` declares `FieldId`, `FieldKind {diagnostic, prognostic,
  reservoir}`, `FieldPartition {slow, fast, climatology, derived}`,
  `FieldLayout`, `FieldDataType`, and three fields. `FieldKind` is used only
  by the registry entries and one assertion in `tests/unit/test_field.cpp`.
  Compile-time checks: IDs unique, sorted, persistent ⇔ slow.
- `for_each_deterministic_block` runs a function over fixed cell blocks in
  deterministic round-robin. There is **no reduction helper**:
  `solar_diagnostics.cpp` builds per-block partials by hand and combines them
  in block order with compensated summation; operator validation sums
  serially.
- `counter_rng.hpp` has `mix_random_key`, `keyed_random_u64` and
  `keyed_random_unit_double`. Tests compare values with each other only; no
  output value is pinned.
- `PlanetState` allocates `hypsometry_m` and the insolation field by hand,
  choosing element type and dimensions independently of the registry.
- CI (`.github/workflows/ci.yml`) has no registry check.
- Area closure on the dual mesh is tested at L0–L6 with tolerance 5e-14; the
  measured values are not recorded per level (ADR-0002 §9 quotes 1.2e-16).

## 3. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **One field classification.** Remove `FieldKind`. `FieldPartition`
   (slow, fast, climatology, derived) is the only classification, matching
   ADR-0001 §4.1 and specification §6. The precision rule of ADR-0002 §4.4
   (float32 by default, float64 for accumulators and slow reservoirs) is
   carried by `FieldDataType` alone. This resolves the M2-01 questions about
   `kind` in the snapshot manifest and about hypsometry's kind.
2. **Reduction helper signature.** In `deterministic_executor.hpp`:

   ```cpp
   template <typename Partial, typename Block, typename BlockFunction, typename CombineFunction>
   [[nodiscard]] Partial reduce_deterministic_blocks(std::span<const Block> blocks,
                                                     std::size_t worker_count,
                                                     Partial identity,
                                                     BlockFunction block_function,     // (block_index, block) -> Partial
                                                     CombineFunction combine);         // (Partial accumulated, const Partial& next) -> Partial
   ```

   It evaluates one partial per block (in parallel through
   `for_each_deterministic_block`), then folds them serially in block-index
   order starting from `identity`. No atomics, no shared accumulator.
   Refactor `analyze_solar_forcing` onto it without changing any output bit.
3. **Typed field factory.** A compile-time mapping from `FieldId` to its
   container: `field_container_t<FieldId::x>` is `Field2D<T>` for `cell`,
   `Field3D<T>` for `cell_layers`, `EdgeField<T>` for `edge`, and `T` for
   `global`, where `T` is `float` or `double` from the descriptor's dtype.
   `make_field<FieldId::x>(const PlanetMesh&)` returns it sized from the mesh
   and the descriptor's layer count, zero-initialised. Use it in
   `PlanetState` for every registered field it owns, and make `SlowState`
   member types use `field_container_t`, so a registry/dtype mismatch fails to
   compile.
4. **Registry append-only check.** Keep a committed baseline
   `tools/ci/field_registry_baseline.tsv` (one line per field: id, name,
   partition, layout, dtype, layers, units). Add `planet_cli registry dump`
   printing the current registry in the same format, and
   `tools/ci/check_field_registry.py BASELINE CURRENT`, which fails when a
   baseline ID is missing or any of its attributes changed, and reports new
   IDs. Retired IDs are declared in code as `retired_field_ids` (initially
   empty) with a compile-time check that no registered ID is retired; the
   script accepts a baseline ID missing from the current registry only when
   it is retired. The baseline is updated deliberately, in the commit that
   adds a field. Run it in CI after the build.
5. **RNG golden vectors.** Pin the current outputs of `mix_random_key`,
   `keyed_random_u64` and `keyed_random_unit_double` for a table of at least
   eight keys covering every argument (seed, each stream, positive and
   negative ticks, cell keys, sample indices, and 0 and max values). Record
   them as literals in a test, with a comment that changing them invalidates
   recorded runs, golden snapshots and replay, and therefore requires an ADR.
6. **No behaviour change.** Every existing output (snapshot bytes, golden
   file, solar diagnostics, operator results) must stay bit-identical.

## 4. Implementation steps

Small, reviewable commits; build and run `ctest` after each.

1. **Area closure record.** Extend `tests/conservation/test_mesh_area.cpp` to
   print the measured relative area error per level (L0–L6), keep the 5e-14
   gate, and record the table in `README.md` and the ADR-0002 §9 record.
2. **RNG golden vectors** (decision 5).
3. **`reduce_deterministic_blocks`** (decision 2) with tests: a float sum
   whose value depends on summation order gives bit-identical results for
   1, 2, 8 and 16 workers and equals a serial block-order fold; an empty
   block list returns `identity`. Refactor `analyze_solar_forcing` and verify
   the solar tests and CLI output are unchanged.
4. **Remove `FieldKind`** (decision 1) and update the registry tests.
5. **Typed field factory** (decision 3) with compile-time tests
   (`static_assert` on the mapped types) and a runtime test of sizes.
6. **Registry baseline, dump command, check script and CI step**
   (decision 4). Test the script on a modified copy of the baseline (changed
   attribute, missing ID, retired ID) in a ctest.
7. **Documentation.** ADR-0003 §8: the manifest carries `partition` and
   `layout` rather than the sketch's `kind` (and why); golden saves are
   loaded exactly now and will also be stepped for ten years (V5) once M3
   physics exists. Specification §26: mark items 1–6 done with commit
   references. Audit: update the rows these items affect. `AGENTS.md`:
   current task back to M2-02.

## 5. Acceptance criteria

| # | Check | Gate |
|---|---|---|
| B1 | Measured dual area closure recorded for L0–L6 | all ≤ 5e-14; table in README and ADR-0002 §9 |
| B2 | RNG golden vectors | exact literals match |
| B3 | Reduction helper: order-sensitive float sum identical for 1/2/8/16 workers and equal to serial block-order fold; empty input returns identity | bit-identical |
| B4 | Solar diagnostics after the refactor | every printed value bit-identical to before (compare CLI output at L5 for two times of year) |
| B5 | `FieldKind` removed; registry still unique, sorted, persistent ⇔ slow | compile-time |
| B6 | `field_container_t` and `make_field` types and sizes for all three registered fields; `SlowState` uses them | compile-time and runtime |
| B7 | Registry check: passes on the current tree; fails on a changed attribute and on a missing non-retired ID; passes on a missing retired ID | ctest |
| B8 | CI runs the registry check | workflow step present and green |
| B9 | Existing outputs unchanged: golden snapshot loads, snapshot bytes for a fixed synthetic state identical to before, operator report identical | bit-identical |

The suite must stay green in CI (GCC and Clang, Debug and Release,
ASan+UBSan) with warnings as errors, and the floating-point policy check must
pass.

## 6. Out of scope

- Terrain and geology (M2-02), drainage (M2-03), the mode scheduler.
- The xxHash3 `state_hash`, run manifests and replay (M3).
- Any new registered field.

## 7. Environment notes

- Configure and test: `cmake -S . -B build -DPLANETSIM_BUILD_TESTS=ON`,
  `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- On Linux 6.x kernels with 32-bit mmap randomisation, sanitizer binaries
  built by Clang 14–17 hang at start-up; run them as
  `setarch -R ctest --test-dir build-sanitize`. CI lowers `vm.mmap_rnd_bits`.
- Stop and ask before adding a dependency, changing an accepted ADR's
  decision, changing a §3 decision, or weakening an existing test.

## 8. Report when done

- Files added and changed, per step.
- Result of each acceptance item B1–B9, with the measured area-closure table.
- Anything not met, and why; any ambiguity found in the specification or ADRs.
