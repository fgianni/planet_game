# ADR-0020 — The planet's read contract: published snapshots, field descriptors, accumulators, regions and the observation interface

- **Status:** Proposed
- **Date:** 2026-10-05
- **Milestone:** none yet; it is meant to run alongside P0 the way the rendering track does (§9). It blocks nothing in P0. It must be accepted before the interpretation layer, S1 or the observation ADR (specification §26.1 item 4) is written against PlanetSim.
- **Context documents:** Design Record v1.4 §46 ("the architecture note", merged from the Systems Architecture note of 2026-10-04), notably §46.3 *PlanetSim: field exposure*; `docs/DEVELOPMENT_SPEC_v0_8.md` §2, §4, §6, §7, §8, §12, §13.3, §15.1, §17, §29.2–29.4, §31.1, §32.2
- **Related:** ADR-0001 (state partition), ADR-0002 (blocks and reductions), ADR-0003 (determinism, PSNAP, history), ADR-0005 (land fraction, drainage basins), ADR-0006 (climate sub-steps), ADR-0018 (presentation boundary). It also relates to records that are reserved but not yet written: ADR-0012 (society state and regions) and ADR-0019 (fog of knowledge).
- **Amends on acceptance:** `FieldDescriptor` (`sim/core/fields/field_registry.hpp`) gains a statistic, a cadence and a valid range (§4.3). The specification's §6 gains `PublishedSnapshot` among the core types. Neither `StateSnapshot` nor PSNAP changes.

## 1. Context

The architecture note puts four consumers above PlanetSim. The interpretation layer turns states into events and metrics. PopSim is the society model of specification §29. Population knowledge is the government's estimate of its own people. Presentation draws the result. The note asks PlanetSim to publish **immutable, typed snapshots through a fixed contract** so that "nothing reads its internals", and it lists what that contract carries:

- a snapshot at the end of each tick: tick id, simulation time, RNG state and field buffers;
- double-buffered or copy-on-write publication, so readers work on tick N while the simulation computes N+1 and the simulation never waits;
- zero-copy `std::span` views over float32 structure-of-arrays;
- a field registry of `FieldDesc { id, unit, grid, cadence, stat, valid, version }`, through which tests can inject synthetic fields;
- accumulated fluxes (totals since the last snapshot) rather than samples;
- precomputed region masks and a reduction API;
- truth and observation as separate channels, joined by an observation operator `Observed = H(truth, network) + noise` with a variance field;
- optional per-process tendency diagnostics as the cause weights of events;
- bit-for-bit reproduction from snapshot, seed and input log.

The specification already decides much of the layer above. Society reads the planet only through `ExposureState` (§29.4). The view is gated, the field never is (§17.2). Attribution comes from the counterfactual planet (§7.1, §15.1). Knowledge and fog have no physical effect (§17.2, §32.2). What it does **not** decide is the contract on the planet's side: what a consumer outside the solvers may read, at what moment, in what shape and with what guarantees. Today each consumer gets its own ad hoc copy:

### 1.1 What the repository has (audit, 2026-10-05, at `dfbc25f`)

- **The field registry** (`sim/core/fields/field_registry.hpp`). This is a `constexpr` table of 19 `FieldDescriptor`s: append-only id, name, partition (slow, fast, climatology, derived), layout (cell, cell_layers, edge, edge_layers, global), data type (float32, float64), layer count and a units string. Retired ids are listed. Compile-time checks enforce uniqueness, ordering and partition rules, and CI diffs the table against a baseline. It has no cadence, statistic or valid range.
- **Mixed precision.** The slow state's reservoirs are `float64`: ocean temperatures, snow and sea-ice mass, surface pressure, atmospheric temperatures and sea level (ADR-0002, ADR-0007 §10). Hypsometry and the land temperatures are `float32`.
- **`StateSnapshot`** (presentation schema 3, ADR-0018 §4.4). This is a struct of copied `std::vector<float>`s that `make_state_snapshot(PlanetState, SimulationClock)` builds on demand: insolation, surface temperature, snow, sea ice and two climatology aggregates. It is for presentation only.
- **`TerrainSnapshot`** is built once per planet.
- **PSNAP** (ADR-0003 §3.4, schema 5) stores exactly the slow partition and the history of deltas.
- **The scheduler** (`sim/core/scheduler/scheduler.hpp`) is serial. A climate step is one ADR-0006 sub-step of about 43,830 ticks; a reference step is 10 ticks. There is no publication point and no threading between the simulation and its readers.
- **Randomness** is counter-based and keyed by `(world_seed, stream, tick, cell, sample)` (ADR-0003 §3.1). There is no generator state anywhere.
- **Budgets close to rounding** in every column solver (ADR-0007–0010). Per-process terms exist as diagnostics inside the solvers, but no common interface exposes them.
- **Regions:** drainage basins and catchments exist (ADR-0005). Latitude bands are computed inside diagnostics. There is no mask type and no reduction API. `reduce_deterministic_blocks` is the only deterministic sum.

