# ADR-0003 — Determinism scope, snapshot schema, history forks and migration

- **Status:** Accepted
- **Date:** 2026-09-23
- **Accepted:** 2026-09-25
- **Context document:** Planetary Civilization Simulator — Design Record v0.4, §13, §14.2, §33
- **Related:** ADR-0001 (time acceleration, simulation modes), ADR-0002 (mesh topology, field layout)
- **Supersedes:** [`docs/decisions/0003-determinism-snapshots-and-replay.md`](../docs/decisions/0003-determinism-snapshots-and-replay.md)

## 1. Context

Three features of the design all rest on the same machinery: **replay** (a history can be re-watched and analysed), **forking** (a decision point can be re-run differently and compared), and the **counterfactual planet** (the same world without the civilization). All three need a state that can be written, read, compared and re-simulated — and they need it to stay loadable while the state layout keeps changing throughout a multi-year project.

The design record asks for determinism without saying how strong. That distinction matters: bit-exact reproducibility across compilers, platforms and optimisation levels is a different engineering problem from reproducibility on one machine, and only one of them is worth paying for here.

## 2. Decision drivers

- **D1 — Trustworthy comparison.** Two forks must differ *because of the decision*, never because of a scheduling or threading artefact.
- **D2 — Debuggability.** A player's bug report must be reproducible from a small file.
- **D3 — Storage.** A 300-year history with decade snapshots, plus forks, plus a shadow planet, must not run into gigabytes.
- **D4 — Longevity.** Saves must survive state layout changes for the life of the project.
- **D5 — Cost.** Snapshotting must fit in the ADR-0001 budget (≤ 15 ms, taken on a worker thread).

## 3. Decision

### 3.1 Determinism, scoped in three levels

| Level | Guarantee | How it is achieved | Tested by |
|---|---|---|---|
| **L0 — Run determinism** | Same build, same platform, same seed, same commands → **bit-identical** state, for any thread count, frame rate, pause pattern or weather-window schedule | fixed block decomposition and reduction order (ADR-0002 §4.6), per-stream RNG keyed by `(seed, stream, tick, cell)`, integer simulation clock, no wall-clock input to the solver | V1, V2 |
| **L1 — Build determinism** | Same platform, different build of the same source → statistically equivalent, not bit-identical | compiler flags pinned (`-ffp-contract=off`, no fast-math), but library and codegen changes are not controlled | V3 |
| **L2 — Cross-platform** | Different OS/CPU → **not guaranteed**, explicitly out of scope | — | — |

Chasing L2 would mean fixed-point arithmetic or a software FP path, at a large and permanent cost in speed and complexity, for a single-player desktop game. It is rejected. Where cross-platform comparison is needed (CI on another machine, a shared scenario benchmark), the comparison is against **tolerances**, using the acceptance targets of Design Record §30.

### 3.2 The simulation clock is an integer

Simulated time is an `int64` tick count, one tick = one minute of simulated time
(approximately 5.26 × 10⁷ ticks per simulated century, far inside range).
Every schedule, RNG key and snapshot label uses ticks. No accumulated `double`
seconds anywhere: accumulation drift is a classic source of forks diverging
for no physical reason.

### 3.3 Replay is recorded as inputs, not as outputs

A run is fully described by a small **run manifest**:

```
run:
  scenario_id, scenario_hash
  world_seed
  engine_version, mesh_level, layer_counts, flags
  commands: [ { tick, actor, type, payload } ... ]
  checkpoints: [ { tick, state_hash } ... ]      # every simulated year
```

A few kilobytes for a century of play. Replay re-simulates from the scenario and the commands; the periodic `state_hash` (xxHash3 over the slow state in canonical order) detects divergence and reports **when** it started, which is what makes an L0 violation a tractable bug rather than a mystery.

### 3.4 Snapshot schema

Binary, chunked, with a text manifest for tooling:

```
manifest (JSON)   schema_version, engine_version, tick, mesh_level, layer counts,
                  parent_snapshot_id, field table [ { field_id, name, kind, dtype,
                  layers, compression, byte_range, checksum } ]
chunks (binary)   one per field, zstd-compressed, float32 unless declared float64
```

- **Fields are addressed by stable numeric `field_id`, never by name.** Renaming a field in code must not change the file; removing one must not shift any other field's identity.
- **Only the slow state is stored** (ADR-0001 §4.1). Fast weather fields are never serialised; climatology is derived on load unless a `derived_cache` flag says otherwise.
- **Checksums per chunk**, so corruption is detected per field rather than invalidating the whole save.
- **Canonical order** — cells in mesh order, fields by `field_id` — so that two snapshots of equal state are byte-equal, which makes `state_hash` and fork comparison trivial.

Expected sizes at level 6 (≈ 41k cells, 5 layers): ~17 MB raw slow state, **≈ 4–6 MB compressed**.

### 3.5 History and forks: base plus delta chain

A history is a DAG of snapshots. Storing every decade snapshot in full would be wasteful, since most fields change slowly and smoothly:

```
[base snapshot]──►[Δ]──►[Δ]──►[Δ]──►[Δ]  main history
                          └───►[Δ']──►[Δ']  fork at a decision point
```

