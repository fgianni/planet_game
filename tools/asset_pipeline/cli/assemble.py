#!/usr/bin/env python3
"""AssetForge Phase 3 (docs/assetforge_document §9, the first end-to-end
proof of concept): a blueprint laid out, assembled in Godot from its
modules' built GLBs, driven by a plant state frame, and re-assembled
deterministically after a parameter changes.

    assemble.py --blueprint assets/blueprints/power_chain_v1.json
                [--built assets/generated] [--godot PATH] [--blender PATH]
                [--vary NAME=VALUE] [--preview]

Writes BUILT/assemblies/<id>/layout.json (and preview.png). With --vary,
lays it out again with that parameter changed (it must change and stay
feasible) and then as given (it must give the same bytes as the first).
The modules must be built first (build_asset.py). Exits 1 if a step fails.
"""

import argparse
import filecmp
import functools
import json
import os
import shutil
import subprocess
import sys
import tempfile

print = functools.partial(print, flush=True)   # in order with the subprocesses' output

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
LAYOUT = os.path.join(ROOT, "tools/asset_pipeline/assembly/layout.py")
RENDER = os.path.join(ROOT, "tools/asset_pipeline/assembly/render_assembly.py")
CHECK = os.path.join(ROOT, "tools/asset_pipeline/godot/assembly_check.gd")
PLANT_STATE = os.path.join(ROOT, "tools/asset_pipeline/tests/plant_state.json")


def lay_out(blueprint, out, overrides=()):
    command = [sys.executable, LAYOUT, blueprint, out]
    for item in overrides:
        command += ["--set", item]
    result = subprocess.run(command, capture_output=True, text=True)
    print(result.stdout.strip())
    return result.returncode == 0


def godot_assemble(godot, layout_path, built):
    layout = json.load(open(layout_path))
    with tempfile.TemporaryDirectory() as project:
        with open(os.path.join(project, "project.godot"), "w") as stream:
            stream.write('config_version=5\n\n[application]\nconfig/name="assetforge_assembly"\n')
        shutil.copy(CHECK, project)
        for module in layout["modules"]:
            shutil.copy(os.path.join(built, module["asset"], module["asset"] + ".glb"), project)
        imported = subprocess.run([godot, "--headless", "--path", project, "--import"],
                                  capture_output=True, text=True, timeout=600)
        if imported.returncode != 0:
            print("FAIL Godot import")
            return False, None
        result = subprocess.run(
            [godot, "--headless", "--path", project, "--script", "assembly_check.gd", "--",
             layout_path, os.path.join(ROOT, "assets/specifications"), PLANT_STATE],
            capture_output=True, text=True, timeout=600)
    lines = [l for l in result.stdout.splitlines() if l.startswith(("FAIL", "assetforge"))]
    print("\n".join(lines))
    digest = next((l.split()[-1] for l in lines if " digest " in l), None)
    return result.returncode == 0, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--blueprint", required=True)
    parser.add_argument("--built", default=os.path.join(ROOT, "assets/generated"))
    parser.add_argument("--godot")
    parser.add_argument("--blender", default=shutil.which("blender") or "blender")
    parser.add_argument("--vary", help="NAME=VALUE: re-assemble with a parameter changed")
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()

    blueprint = os.path.abspath(args.blueprint)
    name = json.load(open(blueprint))["id"]
    out = os.path.join(os.path.abspath(args.built), "assemblies", name)
    os.makedirs(out, exist_ok=True)
    layout = os.path.join(out, "layout.json")
    ok = True

    print(f"== lay out {name}")
    if not lay_out(blueprint, layout):
        print(f"== {name}: impossible")
        return 1
    digest = None
    if args.godot:
        print("== assemble in Godot")
        assembled, digest = godot_assemble(args.godot, layout, os.path.abspath(args.built))
        ok &= assembled
    if args.vary:
        print(f"== re-assemble with {args.vary}, then as given")
        with tempfile.TemporaryDirectory() as work:
            varied = os.path.join(work, "varied.json")
            again = os.path.join(work, "again.json")
            feasible = lay_out(blueprint, varied, [args.vary])
            changed = feasible and not filecmp.cmp(layout, varied, shallow=False)
            print(f"{'PASS' if changed else 'FAIL'} the layout changes with {args.vary} and stays feasible")
            ok &= changed
            if args.godot and feasible:
                assembled, varied_digest = godot_assemble(args.godot, varied, os.path.abspath(args.built))
                ok &= assembled and varied_digest != digest
            lay_out(blueprint, again)
            same = filecmp.cmp(layout, again, shallow=False)
            print(f"{'PASS' if same else 'FAIL'} laid out again as given: the same bytes")
            ok &= same
            if args.godot:
                assembled, again_digest = godot_assemble(args.godot, again, os.path.abspath(args.built))
                same_digest = assembled and again_digest == digest
                print(f"{'PASS' if same_digest else 'FAIL'} assembled again: the same digest ({again_digest})")
                ok &= same_digest
    if args.preview:
        print("== preview")
        image = os.path.join(out, "preview.png")
        result = subprocess.run([args.blender, "-b", "--factory-startup", "--python", RENDER, "--",
                                 "--layout", layout, "--built", os.path.abspath(args.built),
                                 "--out", image], capture_output=True, text=True)
        rendered = result.returncode == 0 and os.path.exists(image)
        print(f"{'PASS' if rendered else 'FAIL'} preview {image if rendered else result.stderr[-2000:]}")
        ok &= rendered
    print(f"== {name}: {'all steps passed' if ok else 'FAILED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