### 1.2 Where the note differs from what is decided

| Note | Decided | Consequence here |
|---|---|---|
| Snapshot "at the end of each tick" | A tick is one minute. A climate step is about 43,830 ticks and a reference step 10 (ADR-0006, ADR-0001 §4.2). Snapshot cadence is independent of solver cadence (spec §8). | Publication is at step boundaries, on a tick cadence of its own (§4.1). |
| Snapshot carries "RNG state" | There is no RNG state. Draws are keyed by seed, stream, tick and cell (ADR-0003 §3.1). | The snapshot carries the world seed and scenario hash instead (§4.2). |
| Views over "float32 SoA" | The slow reservoirs are float64 (ADR-0002, ADR-0007 §10). | Fields are published in their registered type (§4.2). |
| "Zero-copy" views | Solvers update their arrays in place. | The copy happens once, at publication, into buffers the snapshot owns. Readers then get zero-copy views of those buffers (§4.1). |
| `FieldDesc.version` per field | A change of meaning or units needs a new id (ADR-0003 §3.6). | There is no per-field version, only a registry version (§4.3). |
| Bit-for-bit "any run" | L0 only: same build and platform. L1 is statistical (ADR-0003 §3.1). | The guarantee is stated as L0 (§4.8). |
| Tendency terms as an event's cause weights, e.g. "upstream deforestation runoff" | Human attribution comes from the real-minus-counterfactual difference, never from bookkeeping (spec §15.1). | Tendencies split a change among **physical processes** only (§4.5). |

The rest of the note's PlanetSim section is consistent with the specification, and this record adopts it.

## 2. Decision drivers

1. **One door.** Every reader outside the solvers sees the planet through the same immutable object: interpretation, exposure aggregation, observation, presentation, tests and tools.
2. **Reading never changes the world.** Publishing and reading must leave every state hash, golden save and replay unchanged.
3. **The simulation never waits on a reader**, and a slow reader can never make a fast reader wrong.
4. **Testable without physics.** A detector or reduction must be testable on a hand-built snapshot.
5. **Truth, never a guess, at this boundary.** Observation is a consumer of truth with an interface fixed here. Its model belongs to later records.
6. **No new determinism scope and no new persistent state.** PSNAP schema 5 is untouched.
7. **Cheap.** Publication must cost under 1 % of the ADR-0001 climate-mode budget at L5 and L6, the same bound the society step has (spec §29.5).

## 3. Options considered

### 3.1 How readers get state

| Option | For | Against |
|---|---|---|
| A. Readers take `const PlanetState&` | No copy | Readers see half-finished steps. A reader on another thread races the solver. It breaks spec §31.1 and §29.2. |
| B. Double buffer of the whole state, swapped each step | Bounded memory | Every step copies everything, including fields nobody reads. The swap must wait for the last reader of the old buffer, so the simulation can wait. |
| **C. Immutable published snapshot, reference-counted, latest-wins** | The simulation never waits. Old snapshots die when their last reader lets go. Each reader holds a consistent state. | One copy per publication, with memory proportional to the number of snapshots held. |
| D. Copy-on-write fields inside `PlanetState` | Only fields that change are copied | Every solver write must check a reference count. That puts the reading concern inside every kernel, against ADR-0002's plain arrays. |

**C** is chosen. The copy is bounded and measured in §6. D can be revisited behind the same interface if the copy ever shows in a profile.

### 3.2 How accumulated fluxes are delivered

