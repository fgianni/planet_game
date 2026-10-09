# Golden snapshots

ADR-0003 V5 corpus: one persistent snapshot per schema version. Older files
must keep loading through the migration chain (v1 through the schema 1 → 2
migration of ADR-0007 §4.6; v2 through the schema 2 → 3 widening of the ocean
mixed layer to float64, ADR-0007 §10; v1–v3 through the schema 3 → 4
initialiser of ADR-0008 §4.6, which declares no snow and no sea ice; v1–v4
through the schema 4 → 5 initialiser of ADR-0010 §4.3, the atmosphere at
hydrostatic rest; v1–v5 through the schema 5 → 6 initialiser of ADR-0021
§4.1, each layer at 60% relative humidity and the bucket half full). Each
file is
committed once and never regenerated after its schema version is released;
`tests/regression/test_golden_snapshot.cpp` loads it and checks every value.

| File | Schema | Mesh generator | Produced by |
|---|---|---|---|
| `psnap-v1-l0.psnap` | 1 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v1-l0.psnap` (uncompressed; the writer of the time) |
| `psnap-v2-l0.psnap` | 2 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v2-l0.psnap` |
| `psnap-v3-l0.psnap` | 3 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v3-l0.psnap` |
| `psnap-v4-l0.psnap` | 4 | 2 | `planet_cli snapshot write --subdivision 0 --compression none --out psnap-v4-l0.psnap` |
| `psnap-v4-l0-zstd.psnap` | 4 | 2 | `planet_cli snapshot write --subdivision 0 --compression zstd --out psnap-v4-l0-zstd.psnap` |
| `psnap-v4-l0-delta.psnap` | 4 (delta of the zstd file) | 2 | `planet_cli snapshot write --subdivision 0 --delta-of psnap-v4-l0-zstd.psnap --out psnap-v4-l0-delta.psnap` |
| `psnap-v5-l0.psnap` | 5 (three atmosphere layers) | 2 | `planet_cli snapshot write --subdivision 0 --compression none --out psnap-v5-l0.psnap` |
| `psnap-v6-l0.psnap` | 6 (three layers of humidity and the bucket) | 2 | `planet_cli snapshot write --subdivision 0 --compression none --out psnap-v6-l0.psnap` |

A golden file stops loading when the mesh generator version changes, by
design; that change needs a migration or remap decision (ADR-0003 §3.6,
ADR-0002 §4.7), not a regenerated file.

Since task M4-05 the corpus also pins the codecs (ADR-0003 §3.4): the
v1–v4 files are uncompressed (`none`); `psnap-v4-l0-zstd.psnap` is the
`shuffle-zstd` codec and `psnap-v4-l0-delta.psnap` a delta of it
(`xor-shuffle-zstd`, `"kind":"delta"`) whose parent id is the base file's
stem. A zstd release that changes compressed bytes does not invalidate them:
the reader decompresses any conforming frame.
