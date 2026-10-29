#!/usr/bin/env python3
"""AssetForge Phase 1 validator (docs/assetforge_document §5, ADR-0031).

Checks a built asset against its specification, reading the GLB directly
(no Blender, no third-party packages):

    validate_asset.py SPEC.json DIR [--require-approval]

DIR holds <id>.glb, <id>.manifest.json and <id>.provenance.json. Prints one
line per check and exits 1 if any fails.

Axes: the specification's are Blender's (+Z up); glTF's are +Y up, with
Blender (x, y, z) at glTF (x, z, -y). A port's outward direction is the
empty's local +Z in Blender, which is its local +Y in glTF.
"""

import hashlib
import json
import math
import os
import struct
import sys

POSITION_TOLERANCE_M = 1e-3
DIRECTION_TOLERANCE = 1e-3
DIMENSION_TOLERANCE = 0.02   # relative


def to_gltf(v):
    return (v[0], v[2], -v[1])


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or length != len(data):
        raise ValueError("not a glTF 2.0 binary")
    json_length, json_type = struct.unpack_from("<I4s", data, 12)
    if json_type != b"JSON":
        raise ValueError("first chunk is not JSON")
    document = json.loads(data[20:20 + json_length])
    offset = 20 + json_length
    binary = b""
    if offset < len(data):
        bin_length, bin_type = struct.unpack_from("<I4s", data, offset)
        if bin_type == b"BIN\x00":
            binary = data[offset + 8:offset + 8 + bin_length]
    return document, binary


COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def accessor_floats(document, binary, index):
    accessor = document["accessors"][index]
    if accessor["componentType"] != 5126:   # FLOAT
        raise ValueError("expected a float accessor")
    view = document["bufferViews"][accessor["bufferView"]]
    width = COMPONENTS[accessor["type"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 4 * width)
    return [struct.unpack_from(f"<{width}f", binary, start + i * stride)
            for i in range(accessor["count"])]


def image_size(document, binary, image):
    """(width, height) of an embedded PNG or JPEG, from its header."""
    view = document["bufferViews"][image["bufferView"]]
    data = binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", data[16:24])
    if data[:2] == b"\xff\xd8":
        at = 2
        while at < len(data):
            marker, length = data[at + 1], struct.unpack(">H", data[at + 2:at + 4])[0]
            if marker in (0xC0, 0xC1, 0xC2):   # start of frame: height, then width
                height, width = struct.unpack(">HH", data[at + 5:at + 9])
                return width, height
            at += 2 + length
    raise ValueError("an embedded image is neither PNG nor JPEG")


# Quaternions are (x, y, z, w), as in glTF.
def q_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_rotate(q, v):
    x, y, z, w = q
    p = q_multiply(q_multiply(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]


def world_transforms(document):
    """Each node's world (translation, rotation, scale), for TRS nodes."""
    nodes = document["nodes"]
    parent = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parent[child] = index
    cache = {}

    def world(index):
        if index in cache:
            return cache[index]
        node = nodes[index]
        if "matrix" in node:
            raise ValueError(f"node {node.get('name')} uses a matrix; expected TRS")
        t = node.get("translation", [0.0, 0.0, 0.0])
        r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        s = node.get("scale", [1.0, 1.0, 1.0])
        if index in parent:
            pt, pr, ps = world(parent[index])
            scaled = [t[i] * ps[i] for i in range(3)]
            moved = q_rotate(pr, scaled)
            result = ([pt[i] + moved[i] for i in range(3)], q_multiply(pr, r),
                      [ps[i] * s[i] for i in range(3)])
        else:
            result = (list(t), list(r), list(s))
        cache[index] = result
        return result

    return {index: world(index) for index in range(len(nodes))}


def distance(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def normalised(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def check_movement(check, document, binary, animation_json, target, animation, world):
    """A "position" animation (a level gauge): its node moves travel_m along
    the axis over the cycle, linearly and off the axis not at all, starting
    at the pivot. Its place is set from the state, not played."""
    name = animation["action"]
    channels = [c for c in animation_json["channels"]
                if c["target"].get("node") == target and c["target"]["path"] == "translation"]
    check(f"animation {name} moves {animation['node']}", len(channels) == 1)
    if not channels:
        return
    sampler = animation_json["samplers"][channels[0]["sampler"]]
    times = [t[0] for t in accessor_floats(document, binary, sampler["input"])]
    places = accessor_floats(document, binary, sampler["output"])
    check(f"animation {name} cycle", abs(times[-1] - times[0] - animation["cycle_s"]) <= 1e-4,
          f"{times[-1] - times[0]:.4f} s")
    axis = normalised(to_gltf(animation["axis"]))
    worst_off = 0.0
    worst_linear = 0.0
    for time, place in zip(times, places):
        moved = [place[i] - places[0][i] for i in range(3)]
        along = sum(moved[i] * axis[i] for i in range(3))
        worst_off = max(worst_off, distance(moved, [along * axis[i] for i in range(3)]))
        wanted = animation["travel_m"] * (time - times[0]) / animation["cycle_s"]
        worst_linear = max(worst_linear, abs(along - wanted))
    check(f"animation {name} axis", worst_off <= 1e-4, f"off axis {worst_off:.2e} m")
    check(f"animation {name} linear travel", worst_linear <= 1e-4,
          f"largest error {worst_linear:.2e} m over {len(times)} samples, {animation['travel_m']} m")
    error = distance(world[target][0], to_gltf(animation["pivot_m"]))
    check(f"animation {name} starts at the pivot", error <= POSITION_TOLERANCE_M,
          f"{error * 1000:.2f} mm from the specification")


def validate(spec_path, directory, require_approval=False):
    spec = json.load(open(spec_path))
    asset = spec["id"]
    glb_path = os.path.join(directory, asset + ".glb")
    manifest = json.load(open(os.path.join(directory, asset + ".manifest.json")))
    provenance = json.load(open(os.path.join(directory, asset + ".provenance.json")))
    document, binary = read_glb(glb_path)
    nodes = document["nodes"]
    by_name = {}
    results = []

    def check(name, ok, detail=""):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}")

    names = [node.get("name") for node in nodes]
    check("every node is named", all(names))
    check("node names are unique", len(set(names)) == len(names))
    by_name = {node.get("name"): index for index, node in enumerate(nodes)}
    world = world_transforms(document)

    # The manifest matches the specification it came from.
    check("manifest id", manifest["id"] == asset)
    check("manifest ports", [(p["id"], p["type"], p["node"]) for p in manifest["ports"]] ==
          [(p["id"], p["type"], p["node"]) for p in spec.get("ports", [])])
    check("provenance geometry hash",
          provenance["geometry_sha256"] == hashlib.sha256(open(glb_path, "rb").read()).hexdigest())

    # Ports: present, typed, placed and pointing as specified.
    for port in spec.get("ports", []):
        index = by_name.get(port["node"])
        if index is None:
            check(f"port {port['node']} exists", False)
            continue
        extras = nodes[index].get("extras", {})
        check(f"port {port['node']} typed",
              extras.get("port_id") == port["id"] and extras.get("port_type") == port["type"],
              f"extras {extras}")
        position, rotation, _ = world[index]
        error = distance(position, to_gltf(port["position_m"]))
        check(f"port {port['node']} position", error <= POSITION_TOLERANCE_M,
              f"{error * 1000:.2f} mm from the specification")
        outward = q_rotate(rotation, (0.0, 1.0, 0.0))
        error = distance(outward, normalised(to_gltf(port["direction"])))
        check(f"port {port['node']} direction", error <= DIRECTION_TOLERANCE, f"error {error:.2e}")

    # The meshes' world bounds against the specified dimensions (the
    # collision shape is not drawn, so it is left out).
    collision_suffix = spec.get("collision", {}).get("node_suffix")
    low = [math.inf] * 3
    high = [-math.inf] * 3
    triangles = 0
    for index, node in enumerate(nodes):
        if "mesh" not in node:
            continue
        if collision_suffix and node.get("name", "").endswith(collision_suffix):
            continue
        translation, rotation, scale = world[index]
        for primitive in document["meshes"][node["mesh"]]["primitives"]:
            accessor = document["accessors"][primitive["attributes"]["POSITION"]]
            lo, hi = accessor["min"], accessor["max"]
            for corner in range(8):
                local = [hi[i] if corner >> i & 1 else lo[i] for i in range(3)]
                moved = q_rotate(rotation, [local[i] * scale[i] for i in range(3)])
                point = [translation[i] + moved[i] for i in range(3)]
                low = [min(low[i], point[i]) for i in range(3)]
                high = [max(high[i], point[i]) for i in range(3)]
            if primitive.get("mode", 4) != 4:
                check(f"mesh {node.get('name')} is triangles", False)
            count = document["accessors"][primitive["indices"]]["count"] if "indices" in primitive \
                else accessor["count"]
            triangles += count // 3
    size = [high[i] - low[i] for i in range(3)]
    if "dimensions_m" in spec:
        wanted = [abs(c) for c in to_gltf(spec["dimensions_m"])]
        worst = max(abs(size[i] - wanted[i]) / wanted[i] for i in range(3))
        check("dimensions", worst <= DIMENSION_TOLERANCE,
              f"{[round(c, 3) for c in size]} against {wanted} (glTF axes)")
    else:
        # A decorative asset: its height, and its footprint centred on the origin.
        error = abs(size[1] - spec["height_m"]) / spec["height_m"]
        check("height", error <= DIMENSION_TOLERANCE, f"{size[1]:.3f} m against {spec['height_m']} m")
        offset = math.hypot((low[0] + high[0]) / 2, (low[2] + high[2]) / 2)
        check("footprint centred", offset <= 0.01, f"{offset * 1000:.1f} mm off the origin")
    check("origin at the base", abs(low[1]) <= POSITION_TOLERANCE_M, f"lowest y {low[1]:.4f} m")
    check("triangle budget", triangles <= spec["budgets"]["max_triangles"], f"{triangles} triangles")

    # The collision shape: present, within its budget of distinct points,
    # and enclosing the drawn mesh (to 1 % of its size).
    if collision_suffix:
        index = by_name.get(asset + collision_suffix)
        check(f"collision {asset + collision_suffix} exists", index is not None)
        if index is not None:
            translation, rotation, scale = world[index]
            points = set()
            for primitive in document["meshes"][nodes[index]["mesh"]]["primitives"]:
                check("collision has no material", "material" not in primitive)
                for local in accessor_floats(document, binary, primitive["attributes"]["POSITION"]):
                    moved = q_rotate(rotation, [local[i] * scale[i] for i in range(3)])
                    points.add(tuple(round(translation[i] + moved[i], 6) for i in range(3)))
            check("collision point budget", len(points) <= spec["collision"]["max_vertices"],
                  f"{len(points)} distinct points")
            hull_low = [min(p[i] for p in points) for i in range(3)]
            hull_high = [max(p[i] for p in points) for i in range(3)]
            slack = 0.01 * max(size)
            inside = all(hull_low[i] <= low[i] + slack and hull_high[i] >= high[i] - slack
                         for i in range(3))
            check("collision encloses the mesh", inside,
                  f"hull {[round(c, 3) for c in hull_low]}..{[round(c, 3) for c in hull_high]}")

    # Materials.
    materials = [m.get("name") for m in document.get("materials", [])]
    check("material slots", all(name in materials for name in spec["materials"].values()),
          f"{materials}")
    check("material budget", len(materials) <= spec["budgets"]["max_materials"])
    if "max_texture_px" in spec["budgets"]:
        sizes = [image_size(document, binary, image) for image in document.get("images", [])]
        check("texture budget", all(max(size) <= spec["budgets"]["max_texture_px"] for size in sizes),
              f"{sizes}")
    if spec["type"] == "decorative":
        # A neural candidate carries its licences and its chain of steps.
        check("provenance licences", bool(provenance.get("licences")) and
              all(provenance["licences"]), f"{provenance.get('licences')}")
        check("provenance chain", [step.get("step") for step in provenance.get("chain", [])] ==
              ["concept image", "image to 3D", "clean-up"])
        if require_approval:
            # A person approved exactly these bytes (approve_asset.py).
            root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
            path = os.path.join(root, "assets/approvals", asset + ".json")
            approval = json.load(open(path)) if os.path.exists(path) else {}
            check("human approval", approval.get("decision") == "approved" and
                  approval.get("geometry_sha256") == provenance["geometry_sha256"],
                  f"{approval.get('decision', 'none')} by {approval.get('reviewer', 'nobody')}"
                  f"{'' if approval.get('geometry_sha256') in (None, provenance['geometry_sha256']) else ', for other bytes'}")

    # Animations: the action exists, turns its node about the axis through
    # the pivot, and returns to its start after the cycle.
    for key, animation in spec.get("animations", {}).items():
        found = [a for a in document.get("animations", []) if a.get("name") == animation["action"]]
        if not found:
            check(f"animation {animation['action']} exists", False)
            continue
        target = by_name.get(animation["node"])
        if animation.get("kind", "rotation") == "position":
            check_movement(check, document, binary, found[0], target, animation, world)
            continue
        channels = [c for c in found[0]["channels"]
                    if c["target"].get("node") == target and c["target"]["path"] == "rotation"]
        check(f"animation {animation['action']} rotates {animation['node']}", len(channels) == 1)
        if not channels:
            continue
        sampler = found[0]["samplers"][channels[0]["sampler"]]
        times = [t[0] for t in accessor_floats(document, binary, sampler["input"])]
        rotations = accessor_floats(document, binary, sampler["output"])
        check(f"animation {animation['action']} cycle",
              abs(times[-1] - times[0] - animation["cycle_s"]) <= 1e-4,
              f"{times[-1] - times[0]:.4f} s")
        axis = normalised(to_gltf(animation["axis"]))
        off_axis = 0.0
        for q in rotations:
            vector = q[:3]
            along = sum(vector[i] * axis[i] for i in range(3))
            off_axis = max(off_axis, distance(vector, [along * axis[i] for i in range(3)]))
        check(f"animation {animation['action']} axis", off_axis <= 1e-4, f"off axis {off_axis:.2e}")
        # A constant angular speed: each sample's angle about the axis,
        # unwrapped, against turns_per_cycle × 2π × t / cycle.
        rate = 2.0 * math.pi * animation["turns_per_cycle"] / animation["cycle_s"]
        previous = 0.0
        worst = 0.0
        for time, q in zip(times, rotations):
            along = sum(q[i] * axis[i] for i in range(3))
            angle = 2.0 * math.atan2(along, q[3])
            while angle < previous - math.pi:
                angle += 2.0 * math.pi
            previous = angle
            worst = max(worst, abs(angle - rate * (time - times[0])))
        check(f"animation {animation['action']} constant speed", worst <= 1e-3,
              f"largest error {worst:.2e} rad over {len(times)} samples")
        start, end = rotations[0], rotations[-1]
        same = min(distance(start[:3], end[:3]) + abs(start[3] - end[3]),
                   distance(start[:3], [-c for c in end[:3]]) + abs(start[3] + end[3]))
        check(f"animation {animation['action']} loops", same <= 1e-4)
        error = distance(world[target][0], to_gltf(animation["pivot_m"]))
        check(f"animation {animation['action']} pivot", error <= POSITION_TOLERANCE_M,
              f"{error * 1000:.2f} mm from the specification")

    failed = results.count(False)
    print(f"assetforge validate {asset}: {len(results) - failed} passed, {failed} failed")
    return failed == 0


if __name__ == "__main__":
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(paths) != 2 or set(flags) - {"--require-approval"}:
        print(__doc__)
        sys.exit(2)
    sys.exit(0 if validate(paths[0], paths[1], "--require-approval" in flags) else 1)
