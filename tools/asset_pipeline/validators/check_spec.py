#!/usr/bin/env python3
"""AssetForge: check asset specifications before anything is built (standard
library only, so it runs in CI without Blender).

    check_spec.py SPEC.json [SPEC.json ...]

A bad specification fails here with a clear message rather than as a
broken asset. Exits 1 if any specification has an error.
"""

import json
import math
import re
import sys

SCHEMAS = {"assetforge.machine/0.1": "machine", "assetforge.decorative/0.1": "decorative"}
GENERATORS = {"machine": {"machine_generator"}, "decorative": {"neural_cleanup"}}
# The typed ports' vocabulary: what PlanetSim's processes exchange (ADR-0031).
PORT_TYPES = {"mechanical_power", "electrical_power", "fluid", "heat", "item", "data"}
IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
PORT_NODE = re.compile(r"^PORT_[A-Z0-9_]+$")
MATERIAL = re.compile(r"^MAT_[A-Z0-9_]+$")
ANIMATION = re.compile(r"^ANIM_[A-Z0-9_]+$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
FAMILIES = {"generator", "tank", "pump", "reactor", "cooler"}


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def vector(value, length=3):
    return isinstance(value, list) and len(value) == length and all(number(v) for v in value)


def check(spec):
    errors = []

    def need(condition, message):
        if not condition:
            errors.append(message)
        return condition

    kind = SCHEMAS.get(spec.get("schema"))
    if not need(kind is not None, f"schema must be one of {sorted(SCHEMAS)}"):
        return errors
    need(spec.get("type") == kind, f"type must be '{kind}' for schema {spec['schema']}")
    need(isinstance(spec.get("id"), str) and IDENTIFIER.match(spec["id"]) is not None,
         "id must be lower_snake_case")
    need(spec.get("generator") in GENERATORS[kind],
         f"generator must be one of {sorted(GENERATORS[kind])}")
    materials = spec.get("materials")
    if need(isinstance(materials, dict) and materials, "materials must name at least one slot"):
        for slot, name in materials.items():
            need(isinstance(name, str) and MATERIAL.match(name) is not None,
                 f"material {slot}: '{name}' must be MAT_UPPER_SNAKE")
        need(len(set(materials.values())) == len(materials), "material names must be unique")
    budgets = spec.get("budgets", {})
    for key in ("max_triangles", "max_materials"):
        need(isinstance(budgets.get(key), int) and budgets[key] > 0, f"budgets.{key} must be a positive integer")
    if isinstance(materials, dict) and isinstance(budgets.get("max_materials"), int):
        need(len(materials) <= budgets["max_materials"], "more material slots than budgets.max_materials")

    if kind == "machine":
        dimensions = spec.get("dimensions_m")
        if need(vector(dimensions) and all(d > 0 for d in dimensions),
                "dimensions_m must be three positive lengths"):
            half_x, half_y, height = dimensions[0] / 2, dimensions[1] / 2, dimensions[2]
        ports = spec.get("ports", [])
        need(isinstance(ports, list) and ports, "a machine needs at least one port")
        for key in ("id", "node"):
            values = [p.get(key) for p in ports]
            need(len(set(values)) == len(values), f"port {key}s must be unique")
        for port in ports:
            name = port.get("id", "?")
            need(isinstance(port.get("id"), str) and IDENTIFIER.match(port["id"]) is not None,
                 f"port {name}: id must be lower_snake_case")
            need(port.get("type") in PORT_TYPES, f"port {name}: type must be one of {sorted(PORT_TYPES)}")
            need(isinstance(port.get("node"), str) and PORT_NODE.match(port["node"]) is not None,
                 f"port {name}: node must be PORT_UPPER_SNAKE")
            direction = port.get("direction")
            if need(vector(direction), f"port {name}: direction must be three numbers"):
                need(abs(math.sqrt(sum(c * c for c in direction)) - 1.0) <= 1e-6,
                     f"port {name}: direction must be a unit vector")
            position = port.get("position_m")
            if need(vector(position), f"port {name}: position_m must be three numbers") and \
                    vector(dimensions):
                tolerance = 1e-6
                inside = (abs(position[0]) <= half_x + tolerance and abs(position[1]) <= half_y + tolerance
                          and -tolerance <= position[2] <= height + tolerance)
                need(inside, f"port {name}: position_m lies outside the dimensions")
                on_face = (abs(abs(position[0]) - half_x) <= tolerance or
                           abs(abs(position[1]) - half_y) <= tolerance or
                           abs(position[2]) <= tolerance or abs(position[2] - height) <= tolerance)
                need(on_face, f"port {name}: position_m must lie on the bounding box's surface, "
                              "where a connection can reach it")
        need(spec.get("family", "generator") in FAMILIES,
             f"family must be one of {sorted(FAMILIES)}")
        for key, animation in spec.get("animations", {}).items():
            kind = animation.get("kind", "rotation")
            need(kind in ("rotation", "position"), f"animation {key}: kind must be rotation or position")
            if kind == "position":
                need(number(animation.get("travel_m")) and animation["travel_m"] > 0,
                     f"animation {key}: a position animation needs a positive travel_m")
            else:
                need(isinstance(animation.get("turns_per_cycle"), int) and animation["turns_per_cycle"] > 0,
                     f"animation {key}: turns_per_cycle must be a positive integer")
            binding = animation.get("binding", {"state": "rotor_speed_rpm", "kind": "speed_rpm"})
            need(isinstance(binding.get("state"), str) and
                 binding.get("kind") == ("position" if kind == "position" else "speed_rpm"),
                 f"animation {key}: binding must name a state and match the kind")
            need(isinstance(animation.get("action"), str) and ANIMATION.match(animation["action"]),
                 f"animation {key}: action must be ANIM_UPPER_SNAKE")
            need(vector(animation.get("pivot_m")), f"animation {key}: pivot_m must be three numbers")
            axis = animation.get("axis")
            need(vector(axis) and abs(math.sqrt(sum(c * c for c in axis)) - 1.0) <= 1e-6,
                 f"animation {key}: axis must be a unit vector")
            need(number(animation.get("cycle_s")) and animation["cycle_s"] > 0,
                 f"animation {key}: cycle_s must be positive")
    else:
        need(number(spec.get("height_m")) and spec["height_m"] > 0, "height_m must be positive")
        source = spec.get("source", {})
        need(isinstance(source.get("mesh"), str), "source.mesh must name the candidate mesh")
        need(isinstance(source.get("mesh_sha256"), str) and SHA256.match(source["mesh_sha256"]),
             "source.mesh_sha256 must be a SHA-256: a generated mesh is pinned by its hash")
        for key in ("concept_provenance", "image_to_3d_provenance"):
            need(isinstance(source.get(key), str), f"source.{key} must name the step's record")
        need(isinstance(budgets.get("max_texture_px"), int) and budgets["max_texture_px"] > 0,
             "budgets.max_texture_px must be a positive integer")
        collision = spec.get("collision")
        if collision is not None:
            need(collision.get("shape") == "convex" and collision.get("node_suffix") == "-convcolonly",
                 "collision: only a convex shape with Godot's -convcolonly suffix is supported")
            need(isinstance(collision.get("max_vertices"), int) and 4 <= collision["max_vertices"] <= 255,
                 "collision.max_vertices must be 4 to 255")
    return errors


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    failed = 0
    for path in paths:
        try:
            spec = json.load(open(path))
        except (OSError, json.JSONDecodeError) as error:
            print(f"FAIL {path}: {error}")
            failed += 1
            continue
        errors = check(spec)
        for error in errors:
            print(f"FAIL {path}: {error}")
        if not errors:
            print(f"PASS {path}")
        failed += bool(errors)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