| Option | For | Against |
|---|---|---|
| A. Totals since the previous publication | What the note describes | A reader that skips a snapshot loses that interval for good. With latest-wins (§3.1 C), skipping is normal. |
| **B. Running totals since an epoch; readers difference two snapshots** | Correct for any skip pattern. One rule for all readers. | Needs float64 and an epoch id. A difference across epochs is meaningless and must be refused. |

**B** is chosen.

### 3.3 Where masks and reductions live

In `sim/core` (A), every domain sees them, but the masks need terrain and drainage, which `sim/core` must not depend on. In the solvers (B), the reductions would be duplicated across consumers. **In a read-side library next to `sim/presentation` (C)** is chosen.

## 4. Decision

### 4.1 Publication

At the end of a scheduler step, after the last registered process, the run may **publish** a `PublishedSnapshot`:

- **When.** It publishes at every climate step. In reference mode it publishes at every `publish_every_ticks`, a positive multiple of the reference step that is a run setting (default 60 ticks, one simulated hour). A weather window, once it exists, behaves like reference mode. The publication schedule depends only on the tick sequence, never on wall clock, frame rate or readers (spec §8).
- **How.** The snapshot is built by copying the published fields into buffers it owns. It is then frozen and handed to a `SnapshotChannel`: a single-producer `std::atomic<std::shared_ptr<const PublishedSnapshot>>`. Readers call `latest()` and keep the pointer as long as they need it. The simulation never blocks. A reader that falls behind simply gets the newest snapshot next time.
- **What is copied.** The default set is the slow and climatology partitions, the derived fields marked `published` (§4.3), the active accumulators (§4.4) and, in reference mode, the fast fields. A run can narrow the set. A field is never partly copied.
- **Not persistent.** A `PublishedSnapshot` is an in-process object, like `StateSnapshot`. It is never written to disk. PSNAP and the ADR-0003 history remain the only saved state.

### 4.2 `PublishedSnapshot`

```cpp
struct PublishedHeader {
    std::uint32_t contract_version;     // this record's schema, starts at 1
    std::uint32_t field_registry_version;
    SimulationTick tick;                // end of the step just finished
    std::uint64_t step_index;
    SimulationMode mode;
    std::optional<ClimateSubstep> substep;
    std::uint64_t world_seed;           // no RNG state exists (ADR-0003 §3.1)
    std::uint64_t scenario_hash;
    std::uint32_t mesh_level;
    std::uint32_t atmosphere_layers;
    std::uint64_t accumulator_epoch;    // §4.4
    SimulationTick accumulator_epoch_tick;
};

class PublishedSnapshot {
public:
    const PublishedHeader& header() const noexcept;
    const PlanetMesh& mesh() const noexcept;           // the shared immutable mesh
    bool has(FieldId) const noexcept;
    const FieldDescriptor& descriptor(FieldId) const;
    template <class T> FieldView<T> view(FieldId) const;  // throws on type mismatch
};

template <class T> struct FieldView {
    FieldId id;
    std::uint32_t layers;
    std::span<const T> values;           // layer-major, then cell- or edge-major
    std::span<const T> layer(std::uint32_t) const;
};
```

- **Native types.** Fields keep their registered `FieldDataType`. `view<float>` on a float64 field throws. `to_float32(view)` is the one explicit, copying narrowing, for consumers such as presentation that want float32.
- **Layout.** The layout is the registry's: the order of `Field2D`, `Field3D` and `EdgeField`. Spans are over the snapshot's own 64-byte-aligned buffers.
- **No partial state.** Every field in a snapshot is from the same tick, and accumulators are consistent with it.

`StateSnapshot` and `TerrainSnapshot` stay as they are. Once this record is implemented, `make_state_snapshot` may be rebuilt on top of `PublishedSnapshot` without changing its schema. That is a separate task under ADR-0018.

### 4.3 Field descriptors

`FieldDescriptor` gains three members. The existing ones are unchanged.

```cpp
enum class FieldStatistic : std::uint8_t {
    instantaneous,   // state at the tick
    step_mean,       // mean over the step that produced it (e.g. sub-step insolation)
    accumulated,     // running total since the accumulator epoch (§4.4)
    climatology,     // a statistic over many steps (ADR-0001 §4.1)
};
enum class FieldCadence : std::uint8_t { every_step, climate_step, reference_step, static_after_build };

struct FieldDescriptor {
    // ... existing: id, name, partition, layout, data_type, layers, units
    FieldStatistic statistic;
    FieldCadence cadence;
    double valid_min, valid_max;   // physical bounds a test may assert, not clamps
    bool published;                // derived fields only: copied by default (§4.1)
};
```

