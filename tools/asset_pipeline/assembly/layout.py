#!/usr/bin/env python3
"""AssetForge Phase 3 (docs/assetforge_document §4, §9; ADR-0031): a
blueprint's deterministic physical layout. Places its modules and routes
its pipes, shaft and cables, or reports that it cannot.

    layout.py BLUEPRINT.json OUT.json [--set NAME=VALUE ...]

Reads each module's specification (assets/specifications/<asset>.json) for
its dimensions and ports. Axes are Blender's (+Z up, metres); a module
stands on z = 0 and turns about +Z by a multiple of 90°. Writes the layout
(module transforms, routes, checks) and exits 1 if it is impossible.
Standard library only; the same inputs give the same bytes.

The rules:
- the root module stands at the origin, unturned;
- a mechanical_power connection is a rigid mate: the second module is
  placed so its port coincides with the first's, facing it;
- the others are flexible (pipes, cables): each new module, beside one
  already placed, tries the four turns, is pushed outward until it clears
  the placed ones by the clearance, and the cheapest wins: the length of
  its connections to placed modules, plus a penalty for its ports that
  face the plant rather than away from it;
- a route leaves each port along its direction by a stub, then runs along
  the axes, or up over the modules and down; the shortest one that crosses
  no module is taken.
"""

import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
YAWS = (0, 90, 180, 270)
MEDIA = {"fluid": ("pipe", 0.07), "electrical_power": ("cable", 0.025),
         "heat": ("pipe", 0.07), "item": ("conveyor", 0.15), "data": ("cable", 0.015)}


def rotate(v, yaw):
    c, s = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
    return (c * v[0] - s * v[1], s * v[0] + c * v[1], v[2])


def add(a, b):
    return tuple(a[i] + b[i] for i in range(3))


def sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def scale(a, k):
    return tuple(c * k for c in a)


def clean(v):
    return tuple(round(c, 6) + 0.0 for c in v)


class Module:
    def __init__(self, entry):
        self.id = entry["id"]
        self.specification = entry["asset"]   # assets/specifications/<this>.json
        self.spec = json.load(open(os.path.join(ROOT, "assets/specifications", entry["asset"] + ".json")))
        self.asset = self.spec["id"]
        self.ports = {p["id"]: p for p in self.spec["ports"]}
        self.yaw = None
        self.position = None

    def port_world(self, name, yaw=None, position=None):
        yaw = self.yaw if yaw is None else yaw
        position = self.position if position is None else position
        port = self.ports[name]
        return add(position, rotate(port["position_m"], yaw)), rotate(port["direction"], yaw)

    def box(self, yaw=None, position=None, margin=0.0):
        """The module's axis-aligned box (low, high), turned and placed."""
        yaw = self.yaw if yaw is None else yaw
        position = self.position if position is None else position
        x, y, z = self.spec["dimensions_m"]
        half = rotate((x / 2, y / 2, 0.0), yaw)
        hx, hy = abs(half[0]), abs(half[1])
        return ((position[0] - hx - margin, position[1] - hy - margin, -margin),
                (position[0] + hx + margin, position[1] + hy + margin, z + margin))


def boxes_overlap(a, b):
    return all(a[0][i] < b[1][i] - 1e-9 and b[0][i] < a[1][i] - 1e-9 for i in range(3))


def segment_hits_box(p, q, box):
    """Whether the axis-aligned segment p→q passes through the box's interior."""
    low, high = box
    for i in range(3):
        a, b = min(p[i], q[i]), max(p[i], q[i])
        if b <= low[i] + 1e-9 or a >= high[i] - 1e-9:
            return False
    return True


def resolve(blueprint, modules):
    connections = []
    for c in blueprint["connections"]:
        a_module, a_port = c["from"].split(".")
        b_module, b_port = c["to"].split(".")
        connections.append((modules[a_module], a_port, modules[b_module], b_port))
    return connections


