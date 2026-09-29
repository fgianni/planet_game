# Task documents

Each task document states its scope, the decisions already made, acceptance
criteria and what to report. `AGENTS.md` names the current task.

| Task | Subject | Status |
|---|---|---|
| [M2-01](M2-01-state-partition-and-snapshots.md) | State partition and persistent snapshots | Complete |
| [M2-01b](M2-01b-foundation-hardening.md) | Foundation hardening before terrain (specification §26) | Complete |
| [M2-02](M2-02-plates-terrain-and-sea-level.md) | Plates, terrain, hypsometry and sea level | Complete |
| [M2-03](M2-03-drainage.md) | Drainage | Complete |
| [M2-04](M2-04-scheduler-and-calendar.md) | Simulation modes, scheduler skeleton and sub-step calendar (ADR-0001 §8, ADR-0006) | Complete |

Completed tasks are records: they keep the specification and design versions
they were written against, even after those files have left the tree. Read
cited versions from git history (`git log -- docs/DEVELOPMENT_SPEC_v0_2.md`,
for example).