- **Units** stay an SI string, checked against a closed list in a unit test. A strong unit type is not introduced for this.
- **No per-field version.** A change of statistic or units is a change of meaning and needs a new id (ADR-0003 §3.6). Widening `valid_min` or `valid_max` is allowed. Narrowing them is a change of meaning.
- **Coupling metadata.** Owner, producers, consumers and budget (spec §12) stay in documentation for now. Adding `owner` to the table is deferred until a consumer needs it.
- The append-only CI check now also compares `statistic`, `cadence` and `units` with the baseline for every existing id.

### 4.4 Accumulators

A flux that consumers integrate, such as precipitation, runoff, an energy flux or an emission, is published as an **accumulated** field:

- a per-cell float64 running total of the flux × time, in J/m² or kg/m², since the **accumulator epoch**;
- updated by the process that computes the flux, inside its step, by adding `flux × step duration`. Within the step it uses the same quantity the budget uses, so the total and the budget agree to rounding;
- registered in the **derived** partition. It is never persisted, never part of `state_hash` and never read by a solver.

A reader wanting the total over an interval subtracts two snapshots: `interval(a, b)`, which throws if their `accumulator_epoch` differs. The epoch starts at scenario construction and at every load, and its id increments each time. A difference across a load is therefore refused, never wrong.

Precision: 300 W/m² over 1,000 years is about 10¹³ J/m², so float64 still resolves 10⁻³ J/m². A run longer than 10⁴ years starts a new epoch at a fixed tick multiple recorded in the manifest.

Climatologies, rolling windows and percentiles over time belong to the consumer, not to PlanetSim. The note says the same. Existing climatology fields remain as ADR-0001 defines them.

### 4.5 Tendency terms

For a field with a closed budget, a run may enable **tendency terms**: one accumulated field per `(target FieldId, ProcessTag)` holding that process's contribution to the change in the target since the epoch.

- They are registered in a `tendency_registry` next to the field registry. Ids are append-only, from a reserved range (`0x00F0'xxxx`). Each entry names its target field and process.
- **Closure.** For every target, the sum of its tendencies over an interval equals the change in the target over that interval, to rounding. A test asserts this.
- **Off by default.** They are enabled per run and recorded in the run manifest. Turning them on must not change any state hash.
- **Physical processes only.** A tendency says how much of a change came from absorbed sunlight, emission, sensible heat, latent heat of melt or transport. It does **not** say which civilization or which human flux caused it. That is the real-minus-counterfactual difference of spec §7.1 and §15.1. An event record's "causes" therefore come from two sources: the physical split here and the human attribution there.
- The first targets are the surface and column energy budgets of ADR-0007–0010, which already close to rounding. Water budgets follow with M7–M9.

### 4.6 Regions, masks and reductions

`CellMask` is an immutable per-cell weight in [0, 1] with a stable name and id, built once per planet:

| Mask | Weight | Source |
|---|---|---|
| `land`, `ocean` | the ADR-0005 land fraction and its complement | terrain |
| `latitude_band(k)` | 1 inside the band | mesh |
| `drainage_basin(b)` | 1 in the basin | ADR-0005 drainage |
| `lowland(h)` | fraction of the cell's hypsometry below `h` above sea level | ADR-0005 hypsometry |
| `region(r)`, `territory(c)` | reserved: defined by ADR-0012 and ADR-0019 | scenario, civilization |

Reductions run over a `FieldView` and a mask:

```cpp
double area_mean(FieldView<T>, const CellMask&);       // Σ w a x / Σ w a
double area_total(FieldView<T>, const CellMask&);      // Σ w a x
double masked_max(FieldView<T>, const CellMask&);      // over w > 0
double masked_min(FieldView<T>, const CellMask&);
double area_quantile(FieldView<T>, const CellMask&, double q);
```