def place(blueprint, modules, connections, parameters):
    errors = []
    root = modules[blueprint["root"]]
    root.yaw, root.position = 0, (0.0, 0.0, 0.0)
    placed = [root]
    spacing, clearance = parameters["spacing_m"], parameters["clearance_m"]

    def clear_of_placed(module, yaw, position, ignore=()):
        box = module.box(yaw, position, clearance / 2)
        return all(not boxes_overlap(box, other.box(margin=clearance / 2))
                   for other in placed if other not in ignore)

    def plant_centre():
        boxes = [m.box() for m in placed]
        return tuple((min(b[0][i] for b in boxes) + max(b[1][i] for b in boxes)) / 2 for i in range(3))

    progress = True
    while progress and len(placed) < len(modules):
        progress = False
        for a, a_port, b, b_port in connections:
            if (a in placed) == (b in placed):
                continue
            anchor, anchor_port, new, new_port = (a, a_port, b, b_port) if a in placed else (b, b_port, a, a_port)
            point, direction = anchor.port_world(anchor_port)
            if new.ports[new_port]["type"] == "mechanical_power":
                # A rigid mate: the new port on the anchor's, facing it.
                for yaw in YAWS:
                    if clean(rotate(new.ports[new_port]["direction"], yaw)) == clean(scale(direction, -1)):
                        new.yaw = yaw
                        new.position = clean(sub(point, rotate(new.ports[new_port]["position_m"], yaw)))
                        new.position = (new.position[0], new.position[1], 0.0)
                        break
                else:
                    errors.append(f"{new.id}.{new_port} cannot face {anchor.id}.{anchor_port}")
                    return errors
                if not clear_of_placed(new, new.yaw, new.position, ignore=(anchor,)):
                    errors.append(f"{new.id}, mated to {anchor.id}, overlaps another module")
                    return errors
            else:
                outward = (direction[0], direction[1], 0.0)
                if abs(outward[0]) + abs(outward[1]) < 1e-9:
                    outward = (1.0, 0.0, 0.0)
                centre = plant_centre()
                best = None
                for yaw in YAWS:
                    q = rotate(new.ports[new_port]["position_m"], yaw)
                    target = add(point, scale(outward, spacing))
                    position = (target[0] - q[0], target[1] - q[1], 0.0)
                    steps = 0
                    while not clear_of_placed(new, yaw, position) and steps < 200:
                        position = add(position, scale(outward, 0.25))
                        steps += 1
                    position = clean(position)
                    if steps >= 200:
                        continue
                    # The cost: its connections to placed modules, and its free
                    # ports facing the plant.
                    cost = 0.0
                    for c_a, c_ap, c_b, c_bp in connections:
                        if new in (c_a, c_b):
                            other, other_port, mine = (c_b, c_bp, c_ap) if c_a is new else (c_a, c_ap, c_bp)
                            mine_point, mine_dir = new.port_world(mine, yaw, position)
                            if other in placed:
                                other_point = other.port_world(other_port)[0]
                                cost += sum(abs(mine_point[i] - other_point[i]) for i in range(3))
                            else:
                                towards = sub(centre, mine_point)
                                cost += 2.0 * max(0.0, sum(mine_dir[i] * towards[i] for i in range(2)))
                    if best is None or cost < best[0] - 1e-9:
                        best = (cost, yaw, position)
                if best is None:
                    errors.append(f"no clear place for {new.id} beside {anchor.id}")
                    return errors
                new.yaw, new.position = best[1], best[2]
            placed.append(new)
            progress = True
    for module in modules.values():
        if module not in placed:
            errors.append(f"{module.id} is not connected to the plant")
    return errors


