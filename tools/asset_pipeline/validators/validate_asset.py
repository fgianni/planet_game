#!/usr/bin/env python3
"""AssetForge Phase 1 validator (docs/assetforge_document §5, ADR-0031).

Checks a built asset against its specification, reading the GLB directly
(no Blender, no third-party packages):

    validate_asset.py SPEC.json DIR

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


def validate(spec_path, directory):
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
          [(p["id"], p["type"], p["node"]) for p in spec["ports"]])
    check("provenance geometry hash",
          provenance["geometry_sha256"] == hashlib.sha256(open(glb_path, "rb").read()).hexdigest())

    # Ports: present, typed, placed and pointing as specified.
    for port in spec["ports"]:
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

    # The meshes' world bounds against the specified dimensions.
    low = [math.inf] * 3
    high = [-math.inf] * 3
    triangles = 0
    for index, node in enumerate(nodes):
        if "mesh" not in node:
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
    wanted = [abs(c) for c in to_gltf(spec["dimensions_m"])]
    worst = max(abs(size[i] - wanted[i]) / wanted[i] for i in range(3))
    check("dimensions", worst <= DIMENSION_TOLERANCE,
          f"{[round(c, 3) for c in size]} against {wanted} (glTF axes)")
    check("origin at the base", abs(low[1]) <= POSITION_TOLERANCE_M, f"lowest y {low[1]:.4f} m")
    check("triangle budget", triangles <= spec["budgets"]["max_triangles"], f"{triangles} triangles")

    # Materials.
    materials = [m.get("name") for m in document.get("materials", [])]
    check("material slots", all(name in materials for name in spec["materials"].values()),
          f"{materials}")
    check("material budget", len(materials) <= spec["budgets"]["max_materials"])

    # Animations: the action exists, turns its node about the axis through
    # the pivot, and returns to its start after the cycle.
    for key, animation in spec["animations"].items():
        found = [a for a in document.get("animations", []) if a.get("name") == animation["action"]]
        if not found:
            check(f"animation {animation['action']} exists", False)
            continue
        target = by_name.get(animation["node"])
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
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(0 if validate(sys.argv[1], sys.argv[2]) else 1)
