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

## Phase 2, a first neural test (2026-10-29)

A decorative rock, image to mesh, outside the repository's code paths
(outputs in `assets/generated/rock_granite_v1/`, not tracked).

**The concept image:** `generators/comfyui/text_to_image.py` asks a ComfyUI
server (here over an SSH tunnel to the RTX 5070 Ti machine) for an image
from FLUX.2 [klein] 4B (Apache 2.0: the only FLUX.2 model licensed for
commercial use), with the Qwen3 4B text encoder, 4 steps, a fixed seed,
and writes a provenance record. 98 s cold, including loading the model.

**Image to 3D: Stable Fast 3D** (Stability AI Community Licence: commercial
use allowed up to USD 1 M annual revenue; the weights are gated on Hugging
Face). Installed in its own virtual environment outside the repository, on
each machine:

| GPU | PyTorch | Texture | Time | Peak GPU memory |
|---|---|---|---|---|
| RTX 2080 Ti, 11 GB | 2.4.1, CUDA 12.4 | 1 024 px | 17.6 s | 10.8 GB (fits only with `expandable_segments`) |
| RTX 5070 Ti, 16 GB | 2.14.1, CUDA 13.0 | 1 024 px | 6.9 s | 7.0 GB |
| RTX 5070 Ti, 16 GB | 2.14.1, CUDA 13.0 | 2 048 px | 7.3 s | 8.9 GB |

The output is a valid GLB (about 40 000 triangles, base colour and normal
maps, one roughness value), but the quality is limited: the faceted rock
became a smooth egg, the delighting washes the colours out, and the sides
the image does not show are invented with a repetitive pattern. As the
proposal says, neural meshes are candidates: they need a clean-up (scale,
origin, decimation) and human approval.

**Setup notes:**
- The 2080 Ti needs SF3D's kernels built without pip's build isolation
  (`--no-build-isolation`, `TORCH_CUDA_ARCH_LIST=7.5`); the 5070 Ti
  (Blackwell, compute capability 12.0) needs a CUDA 13 PyTorch and toolkit.
- The 5070 Ti machine's driver was updated without a reboot (NVML
  595.99 against the kernel module's 595.91): `nvidia-smi` fails, and
  PyTorch's `expandable_segments` allocator, which calls NVML, fails too.
  A reboot clears it.
- ComfyUI shares that GPU: `POST /free` with `unload_models` releases its
  memory before an SF3D run.
- That machine's network needs its Aruba portal login: without it, HTTPS
  is intercepted (a `securelogin.arubanetworks.com` certificate) and pip
  and git fail certificate checks. Never work around that by disabling
  verification.

## Not yet tested

- **The editor import path:** Godot's scene import (`.import` files,
  import hints such as `-col` colliders), as the game would ship assets.
  Runtime loading was used here, so no project file changes.
- **LODs and collision shapes,** and the triangle and draw-call budgets on
  real scenes.
- **The rest of Phase 2:** the clean-up of a neural mesh for the game, a
  validator profile for decorative assets, and other models. TRELLIS.2
  needs 24 GB officially and its project page excludes commercial use;
  Hunyuan3D-2.1's licence excludes the EU.
- **Phases 3 and 4:** factory assembly, routing, and the compile from
  assembly to network node (ADR-0031 §3: PlanetSim's, not Godot's).
- **CI:** Blender is not in CI; these checks run by hand for now.
