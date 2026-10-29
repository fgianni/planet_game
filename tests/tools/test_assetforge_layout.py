#!/usr/bin/env python3
"""AssetForge layout (tools/asset_pipeline/assembly/layout.py), standard
library only so it runs in CI: the power chain lays out feasibly and
deterministically, its shaft mates, a parameter change moves it, and
blueprints that cannot be built are reported impossible, not invented."""

import copy
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools/asset_pipeline/assembly"))
import layout  # noqa: E402


def main():
    blueprint = json.load(open(os.path.join(ROOT, "assets/blueprints/power_chain_v1.json")))
    first = layout.layout(blueprint, {})
    assert first["feasible"], first["errors"]
    assert json.dumps(first, sort_keys=True) == json.dumps(layout.layout(blueprint, {}), sort_keys=True)
    shafts = [r for r in first["routes"] if r["medium"] == "shaft"]
    assert len(shafts) == 1 and shafts[0]["length_m"] == 0.0
    assert len(first["routes"]) == len(blueprint["connections"])

    varied = layout.layout(blueprint, {"spacing_m": 2.5})
    assert varied["feasible"] and varied["modules"] != first["modules"]

    # Port types that do not match.
    broken = copy.deepcopy(blueprint)
    broken["connections"].append({"from": "feed_tank.fluid_output", "to": "cooler.power"})
    result = layout.layout(broken, {})
    assert not result["feasible"] and any("cannot connect" in e for e in result["errors"]), result["errors"]

    # A module that nothing connects.
    broken = copy.deepcopy(blueprint)
    broken["modules"].append({"id": "spare_pump", "asset": "pump_centrifugal"})
    result = layout.layout(broken, {})
    assert not result["feasible"] and any("not connected" in e for e in result["errors"]), result["errors"]
    print("assetforge layout: feasible, deterministic, varied, and two impossible blueprints caught")


if __name__ == "__main__":
    main()
