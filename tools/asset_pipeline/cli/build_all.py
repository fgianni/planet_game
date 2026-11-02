#!/usr/bin/env python3
"""AssetForge Phase 4 (docs/assetforge_document §9): build every asset
specification, skipping those whose inputs have not changed, and validate
all of them.

    build_all.py [--blender PATH] [--godot PATH] [--out assets/generated] [--force]

An asset's build key is the SHA-256 of what determines its bytes: its
specification, its Blender generator script, the Blender version, and for a
neural candidate its source mesh and the records of the steps before it.
A build whose key matches the one stored beside the GLB is reused; the
validator (and the Godot check, with --godot) runs on every asset every
time, and a missing preview is rendered. A cached asset keeps its last
Godot result (checks.json, beside the build key) when --godot is not given. Writes OUT/report.json, which the
catalogue (catalogue.py) reads. Exits 1 if any asset fails.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_asset  # noqa: E402  (the single-asset steps)

ROOT = build_asset.ROOT


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, "rb") as stream:
        return sha256_bytes(stream.read())


def blender_version(blender):
    result = subprocess.run([blender, "--version"], capture_output=True, text=True)
    first = result.stdout.splitlines()[0] if result.stdout else ""
    return first.strip() or "unknown"


def build_key(spec_path, spec, blender_line):
    """The SHA-256 of an asset's build inputs, or None if one is missing."""
    parts = [b"spec", open(spec_path, "rb").read(),
             b"generator", open(build_asset.GENERATORS[spec["generator"]], "rb").read(),
             b"blender", blender_line.encode()]
    source = spec.get("source")
    if source:
        for key in ("mesh", "concept_provenance", "image_to_3d_provenance"):
            path = os.path.join(ROOT, source[key])
            if not os.path.exists(path):
                return None
            parts += [key.encode(), sha256_file(path).encode()]
    return sha256_bytes(b"\0".join(parts))


def run_quiet(command):
    result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--blender", default=shutil.which("blender") or "blender")
    parser.add_argument("--godot", help="a Godot 4 binary: run the import checks")
    parser.add_argument("--out", default=os.path.join(ROOT, "assets/generated"))
    parser.add_argument("--force", action="store_true", help="rebuild even when cached")
    args = parser.parse_args()

    out_root = os.path.abspath(args.out)
    blender_line = blender_version(args.blender)
    rows = []
    for spec_path in sorted(glob.glob(os.path.join(ROOT, "assets/specifications/*.json"))):
        started = time.time()
        spec = json.load(open(spec_path))
        asset = spec["id"]
        out = os.path.join(out_root, asset)
        row = {"id": asset, "type": spec["type"], "specification": os.path.relpath(spec_path, ROOT)}
        code, text = run_quiet([sys.executable, build_asset.CHECK_SPEC, spec_path])
        if code != 0:
            row.update(status="FAIL", step="specification", detail=text.strip()[-500:])
            rows.append(row)
            continue
        key = build_key(spec_path, spec, blender_line)
        if key is None:
            row.update(status="SKIP", step="sources", detail="its generated sources are not on this machine")
            rows.append(row)
            continue
        key_path = os.path.join(out, "build_key.json")
        stored = json.load(open(key_path)).get("key") if os.path.exists(key_path) else None
        glb = os.path.join(out, asset + ".glb")
        if args.force or stored != key or not os.path.exists(glb):
            if not build_asset.blender_build(args.blender, spec_path, out):
                row.update(status="FAIL", step="build")
                rows.append(row)
                continue
            with open(key_path, "w") as stream:
                json.dump({"key": key, "blender": blender_line}, stream, indent=2)
                stream.write("\n")
            row["built"] = "built"
        else:
            row["built"] = "cached"
        code, text = run_quiet([sys.executable, build_asset.VALIDATOR, spec_path, out])
        found = re.search(r"(\d+) passed, (\d+) failed", text)
        row["validator"] = {"passed": int(found.group(1)), "failed": int(found.group(2))} if found else None
        ok = code == 0
        checks_path = os.path.join(out, "checks.json")
        if args.godot:
            godot_ok = build_asset.godot_check(args.godot, out, spec_path, asset)
            row["godot"] = "pass" if godot_ok else "FAIL"
            ok &= godot_ok
            with open(checks_path, "w") as stream:
                json.dump({"key": key, "godot": row["godot"]}, stream, indent=2)
                stream.write("\n")
        elif row["built"] == "cached" and os.path.exists(checks_path):
            checks = json.load(open(checks_path))
            if checks.get("key") == key:
                row["godot"] = checks["godot"] + " (cached)"
        preview = os.path.join(out, asset + ".preview.png")
        if row["built"] == "built" or not os.path.exists(preview):
            subprocess.run([args.blender, "-b", "--factory-startup", "--python", build_asset.PREVIEW,
                            "--", "--glb", glb, "--out", preview], capture_output=True, text=True)
        approval_path = os.path.join(ROOT, "assets/approvals", asset + ".json")
        if os.path.exists(approval_path):
            approval = json.load(open(approval_path))
            current = approval.get("geometry_sha256") == sha256_file(glb)
            row["approval"] = f"{approval['decision']} by {approval['reviewer']}" + ("" if current else " (other bytes)")
        elif spec["type"] == "decorative":
            row["approval"] = "pending"
        row.update(status="PASS" if ok else "FAIL", seconds=round(time.time() - started, 1))
        rows.append(row)

    os.makedirs(out_root, exist_ok=True)
    with open(os.path.join(out_root, "report.json"), "w") as stream:
        json.dump({"blender": blender_line, "assets": rows}, stream, indent=2)
        stream.write("\n")
    print(f"\n{'asset':26} {'status':6} {'build':7} {'validator':10} {'godot':6} approval")
    for r in rows:
        v = r.get("validator")
        print(f"{r['id']:26} {r['status']:6} {r.get('built', '-'):7} "
              f"{(str(v['passed']) + '/' + str(v['passed'] + v['failed'])) if v else '-':10} "
              f"{r.get('godot', '-'):6} {r.get('approval', '-')}")
    failed = [r for r in rows if r["status"] == "FAIL"]
    print(f"assetforge build_all: {len(rows)} assets, {len(failed)} failed, "
          f"{sum(1 for r in rows if r.get('built') == 'cached')} cached")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
