# AssetForge — Phase 1 prototype

A first test of the AssetForge proposal
(`docs/assetforge_document/ASSET_PIPELINE_SPEC.md`; ADR-0031, proposed).
It builds one procedural machine from its specification, end to end:
specification → Blender → GLB with a manifest and provenance → validation →
Godot. No simulation code, Godot project file or neural model is involved.

```bash
python3 tools/asset_pipeline/cli/build_asset.py \
    --spec assets/specifications/electric_generator.json \
    --blender /snap/bin/blender --godot ../../Godot_v4.7.2-stable_linux.x86_64 \
    --reproducible --preview
```

Outputs go to `assets/generated/<id>/` (not tracked): the GLB, its manifest
and provenance, and a preview image. About 13 s on the development machine.

| File | Role |
|---|---|
| `assets/specifications/electric_generator.json` | The specification: dimensions, typed ports with positions and directions, the rotor's animation (pivot, axis, cycle), material slots and budgets |
| `generators/blender/machine_generator.py` | Builds the asset in Blender from the specification; exports GLB, manifest and provenance. Nothing random, nothing timed |
| `generators/blender/render_preview.py` | A Cycles (CPU) preview for people; outside the reproducibility check |
| `validators/validate_asset.py` | Reads the GLB directly (standard library only): 25 checks |
| `godot/import_check.gd` | Loads the GLB at runtime in a throwaway project; checks ports and animation; drives them from a mock state frame (`tests/generator_state.json`) |
| `cli/build_asset.py` | Runs the steps; `--reproducible` builds twice and compares the bytes |

## What the first run showed (2026-10-29)

Tools: Blender 5.2.2 LTS, Godot 4.7.2, Python 3.10.

- **The asset contract survives the whole chain.** Each port is an empty
  carrying its id and type as glTF extras. They arrive in Godot as node
  metadata (`get_meta("extras")`), at the specified positions (0.00 mm)
  and directions (error ~1e-7).
- **A state frame can drive the visuals.** At 1 500 rpm the rotor turns
  exactly a quarter turn in 10 ms (playback speed = the state's turns per
  second over the animation's own), and the heat material takes its glow
  from the temperature.
- **Builds are byte-reproducible** with a pinned Blender: the GLB, the
  manifest and the provenance are identical between two builds.
- **The checks caught two real generator bugs:**
  - parenting the rotor without Blender's parent inverse moved it 100 mm
    off its axis (the pivot check);
  - Blender's default Bézier keys made the rotor ease in and out. Only the
    Godot check's behaviour test caught it at first: the GLB had the right
    axis, cycle and loop. The validator now checks a constant angular
    speed too. Validators should check behaviour, not only that the
    named parts exist.

## Not yet tested

- **The editor import path:** Godot's scene import (`.import` files,
  import hints such as `-col` colliders), as the game would ship assets.
  Runtime loading was used here, so no project file changes.
- **LODs and collision shapes,** and the triangle and draw-call budgets on
  real scenes.
- **AssetForge Phase 2, the neural backends.** This machine has two RTX
  2080 Ti with 11 GB each, not the RTX 4070 Ti the document assumes; the
  neural models' memory needs and licences (Hunyuan3D-2.1 excludes the
  EU; TRELLIS.2 is research and non-commercial) must be checked first.
- **Phases 3 and 4:** factory assembly, routing, and the compile from
  assembly to network node (ADR-0031 §3: PlanetSim's, not Godot's).
- **CI:** Blender is not in CI; these checks run by hand for now.
