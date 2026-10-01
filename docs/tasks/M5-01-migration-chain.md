# Task M5-01 — The ordered snapshot migration chain

- **Milestone:** P0 / M5 (first task; requested 2026-10-01)
- **Scope:** ADR-0003 §3.6 and its M5 row: replace the per-schema hooks of
  `SnapshotMigration` with an ordered chain of named `vN → vN+1` steps and
  a log of the steps applied. No physics and no schema change; every golden
  save keeps loading.
- **Governing decisions:** ADR-0003 §3.6, V5; ADR-0010 §4.7

## 1. Decisions already made for this task

1. **The chain is data in core**
   (`sim/core/serialization/snapshot_migration.hpp`). It has one entry per
   step, contiguous from the oldest readable schema to the current one,
   statically checked. Each entry has its source schema, a name, the
   decision it implements, and its kind:
   - *initialiser*: the planet layer supplies a function of the mesh and the
     staged slow state (ADR-0003 §3.6, "the field declares an initialiser");
   - *core*: a conversion of stored chunks implemented in core, such as the
     2 → 3 widening of ADR-0007 §10. Removing a field is a core step that
     drops a retired field's chunk, with its log line. None exists yet.
2. **`SnapshotMigration`** holds the planet layer's initialisers keyed by
   their target schema. Registering one for a core step, or for a step
   that does not exist, throws. Loading a schema that needs a missing
   initialiser fails with the step's name and decision.
3. **The log** lists one line per step applied, in order, for example
   `1 -> 2 surface-energy temperatures (ADR-0007 §4.6)`. It is returned in
   `SnapshotManifest::applied_migrations`, which the reader fills and the
   writer never stores. `planet_cli snapshot inspect` does not migrate, so
   it shows no log.
4. **Steps compose.** A step's input is the staged state after the
   previous steps. A core step whose stored input is absent because an
   earlier initialiser produced the field leaves the field as that
   initialiser wrote it.
5. The scenario-layered fields of ADR-0010 §4.1 move to M5-02, because
   nothing can test them before the atmosphere fields exist.

## 2. Acceptance

- Golden saves v1–v4 load through the chain with the expected logs, and
  step ten years (ADR-0003 V5, unchanged).
- Missing-initialiser errors name the step.
- The existing suite, warnings as errors, the floating-point policy and the
  registry check all pass.
