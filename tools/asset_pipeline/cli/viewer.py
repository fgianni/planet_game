#!/usr/bin/env python3
"""AssetForge viewer: a throwaway Godot project showing the assembled plant
and each built asset, to look at (not a check).

    viewer.py --godot PATH [--built assets/generated] [--launch]

Builds BUILT/viewer/ (ignored by git): the assets' GLBs, the plant's layout
(assemble.py must have run), the specifications and state frames in one
catalogue, and tools/asset_pipeline/godot/viewer.gd as its main scene. Imports
it headless; with --launch, opens the window.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
VIEWER = os.path.join(ROOT, "tools/asset_pipeline/godot/viewer.gd")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--godot", required=True)
    parser.add_argument("--built", default=os.path.join(ROOT, "assets/generated"))
    parser.add_argument("--blueprint", default="power_chain_v1")
    parser.add_argument("--launch", action="store_true")
    args = parser.parse_args()

    built = os.path.abspath(args.built)
    project = os.path.join(built, "viewer")
    shutil.rmtree(project, ignore_errors=True)
    os.makedirs(project)

    specifications = {}
    assets = []
    for path in sorted(glob.glob(os.path.join(ROOT, "assets/specifications/*.json"))):
        spec = json.load(open(path))
        name = os.path.splitext(os.path.basename(path))[0]
        glb = os.path.join(built, spec["id"], spec["id"] + ".glb")
        if not os.path.exists(glb):
            print(f"assetforge viewer: {spec['id']} is not built; skipped")
            continue
        shutil.copy(glb, project)
        specifications[name] = spec
        entry = {"id": spec["id"], "type": spec["type"], "specification": name}
        if spec["type"] == "decorative":
            approval = os.path.join(ROOT, "assets/approvals", spec["id"] + ".json")
            decision = json.load(open(approval)) if os.path.exists(approval) else {}
            entry["note"] = (f"a neural candidate, {decision.get('decision', 'pending review')}"
                             f" by {decision.get('reviewer', 'nobody')}")
        assets.append(entry)
    layout_path = os.path.join(built, "assemblies", args.blueprint, "layout.json")
    if not os.path.exists(layout_path):
        raise SystemExit("assetforge viewer: run assemble.py first, for the plant's layout")
    tests = os.path.join(ROOT, "tools/asset_pipeline/tests")
    catalogue = {
        "layout": json.load(open(layout_path)),
        "plant_state": json.load(open(os.path.join(tests, "plant_state.json"))),
        "asset_states": json.load(open(os.path.join(tests, "state_frames.json"))),
        "specifications": specifications,
        "assets": assets,
    }
    with open(os.path.join(project, "catalogue.json"), "w") as stream:
        json.dump(catalogue, stream, indent=1)
    shutil.copy(VIEWER, project)
    with open(os.path.join(project, "viewer.tscn"), "w") as stream:
        stream.write('[gd_scene format=3]\n\n[ext_resource type="Script" path="res://viewer.gd" id="1"]\n\n'
                     '[node name="Viewer" type="Node3D"]\nscript = ExtResource("1")\n')
    with open(os.path.join(project, "project.godot"), "w") as stream:
        stream.write('config_version=5\n\n[application]\nconfig/name="AssetForge viewer"\n'
                     'run/main_scene="res://viewer.tscn"\n\n[display]\n'
                     'window/size/viewport_width=1600\nwindow/size/viewport_height=900\n\n'
                     '[rendering]\nrenderer/rendering_method="gl_compatibility"\n'
                     'anti_aliasing/quality/msaa_3d=2\n')
    imported = subprocess.run([args.godot, "--headless", "--path", project, "--import"],
                              capture_output=True, text=True, timeout=600)
    if imported.returncode != 0:
        raise SystemExit(f"assetforge viewer: import failed\n{imported.stderr[-2000:]}")
    print(f"assetforge viewer: {len(assets)} assets and the plant in {project}")
    if args.launch:
        subprocess.Popen([args.godot, "--path", project], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        print("assetforge viewer: launched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
