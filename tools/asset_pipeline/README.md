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
| `validators/check_spec.py` | Checks a specification before Blender runs (standard library only): the schema and type, `lower_snake` ids, the port types' vocabulary (`mechanical_power`, `electrical_power`, `fluid`, `heat`, `item`, `data`), `PORT_*`/`MAT_*`/`ANIM_*` names, unit directions, ports on the bounding box's surface, unique ids, budgets, a generated mesh pinned by its hash |
| `validators/validate_asset.py` | Reads the GLB directly (standard library only): 25 checks |
| `godot/import_check.gd` | Loads the GLB in a throwaway project, at runtime and through the editor import; checks ports, materials, collision and animations; drives each animation from a mock state frame (`tests/state_frames.json`) |
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

## The five reference modules (2026-10-29)

The proposal's tank, pump, reactor, generator and cooler (§9), procedural,
each from its specification (`assets/specifications/<name>.json`), built
by `machine_generator.py`'s builder for its `family`:

| Module | Ports | Animation and its binding |
|---|---|---|
| `tank_fluid_v1` | fluid out | the sight glass's float, **position**: `fill_fraction` sets it on its 1.6 m travel |
| `pump_centrifugal_v1` | fluid in, fluid out, electrical in | the motor fan, **rotation**: `pump_speed_rpm` |
| `reactor_vessel_v1` | feed in, coolant in, mechanical out | the output shaft: `shaft_speed_rpm`; heat bands tinted by `temperature_K` |
| `generator_electric_v1` | mechanical in, electrical out, coolant in | the rotor: `rotor_speed_rpm`; fins tinted by `temperature_K` |
| `cooler_fan_v1` | electrical in, two coolant outs | two fans: `fan_speed_rpm` |

The ports chain them (tank → pump → reactor → shaft → generator, the cooler
serving both); their shaft heights match (0.8 m). Each animation declares
its `kind` and `binding`; the validator checks a position animation's
linear travel along its axis from its pivot, and the Godot check drives
every animation from a mock state frame per asset
(`tests/state_frames.json`), both ways in. All pass (validator 18–31
checks each, Godot 10–18 each way) and rebuild byte-identically.

**Two lessons:**
- **The exporter's triangle order is not reproducible** for meshes made by
  edit-mode operations (the tank's and reactor's domes): the same
  triangles, in a different order. The generator now rewrites each index
  buffer in a canonical order after export (each triangle rotated to its
  smallest index, which keeps its winding, then sorted). The geometry is
  unchanged.
- **The checks cannot see what a person sees:** the cooler's fans passed
  every check (present, turning at the state's speed) while closed shroud
  caps hid them. The previews caught it. Review the previews, as for the
  neural assets.

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

**The clean-up for the game** (`generators/blender/neural_cleanup.py`,
specification `assets/specifications/rock_granite.json`, type
`decorative`). From the SF3D mesh, checked against its hash in the
specification:
- one object, its origin at the base centre, scaled to 1.0 m tall;
- decimated from 39 648 to 4 749 triangles (budget 5 000), with no visible
  loss next to SF3D's original;
- textures downsized from 2 048 to 1 024 px; one material, named
  `MAT_ROCK_GRANITE`; 378 KB, against SF3D's 1.8 MB;
- a provenance chain: the concept image (model, licence, prompt, seed),
  image to 3D (model and code revisions, licence, GPU) and the clean-up
  (script hash, Blender version), with both licences and
  `"approval": "pending human review"`.

**The collision shape** (2026-10-29): the convex hull of the drawn mesh,
reduced to at most 64 points, exported as `<id>-convcolonly`. Godot's
editor import turns that suffix into a physics body with a
`ConvexPolygonShape3D` and no drawn mesh; its runtime loader ignores the
hint (checked with a probe), so the game must ship assets through the
editor import. The rock's hull has 59 points and encloses the mesh.

The validator's decorative profile (height, footprint centred, origin at
the base, triangle, material and texture budgets read from the embedded
JPEG headers, licences and the chain) passes 18 checks with the collision
shape's four. The Godot check runs **both ways in**, each in a throwaway
project: loaded at runtime (rock 4 checks, generator 18) and imported by
the editor, as the game ships assets (rock 7, with the collision body;
generator 18: the ports' typed extras and the rotor's animation survive
the import). The clean-up is
byte-reproducible from the same SF3D mesh; SF3D itself is not run by the
pipeline, so the mesh is pinned by its hash rather than rebuilt.

