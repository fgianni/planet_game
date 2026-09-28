# Golden snapshots

ADR-0003 V5 corpus: one persistent snapshot per schema version. Each file is
committed once and never regenerated after its schema version is released;
`tests/regression/test_golden_snapshot.cpp` loads it and checks every value.

| File | Schema | Mesh generator | Produced by |
|---|---|---|---|
| `psnap-v1-l0.psnap` | 1 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v1-l0.psnap` |

A golden file stops loading when the mesh generator version changes, by
design; that change needs a migration or remap decision (ADR-0003 §3.6,
ADR-0002 §4.7), not a regenerated file.