- A **delta** stores only fields whose contents changed beyond a per-field threshold, as XOR-compressed float32 planes. Typical decade deltas are expected at **10–25 %** of a full snapshot; this must be measured at M4 and the design revisited if it exceeds 40 %.
- A full base is rewritten every N deltas (N ≈ 8) to bound reconstruction cost.
- The **counterfactual planet is a fork with one input zeroed**: it stores its own delta chain against the same base, which is what makes it cheap.
- Budget: a 300-year history with decade granularity, one fork and a shadow planet ≈ **60 snapshots ≈ 150–250 MB compressed**. Acceptable.

### 3.6 Migration

Saves must load for the life of the project.

- `schema_version` increments whenever the meaning or set of stored fields changes.
- The engine carries an **ordered chain of migrations**, each `vN → vN+1`, applied on load; they live in the code permanently and are never deleted.
- **Adding a field**: the field declares an initialiser — a constant, or a function of other fields (e.g. soil carbon from vegetation and climate). Missing data is never silently zero.
- **Removing a field**: dropped by the migration, with a log line.
- **Changing meaning or units**: forbidden without a new `field_id`. Reusing an id with different semantics is the one mistake that silently corrupts old histories.
- **Mesh level change**: handled by the conservative remap of ADR-0002 §4.7, not by migration code.
- A **golden-save corpus** in CI: one save per historical schema version, each loaded and stepped ten years on every build. This is the only thing that actually keeps migrations working.

### 3.7 Autosave and crash recovery

Rolling autosave every simulated decade and every ten minutes of wall-clock time, three slots, written on the worker thread from a double-buffered copy so the simulation never stalls. The run manifest is appended continuously, so even a lost snapshot leaves a replayable command log.

## 4. Validation plan

| ID | Check | Gate |
|---|---|---|
| V1 | L0: same seed and commands under 1, 2, 8, 16 threads, with pauses, speed changes and weather windows opened at different moments | bit-identical `state_hash` at every checkpoint |
| V2 | Replay: re-simulate from manifest alone | matches the original run's hashes; divergence tick reported if not |
| V3 | L1: previous release build vs current, same scenario | within Design Record §30 tolerances |
| V4 | Round trip: snapshot → load → snapshot | byte-identical |
| V5 | Golden saves: every historical schema version loads and steps 10 years | no error, budgets still close (ADR-0001 V1) |
| V6 | Delta chain: reconstruct state at tick T from base + deltas | equals the direct snapshot, bit-for-bit |
| V7 | Sizes and timings: snapshot ≤ 15 ms, ≤ 6 MB compressed; delta ratio measured | within budget; ratio recorded per release |
| V8 | Corruption: flip bits in a chunk | detected by checksum, reported per field, no crash |

## 5. Consequences

**Positive.** Forks and the counterfactual planet become cheap and trustworthy: differences are attributable to decisions by construction. Bug reports arrive as a few-kilobyte manifest. Long-lived saves survive an evolving state layout. The canonical ordering makes state comparison a one-line hash.

**Negative.** Fixed block decomposition costs some load-balancing flexibility. Migrations accumulate forever and must be maintained. Stable `field_id`s impose discipline: adding a field means touching a registry, not just a struct.

**Risks and mitigations.**

- *An L0 violation appears late and is hard to localise* → per-year checkpoint hashes turn "it diverged" into "it diverged at tick X", and V1 runs on every commit.
- *Delta ratios turn out poor for noisy fields* → measure at M4; noisy diagnostic fields can be excluded from history and regenerated instead.
- *A field id is reused by mistake* → the registry is a single generated file with a compile-time uniqueness check and a CI test against the previous release's registry.

## 6. Milestone mapping

| Milestone | What this ADR requires |
|---|---|
| M1 | Integer clock, RNG stream discipline, field registry with stable ids |
| M2 | Snapshot writer/reader, canonical ordering, checksums, V4 |
| M3 | Run manifest, command log, checkpoint hashes, V1–V2 in CI |
| M4 | Delta chains and fork storage, V6–V7; measure the delta ratio |
| M5 | Migration framework and the first golden saves, V5 |

## 7. Open questions

- Are player-facing analytics (charts, the newspaper record, the observation-layer measurement archive) part of the snapshot, or a separate sidecar file? A sidecar keeps the physical state clean and lets presentation data evolve without a schema bump; current position is sidecar.
- Should a fork share the parent's *command log* as well as its state, so a "what if I had decided otherwise at year 40" fork replays the rest of the decisions automatically? Attractive for comparison, but it assumes later decisions still make sense in a changed world.
- Do we expose the run manifest as a first-class player artefact ("share this run"), which would also make external playtesting analysis trivial?
- How long are histories kept by default before old deltas are pruned, and is pruning ever automatic?

## 8. Implementation record

The M1 portion was implemented on 2026-09-25: the authoritative clock uses
one-minute integer ticks; random values are counter-keyed by world seed,
stream, tick, cell, and optional sample index; and the field registry assigns
a compile-time-checked stable numeric ID to top-of-atmosphere insolation.
The in-process presentation snapshot is schema version 2 and carries the tick
and field ID. Thread-count determinism is tested for the current forcing and
diagnostics with 1, 2, 8, and 16 workers. The persistent chunked snapshot
format, run manifests, hashes, migrations, and delta chains remain assigned to
M2–M5 by this ADR and were not pulled into M1.