def route(modules, connections, parameters):
    errors = []
    routes = []
    stub = parameters["stub_m"]
    obstacles = {m.id: m.box(margin=0.02) for m in modules.values()}
    top = max(b[1][2] for b in obstacles.values()) + 0.3
    for a, a_port, b, b_port in connections:
        kind_a, kind_b = a.ports[a_port]["type"], b.ports[b_port]["type"]
        name = f"{a.id}.{a_port} -> {b.id}.{b_port}"
        if kind_a != kind_b:
            errors.append(f"{name}: {kind_a} cannot connect to {kind_b}")
            continue
        start, start_dir = a.port_world(a_port)
        end, end_dir = b.port_world(b_port)
        if kind_a == "mechanical_power":
            gap = math.dist(start, end)
            coaxial = clean(start_dir) == clean(scale(end_dir, -1))
            if gap > 1e-6 or not coaxial:
                errors.append(f"{name}: a shaft must mate (gap {gap:.4f} m, coaxial {coaxial})")
            routes.append({"connection": name, "medium": "shaft", "radius_m": 0.0,
                           "points": [list(clean(start))], "length_m": 0.0})
            continue
        medium, radius = MEDIA[kind_a]
        s = add(start, scale(start_dir, stub))
        e = add(end, scale(end_dir, stub))
        candidates = []
        orders = [(0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0)]
        for order in orders:
            points = [s]
            current = list(s)
            for axis in order:
                if abs(current[axis] - e[axis]) > 1e-9:
                    current[axis] = e[axis]
                    points.append(tuple(current))
            candidates.append(points)
        for order in ((0, 1), (1, 0)):
            points = [s, (s[0], s[1], top)]
            current = [s[0], s[1], top]
            for axis in order:
                current[axis] = e[axis]
                points.append(tuple(current))
            points.append(e)
            candidates.append(points)
        best = None
        for points in candidates:
            path = [start] + points + ([e] if points[-1] != e else []) + [end]
            path = [clean(p) for p in path]
            path = [p for i, p in enumerate(path) if i == 0 or p != path[i - 1]]
            # The stubs leave and enter their own modules; every other leg
            # must cross no module.
            legs = list(zip(path[1:-2], path[2:-1]))
            blocked = any(segment_hits_box(p, q, box) for p, q in legs for box in obstacles.values())
            if blocked:
                continue
            length = sum(math.dist(p, q) for p, q in zip(path, path[1:]))
            if best is None or length < best[0] - 1e-9:
                best = (length, path)
        if best is None:
            errors.append(f"{name}: no route clears the modules")
            continue
        routes.append({"connection": name, "medium": medium, "radius_m": radius,
                       "points": [list(p) for p in best[1]], "length_m": round(best[0], 6)})
    return routes, errors


def layout(blueprint, overrides):
    parameters = dict(blueprint["parameters"])
    parameters.update(overrides)
    modules = {entry["id"]: Module(entry) for entry in blueprint["modules"]}
    connections = resolve(blueprint, modules)
    errors = place(blueprint, modules, connections, parameters)
    routes = []
    if not errors:
        routes, errors = route(modules, connections, parameters)
    placed = [m for m in modules.values() if m.position is not None]
    overlaps = [f"{a.id} / {b.id}" for i, a in enumerate(placed) for b in placed[i + 1:]
                if boxes_overlap(a.box(), b.box())]
    errors += [f"modules overlap: {pair}" for pair in overlaps]
    return {
        "blueprint": blueprint["id"],
        "parameters": parameters,
        "modules": [{"id": m.id, "asset": m.asset, "specification": m.specification,
                     "yaw_deg": m.yaw, "position_m": list(m.position) if m.position else None}
                    for m in modules.values()],
        "routes": routes,
        "feasible": not errors,
        "errors": errors,
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    overrides = {}
    rest = argv[2:]
    while rest:
        if rest[0] != "--set" or len(rest) < 2 or "=" not in rest[1]:
            print(__doc__)
            return 2
        key, value = rest[1].split("=", 1)
        overrides[key] = float(value)
        rest = rest[2:]
    result = layout(json.load(open(argv[0])), overrides)
    with open(argv[1], "w") as stream:
        json.dump(result, stream, indent=2, sort_keys=True)
        stream.write("\n")
    for error in result["errors"]:
        print(f"FAIL {error}")
    print(f"assetforge layout {result['blueprint']}: "
          f"{'feasible' if result['feasible'] else 'impossible'}, "
          f"{len(result['modules'])} modules, {len(result['routes'])} routes")
    return 0 if result["feasible"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
