#!/usr/bin/env python3
"""AssetForge Phase 1 (docs/assetforge_document §5, ADR-0031): build one
asset from its specification, validate it, and optionally check it in
Godot, check that it rebuilds to the same bytes and render a preview.

    build_asset.py --spec assets/specifications/electric_generator.json
                   [--out assets/generated] [--blender PATH] [--godot PATH]
                   [--reproducible] [--preview]

Writes OUT/<id>/<id>.glb, .manifest.json, .provenance.json (and
<id>.preview.png). Exits 1 if any step fails.
"""

import argparse
import functools
import filecmp
import json
import os
import shutil
import subprocess
import sys
import tempfile

print = functools.partial(print, flush=True)   # in order with the subprocesses' output

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GENERATOR = os.path.join(ROOT, "tools/asset_pipeline/generators/blender/machine_generator.py")
PREVIEW = os.path.join(ROOT, "tools/asset_pipeline/generators/blender/render_preview.py")
VALIDATOR = os.path.join(ROOT, "tools/asset_pipeline/validators/validate_asset.py")
GODOT_CHECK = os.path.join(ROOT, "tools/asset_pipeline/godot/import_check.gd")
STATE = os.path.join(ROOT, "tools/asset_pipeline/tests/generator_state.json")


def commit():
    result = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True)
    return result.stdout.strip() or "unknown"


def blender_build(blender, spec, out):
    result = subprocess.run([blender, "-b", "--factory-startup", "--python", GENERATOR, "--",
                             "--spec", spec, "--out", out, "--commit", commit()],
                            capture_output=True, text=True)
    if result.returncode != 0 or "assetforge: wrote" not in result.stdout:
        print(result.stdout[-4000:], result.stderr[-4000:])
        return False
    return True


def godot_check(godot, out, spec, asset):
    # A throwaway project: runtime loading needs no editor import, and no
    # repository project changes.
    with tempfile.TemporaryDirectory() as project:
        with open(os.path.join(project, "project.godot"), "w") as stream:
            stream.write('config_version=5\n\n[application]\nconfig/name="assetforge_check"\n')
        shutil.copy(GODOT_CHECK, project)
        result = subprocess.run(
            [godot, "--headless", "--path", project, "--script", "import_check.gd", "--",
             os.path.join(out, asset + ".glb"), os.path.join(out, asset + ".manifest.json"),
             spec, STATE], capture_output=True, text=True)
    lines = [line for line in result.stdout.splitlines()
             if line.startswith(("PASS", "FAIL", "assetforge"))]
    print("\n".join(lines))
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spec", required=True)
    parser.add_argument("--out", default=os.path.join(ROOT, "assets/generated"))
    parser.add_argument("--blender", default=shutil.which("blender") or "blender")
    parser.add_argument("--godot", help="a Godot 4 binary: run the import check")
    parser.add_argument("--reproducible", action="store_true",
                        help="build a second time and compare the bytes")
    parser.add_argument("--preview", action="store_true", help="render a preview image")
    args = parser.parse_args()

    spec = os.path.abspath(args.spec)
    asset = json.load(open(spec))["id"]
    out = os.path.join(os.path.abspath(args.out), asset)
    ok = True

    print(f"== build {asset} (Blender)")
    if not blender_build(args.blender, spec, out):
        print("FAIL build")
        return 1
    print(f"== validate")
    ok &= subprocess.run([sys.executable, VALIDATOR, spec, out]).returncode == 0
    if args.godot:
        print(f"== Godot import check")
        ok &= godot_check(args.godot, out, spec, asset)
    if args.reproducible:
        print(f"== rebuild and compare")
        with tempfile.TemporaryDirectory() as again:
            if not blender_build(args.blender, spec, again):
                print("FAIL rebuild")
                ok = False
            else:
                for suffix in (".glb", ".manifest.json", ".provenance.json"):
                    same = filecmp.cmp(os.path.join(out, asset + suffix),
                                       os.path.join(again, asset + suffix), shallow=False)
                    print(f"{'PASS' if same else 'FAIL'} identical {asset}{suffix}")
                    ok &= same
    if args.preview:
        print(f"== preview")
        image = os.path.join(out, asset + ".preview.png")
        result = subprocess.run([args.blender, "-b", "--factory-startup", "--python", PREVIEW, "--",
                                 "--glb", os.path.join(out, asset + ".glb"), "--out", image],
                                capture_output=True, text=True)
        rendered = result.returncode == 0 and os.path.exists(image)
        print(f"{'PASS' if rendered else 'FAIL'} preview {image if rendered else result.stderr[-2000:]}")
        ok &= rendered
    print(f"== {asset}: {'all steps passed' if ok else 'FAILED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
