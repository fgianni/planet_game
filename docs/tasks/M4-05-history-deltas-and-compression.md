# Task M4-05 — Snapshot history: compression, delta chains and forks

- **Milestone:** P0 / M4 (fifth and last task; M4-01 to M4-04 are complete)
- **Requested:** 2026-10-01
- **Scope:** ADR-0003's M4 row: compressed snapshots, delta chains and fork
  storage (§3.4, §3.5), V6 (a reconstructed chain equals the direct
  snapshot bit for bit) and V7 (snapshot ≤ 15 ms and ≤ 6 MB at L6; the
  delta ratio measured and recorded, the design revisited above 40 %).
  Autosave (§3.7) and the migration framework (M5) are out of scope.
- **Governing decisions:** ADR-0003 §3.3–3.5, §4 V4, V6–V8, ADR-0002 §4.6

## 1. Decisions already made for this task

Apply these; stop and ask before changing any of them.

1. **Compression: zstd** (ADR-0003 §3.4), the system `libzstd` found with
   `pkg-config` (the project's first third-party library; CI installs
   `libzstd-dev`). Level 3, single-threaded, so the bytes are a function of
   the input and the library version; `state_hash` is taken over the
   uncompressed chunks and never depends on compression.
2. **Codecs**, named in each chunk's existing `compression` key:
   `none` (still read; the golden corpus uses it), `shuffle-zstd` (the
   chunk's bytes regrouped by position within each element, 4 or 8 bytes,
   then zstd), `xor-shuffle-zstd` (delta: XOR with the parent's raw bytes,
   then shuffled and compressed). The manifest's byte range and CRC-32C
   cover the stored bytes; the decompressed length must equal the
   registry's.
3. **Deltas.** A delta file carries the optional manifest key
   `"kind":"delta"` after `format` (absent: a full snapshot; full snapshots
   keep their current manifest) and a non-empty `parent_snapshot_id`. It
   lists only the fields whose raw bytes differ from the parent's (the
   per-field threshold of §3.5 is zero, as V6 requires); a missing field is
   unchanged. A delta and its parent have the same schema version and mesh.
4. **History store** (`sim/core/serialization/history_store.{hpp,cpp}`): a
   directory of `<id>.psnap` files and an appendable `history.txt` index
   (id, parent, kind, tick, depth). Ids are 16 hex digits of XXH3-64 over
   the kind, parent id, tick and `state_hash`. A save with a parent writes a
   delta unless the chain since the last full snapshot already holds
   `base_interval` (8) deltas; a fork is a save with an earlier parent.
   Loading walks the chain from its full snapshot, applying deltas in
   order, then decodes as `read_snapshot` does.
5. **`write_snapshot`** compresses by default (`shuffle-zstd`); `none`
   stays available. `read_snapshot` reads full snapshots of any codec and
   refuses a delta (which needs its chain).

## 2. Steps

1. zstd in the build and CI; the codecs; compressed full snapshots, round
   trip (V4) and corruption (V8) on compressed files.
2. Deltas and the history store; V6 with a decade chain on a running
   planet, a fork and a base rewrite.
3. V7 at L6 through `planet_cli history`: full and delta sizes, write and
   read times; golden files for a compressed snapshot and a delta.
4. Documentation (ADR-0003 implementation record, README, audit, status),
   full CI matrix.

## 3. Acceptance

ADR-0003 V4, V6–V8 with the gates stated there, plus the unchanged suite,
warnings as errors, the floating-point policy, the registry check and the
ADR-0001 performance gates.
