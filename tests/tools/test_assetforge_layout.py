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
    # Routes keep clear of each other (bundles from one port excepted).
    assert layout.routes_clear(first["routes"]) == [], layout.routes_clear(first["routes"])
    for r in first["routes"]:
        assert r["medium"] == "shaft" or len(r["points"]) >= 3, r

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
    # The grid search where every fixed shape is blocked: a wall taller
    # than the routes' ceiling, with one gap at y = 2.
    wall = [((1.5, -5.0, 0.0), (2.5, 1.5, 5.0)), ((1.5, 2.5, 0.0), (2.5, 5.0, 5.0))]
    start, end = (0.0, 0.0, 0.5), (4.0, 0.0, 0.5)
    assert layout.find_route(start, (1, 0, 0), end, (-1, 0, 0), wall, 0.3, 3.0) is None
    found = layout.grid_route(start, (1, 0, 0), end, (-1, 0, 0), wall, 0.3, 3.0)
    assert found is not None, "the grid search finds the gap"
    path = found[1]
    assert not any(layout.segment_hits_box(p, q, box) for p, q in zip(path, path[1:]) for box in wall)
    assert any(1.5 <= p[1] <= 2.5 for p in path), path
    assert found == layout.grid_route(start, (1, 0, 0), end, (-1, 0, 0), wall, 0.3, 3.0)
    print("assetforge layout: feasible, clear routes, deterministic, varied, a grid route through a gap, and two impossible blueprints caught")


if __name__ == "__main__":
    main()
