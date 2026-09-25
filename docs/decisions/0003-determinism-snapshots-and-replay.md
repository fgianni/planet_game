# ADR 0003: Determinism, snapshots, and replay scope

- Status: Superseded
- Date: 2026-09-24
- Superseded: 2026-09-25 by
  [`ADR-0003 — Determinism scope, snapshot schema, history forks and migration`](../../adrs/ADR-0003-determinism-snapshots-migration.md)
- Milestone: P0 / M1 architecture

## Context

Bit-identical floating-point results across compilers, platforms, instruction
sets, and optimization levels would require constraints disproportionate to
the current simulation. The project still needs repeatable tests, useful bug
reports, future history forks, and a migration path as state layouts evolve.

M1 also extends `StateSnapshot` for the first time. A versioning convention is
needed before clients depend on an unversioned layout.

## Decision

- Determinism is required for a fixed build, platform, scenario, seed, and
  ordered command stream.
- Cross-build replay will be based on scenario identity, seed, code version,
  and recorded commands rather than a promise of identical floating-point
  arithmetic.
- Every snapshot schema carries an explicit integer version from its first
  externally consumed form. Schema changes require either compatibility or a
  documented migration.
- Authoritative and derived values are identified explicitly. Derived state is
  reconstructed instead of being required in a future persistent save.
- In M1, simulation time and planet parameters are authoritative inputs;
  orbital phase, rotation angle, solar direction, and per-cell top-of-
  atmosphere insolation are deterministic derived state.
- The current `StateSnapshot` is a read-oriented presentation snapshot, not
  yet a persistent save format. Its schema is nevertheless versioned so the
  Godot boundary is explicit.

## Consequences

- Tests may require exact repeatability within one build but numerical
  regression tolerances remain necessary across toolchains.
- Future save work must add scenario, seed, code version, command history, and
  migrations rather than serializing every derived array.
- Snapshot additions cannot be made as invisible ABI assumptions.

## Alternatives considered

### Cross-platform bit-exact floating point

This would complicate math, compiler, and parallel execution choices without
providing proportional value for P0.

### Store all derived fields in saves

This makes save files larger and migrations harder while duplicating values
that can be reproduced from authoritative inputs.