```bash
python3 tools/asset_pipeline/cli/build_asset.py \
    --spec assets/specifications/rock_granite.json \
    --blender /snap/bin/blender --godot ../../Godot_v4.7.2-stable_linux.x86_64 \
    --reproducible --preview
```

**SF3D from the pipeline** (`generators/image_to_3d/sf3d_remote.py`): a
replaceable GPU worker (proposal §8). The client asks the ComfyUI server
sharing the GPU to unload its models, copies the image and the worker
(`sf3d_worker.py`, kept here) over SSH, runs it in SF3D's environment on
the 5070 Ti machine, fetches the mesh and writes the step's provenance
(model and code revisions, licence, GPU, PyTorch, time, peak memory, the
input's and output's hashes). 7.1 s and 8.6 GiB at 2 048 px. The clean-up
reads that record and checks it against the mesh and the concept image.

**SF3D is not reproducible:** two runs on the same image gave different
bytes. So the specification pins the generated mesh by its hash:
generation is a one-off, and everything after it rebuilds exactly.

**Human approval** (`cli/approve_asset.py`): a person's decision on
exactly the bytes they reviewed, written to `assets/approvals/<id>.json`
(tracked), with the asset's, preview's and specification's hashes. The
validator's `--require-approval` fails without an approval of the current
geometry, so a rebuild with different bytes needs a new review. The rock
is pending.

**Setup notes:**
- The 2080 Ti needs SF3D's kernels built without pip's build isolation
  (`--no-build-isolation`, `TORCH_CUDA_ARCH_LIST=7.5`); the 5070 Ti
  (Blackwell, compute capability 12.0) needs a CUDA 13 PyTorch and toolkit.
- The 5070 Ti machine's driver had been updated without a reboot (NVML
  595.99 against the kernel module's 595.91): `nvidia-smi` failed, and so
  did PyTorch's `expandable_segments` allocator, which calls NVML. After
  the reboot (2026-10-29) both work: with it, 1 024 px takes 8.6 s at a
  6.3 GB peak.
- ComfyUI shares that GPU: `POST /free` with `unload_models` releases its
  memory before an SF3D run.
- That machine's network needs its Aruba portal login: without it, HTTPS
  is intercepted (a `securelogin.arubanetworks.com` certificate) and pip
  and git fail certificate checks. Never work around that by disabling
  verification.

## Not yet tested

- **The import path on CI's Godot 4.2.2:** checked on 4.7.2 only; whether
  4.2 imports glTF extras as node metadata is unverified.
- **LODs,** and the triangle and draw-call budgets on real scenes.
- **The rest of Phase 2:** better quality (other inputs or models), and
  CI's approval gate for shipped assets. TRELLIS.2
  needs 24 GB officially and its project page excludes commercial use;
  Hunyuan3D-2.1's licence excludes the EU.
- **Phases 3 and 4:** factory assembly, routing, and the compile from
  assembly to network node (ADR-0031 §3: PlanetSim's, not Godot's).
- **CI:** CTest runs `assetforge_specifications` everywhere (the
  specifications pass, and eight broken copies fail with the expected
  messages). `assetforge_build_<module>` for the five machines (built,
  validated and rebuilt byte-identically, checked in Godot when
  `ASSETFORGE_GODOT` names a binary) is added only where Blender is
  installed; CI has neither Blender nor the rock's generated sources.
