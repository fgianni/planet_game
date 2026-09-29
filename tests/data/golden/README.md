# Golden snapshots

ADR-0003 V5 corpus: one persistent snapshot per schema version. Older files
must keep loading through the migration chain (v1 through the schema 1 → 2
migration of ADR-0007 §4.6; v2 through the schema 2 → 3 widening of the ocean
mixed layer to float64, ADR-0007 §10). Each file is
committed once and never regenerated after its schema version is released;
`tests/regression/test_golden_snapshot.cpp` loads it and checks every value.

| File | Schema | Mesh generator | Produced by |
|---|---|---|---|
| `psnap-v1-l0.psnap` | 1 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v1-l0.psnap` |
| `psnap-v2-l0.psnap` | 2 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v2-l0.psnap` |
| `psnap-v3-l0.psnap` | 3 | 2 | `planet_cli snapshot write --subdivision 0 --out psnap-v3-l0.psnap` |

A golden file stops loading when the mesh generator version changes, by
design; that change needs a migration or remap decision (ADR-0003 §3.6,
ADR-0002 §4.7), not a regenerated file.