- Sums use per-block partials combined in block order (ADR-0002 §4.6), so the result is bit-identical for any worker count.
- `area_quantile` sorts by value, with ties broken by cell id, and interpolates on cumulative area. This makes it a deterministic function of the snapshot.
- A reduction over a layered field takes a layer index. Edge fields are not reduced here.

### 4.7 Truth and observation

The snapshot is **truth**. This record fixes only the interface through which truth becomes an observation. The observation model is decided later, by spec §26.1 item 4 and ADR-0019: instruments, coverage, staleness, filtering and knowledge stages.

```cpp
template <class T> struct ObservedField {
    FieldId id;                       // the truth's id: same identity (spec §6)
    std::vector<T> estimate;          // same layout as the truth
    std::vector<T> sigma;             // one-sigma uncertainty, same units
    SimulationTick observed_tick;
};

class ObservationOperator {         // implemented by later records
public:
    virtual ObservedField<double> observe(const PublishedSnapshot& truth,
                                          FieldId, const ObservationContext&) const = 0;
};
```

- **Field identity.** An observed field carries the id of the field it estimates. This settles the field-identity rule that spec §26.1 item 4 asks to be decided "before the field set grows further". `EstimatedState` is a set of `ObservedField`s, so a projection run can start from it (spec §7.2).
- **Determinism.** Noise uses the keyed RNG with a reserved observation stream range that no simulation stream uses (as in ADR-0018 §4.8 for presentation).
- **No way back.** Nothing in the read-side library can write to `PlanetState`. The "knowledge has no physical effect" assertion of spec §17.2 becomes a test against this interface.
- **P0 ships one operator, `IdentityObservation`** (estimate = truth, sigma = 0), for tests. The climate lab keeps its own true-state path (spec §17.2). It does not go through an operator.
- Social observation (the note's *population knowledge*) reads `SocietyState`, not planetary fields. It is out of scope here. ADR-0019 may reuse the `ObservedField` shape keyed by cohort.

### 4.8 The read-side library and synthetic snapshots

All of the above except publication (§4.1, which needs `PlanetState`) lives in a new static library, **`sim/readout/` (`PlanetSimReadout`)**:

- It depends on `sim/core` and the mesh only. It must not include `planet_state.hpp` or any solver header, and the CI include check of ADR-0018 V2 (`tools/ci/check_presentation_includes.py`) is extended to it.
- `sim/presentation` may depend on it. It must not depend on `sim/presentation`.
- `SnapshotBuilder` constructs a `PublishedSnapshot` from hand-supplied fields, without a `PlanetState`. It validates each field against the registry: layout, data type, layer count, and values within `[valid_min, valid_max]` unless the test opts out. Detectors and reductions are tested this way, which is the note's "tests inject synthetic fields through the same interface".

Determinism is L0 (ADR-0003 §3.1): the same build, platform, seed and commands give the same sequence of published snapshots, bit for bit, for any worker count or reader behaviour. Across builds, only statistical equivalence holds.

## 5. Validation plan

| ID | Check | Where |
|---|---|---|
| V1 | Publishing at every step leaves golden saves, replay hashes and the 250-year CI hashes unchanged | regression tests |
| V2 | Each published field is bitwise equal to the state at its tick, and the header matches the clock and run | unit test |
| V3 | A snapshot held across later steps is unchanged, and a reader holding it never blocks `step()` | unit test with a deliberately slow reader thread; TSan job when added |
| V4 | `interval(a, b)` of an accumulator equals the closed budget over that interval to rounding, for every skip pattern tried, and is refused across epochs | unit test |
| V5 | Every reduction is bit-identical for 1, 2, 4 and 8 workers and agrees with a brute-force serial version | unit test, L4–L6 |
| V6 | Tendency terms sum to the change in their target to rounding. Enabling them leaves every state hash unchanged. | unit test, regression test |
| V7 | `SnapshotBuilder` rejects a wrong layout, type, layer count or out-of-range value, naming the field | unit test |
| V8 | `sim/readout` reaches no `planet_state.hpp` or solver header | CI include check |
| V9 | The new descriptor members are covered by the append-only registry check | CI script |
| V10 | Two runs differing only in `ObservationContext` and the operator used are bit-identical in `SlowState` (spec §17.2) | regression test |
| V11 | Publication at L5 and L6 costs under 1 % of a climate step, and under 5 % of a reference hour at the default cadence | performance test, Release |

## 6. Consequences

- Interpretation, exposure, observation and presentation each read one immutable object. None of them needs `PlanetState`, and each is testable on a hand-built snapshot.
- The simulation can move to a worker thread without changing any reader (ADR-0018's R2 "live stepping"), because readers already get immutable published snapshots.
- **Memory.** At L6 (40,962 cells) with three layers, a snapshot is about 12 MB. The slow partition is about 4.5 MB (about 108 bytes per cell: hypsometry in float32 and the float64 reservoirs). The climatology is about 8 MB (4 fields × 12 months in float32). Accumulators add 8 bytes per cell each. Readers holding a few snapshots cost tens of MB. A run that does not need the climatology every step can narrow the set (§4.1).
- **Time.** Copying about 12 MB takes a few milliseconds. The L6 climate step's budget is one second (5 years per minute, ADR-0001), so this is under 1 %. In reference mode, publishing every simulated hour copies on one step in six.
- Accumulators and tendency terms add a few lines to each process that owns a flux, inside code that already computes the budget term. Without them, a consumer would have to sample rates and integrate them itself, which is wrong whenever it skips a step.
- The note's "zero-copy float32" becomes "one copy at publication, native types". Presentation keeps narrowing to float32 itself, as it does now.
- Two snapshot-like objects coexist for a while: `StateSnapshot`, for presentation, and `PublishedSnapshot`, the general contract. Neither is persistent. Folding the first into the second is a later ADR-0018 task.

## 7. Milestone mapping

| Track | Uses this record for |
|---|---|
| P0 (M6–M13) | Each new flux registers its accumulator and, where the budget closes, its tendencies. New fields declare statistic, cadence and range. |
| R2 onwards | Live stepping reads `PublishedSnapshot` from a channel. `StateSnapshot` may be rebuilt on it. |
| §26.1 item 4, ADR-0019 | `ObservationOperator` implementations, `EstimatedState` as `ObservedField`s, the fog channels |
| ADR-0012, S1 | `region(r)` masks. `ExposureState` is computed from published snapshots and reductions (spec §29.4). |
| Interpretation layer (no ADR yet) | Detectors on truth, accumulators for event magnitudes, tendencies for physical cause weights |
| M14.1, M14.2 | Real and counterfactual instances publish on the same schedule, so their difference is snapshot-by-snapshot. Projection members publish too. |

## 8. Open questions

1. Is one simulated hour the right default reference-mode cadence, or should it follow the presentation's frame budget? The cadence must stay a tick multiple either way.
2. Should accumulators survive a load, through a sidecar file (spec §27) rather than PSNAP, so that an event spanning a save keeps its total?
3. Which tendency targets come after the energy budgets: water at M7, or carbon at M12?
4. Should `owner` (spec §12) become a descriptor member now, so the coupling registry can be checked mechanically?
5. Does the interpretation layer need its own ADR before S1, or is it covered by ADR-0012 and ADR-0013?

## 9. Proposed tasks

None changes a solver's numerics, a field's meaning or the PSNAP format, so like R1 they can run alongside M6-04.

1. **Descriptor members** (§4.3): add the members, fill them for the 19 existing fields, extend the registry check (V9).
2. **Published snapshot and channel** (§4.1, §4.2, §4.8): `sim/readout`, `PublishedSnapshot`, `SnapshotChannel`, `SnapshotBuilder`, publication in `planet_run` (V1–V3, V7, V8, V11).
3. **Masks and reductions** (§4.6): V5.
4. **Accumulators** (§4.4): the surface and column energy fluxes first, then insolation (V4).
5. **Tendency terms** (§4.5): surface and column energy (V6).
6. **Observation interface** (§4.7): `ObservedField`, `ObservationOperator`, `IdentityObservation` and V10.

## 10. References

- Design Record v1.4 §46.1 (the five boxes), §46.3 (PlanetSim: field exposure), §46.6 (interpretation layer).
- Specification v0.8 §6, §7.1–7.2, §8, §12, §15.1, §17, §26.1 item 4, §29.4, §31.1, §32.2.
- ADR-0001 §4.1, ADR-0002 §4.6, ADR-0003 §3.1–3.6, ADR-0005, ADR-0006, ADR-0018 §4.1–4.8.
