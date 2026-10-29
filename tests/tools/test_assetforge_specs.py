#!/usr/bin/env python3
"""AssetForge: the repository's asset specifications pass check_spec.py,
and deliberately broken copies of them fail with the expected messages.
Standard library only, so it runs in CI without Blender."""

import copy
import glob
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools/asset_pipeline/validators"))
import check_spec  # noqa: E402


def expect(spec, fragment):
    errors = check_spec.check(spec)
    assert any(fragment in error for error in errors), (fragment, errors)


def main():
    paths = sorted(glob.glob(os.path.join(ROOT, "assets/specifications/*.json")))
    assert paths, "no specifications found"
    for path in paths:
        errors = check_spec.check(json.load(open(path)))
        assert not errors, (path, errors)

    machine = json.load(open(os.path.join(ROOT, "assets/specifications/electric_generator.json")))
    broken = copy.deepcopy(machine)
    broken["ports"][0]["type"] = "steam"
    expect(broken, "type must be one of")
    broken = copy.deepcopy(machine)
    broken["ports"][1]["position_m"] = [0.6, 0.0, 1.5]
    expect(broken, "bounding box's surface")
    broken = copy.deepcopy(machine)
    broken["ports"][2]["direction"] = [0.0, -2.0, 0.0]
    expect(broken, "unit vector")
    broken = copy.deepcopy(machine)
    broken["ports"][1]["node"] = broken["ports"][0]["node"]
    expect(broken, "port nodes must be unique")
    broken = copy.deepcopy(machine)
    broken["materials"]["rotor"] = "rotor_steel"
    expect(broken, "MAT_UPPER_SNAKE")
    broken = copy.deepcopy(machine)
    broken["schema"] = "assetforge.machine/9"
    expect(broken, "schema must be one of")

    rock = json.load(open(os.path.join(ROOT, "assets/specifications/rock_granite.json")))
    broken = copy.deepcopy(rock)
    broken["source"]["mesh_sha256"] = "not-a-hash"
    expect(broken, "pinned by its hash")
    broken = copy.deepcopy(rock)
    broken["collision"]["node_suffix"] = "-colonly"
    expect(broken, "-convcolonly")
    print(f"assetforge specifications: {len(paths)} pass, 8 broken copies caught")


if __name__ == "__main__":
    main()
