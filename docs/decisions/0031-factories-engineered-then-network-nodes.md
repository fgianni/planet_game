# ADR-0031 — Factories: engineered assemblies that compile to network nodes

- **Status:** Proposed (2026-10-29). Not to be implemented before P1, and
  not before it is accepted.
- **Date:** 2026-10-29
- **Milestone:** none in P0. P1 (First Civilization) and the asset
  pipeline that serves it.
- **Context documents:** Design Record v1.4 §9.1 (physical flows first),
  §9.3 (energy), §9.4 (transport and infrastructure), §15 (P1 — First
  Civilization), §21.3 (dependency rules);
  `docs/assetforge_document/ASSET_PIPELINE_SPEC.md` (AssetForge, version
  1.0, 29 October 2026)
- **Related:** ADR-0018 (the presentation boundary), ADR-0020 (proposed:
  the planet's read contract), ADR-0022 (player placement and zoning),
  ADR-0012 and ADR-0014 (reserved: society state and regions, player
  commands)

## 1. Context

**The AssetForge proposal** describes an offline asset pipeline.
GPT/Codex write asset specifications and procedural Blender generators;
neural image-to-3D backends make irregular meshes; Blender validates and
exports GLB; Godot assembles factories at runtime from functional
blueprints with typed ports. PlanetSim stays authoritative, and no
language or neural model ships in the game.

**Two points do not match this repository's records:**
- **It assumes an existing "engineering sandbox"** of Materials,
  Processes, Components, typed Ports, Connections and hierarchical
  Blueprints. Neither the design record nor the specification has one.
  PlanetSim today is planet physics (P0); the human systems start at P1.
- **Its scale.** The design record wants an economy "detailed enough for
  physical constraints to matter without becoming a factory-optimization
  game" (§9.1). Factories are nodes of a logistics network (§9.4), next to
  cities, farms, mines, ports and power plants. AssetForge's first proof
  of concept is a tank, pump, reactor, generator and cooler chain with
  routed pipes.

**The resolution discussed with the user:** a factory is both. It is
engineered as an assembly of modules, and once assembled it becomes a
network node.

## 2. Decision drivers

1. The network and the economy must scale to many factories (§9.4)
   without simulating each machine.
2. Human systems act on the planet only through explicit interfaces such
   as emissions and land use (§21.3).
3. PlanetSim is the only source of truth; the presentation observes it
   (ADR-0018).
4. The game must not become a factory-optimization game (§9.1).
5. Saved games must reproduce their factories exactly (ADR-0003's
   determinism).

## 3. Proposal

1. **Two levels.**
   - **The assembly** is a design-time object: modules from a library,
     with typed ports and connections (the functional blueprint), a
     physical layout and a visual skin, as AssetForge separates them.
   - **The node** is what runs: a factory in the logistics network with
     aggregate properties: input and output rates, efficiency, energy use,
     emissions, footprint, labour, capital and wear.
2. **The compile from assembly to node is PlanetSim's, one way only.**
   - PlanetSim (C++) computes the node's summary from the functional
     blueprint and the module definitions. Godot never computes rates,
     efficiencies or emissions.
   - Godot's layout solver places meshes and routes pipes and cables for
     display, and reads its results from PlanetSim's state frames.
   - A node is recompiled only when its design changes, or when its state
     does (wear, repairs, a module replaced).
3. **The compile rules carry a version,** as the PSNAP schema does. A
   saved game records the version, so its factories compile to the same
   nodes.
4. **Engineering is optional and bounded.**
   - Ready-made templates are good enough for normal play.
   - A custom design gains modestly, with diminishing returns, bounded by
     technology level, local resources and climate.
   - Designing is a choice for players who enjoy it, never a requirement.
5. **AssetForge's role.** Its asset contract (geometry, functional
   anchors, animation controls, material slots, validated metadata), its
   provenance registry and its Blender → GLB → Godot validation serve the
   assembly level's visuals. Neural meshes are visual candidates only,
   for decorative and organic assets.

## 4. What it leaves to P1

- **The module library and the compile:** which modules exist, their
  ports and how a summary is computed from them. This belongs with ADR-0012
  and ADR-0014 (society state, regions and player commands).
- **How fine the node's summary is:** whether a node keeps one process or
  several (a refinery's products, for example).
- **What the player sees at each camera altitude** (ADR-0022): the
  assembly up close, the node from afar.
- **The licences.** Hunyuan3D-2.1's licence excludes the EU, and
  TRELLIS.2's project page carries a research and non-commercial
  disclaimer (AssetForge §2). A procedural-only path must work without
  either.

## 5. Consequences

- **The economy and the network scale** with the number of factories,
  not of machines.
- **Engineering can be deep without being required:** templates keep the
  default game at the design record's scale.
- **A second source of truth is ruled out** by construction: the summary
  is PlanetSim's, and the visuals only read it.
- **The cost:** a change to the compile rules changes every existing
  node, so the rules need versioning and saved games need to record the
  version they used.

## 6. Validation, when implemented

| ID | Check |
|---|---|
| V1 | The same blueprint and compile version give a bit-identical node summary, on any worker count and after a save and load |
| V2 | A node's mass and energy close: inputs equal outputs plus losses, emissions included |
| V3 | No Godot code path writes a rate, an efficiency or an emission (a review check and a test on the binding layer) |
| V4 | A template factory performs within a stated margin of a custom design of the same technology level |
