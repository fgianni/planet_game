"""AssetForge Phase 1 (docs/assetforge_document, ADR-0031): a procedural
machine built in Blender from its specification.

Run inside Blender, normally through tools/asset_pipeline/cli/build_asset.py:

    blender -b --factory-startup --python machine_generator.py -- \
        --spec SPEC.json --out DIR [--commit HASH]

It writes DIR/<id>.glb, DIR/<id>.manifest.json and DIR/<id>.provenance.json.
Nothing in it is random and nothing records the time, so the same
specification, script and Blender version give the same bytes.

Axes are Blender's (+Z up, metres); the glTF exporter converts to +Y up.
A port is an empty whose local +Z is the port's outward direction.
"""

import argparse
import hashlib
import json
import math
import os
import sys

import bpy
from mathutils import Vector


def arguments():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--commit", default="unknown")
    return parser.parse_args(argv)


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def material(name, colour, metallic, roughness, emission=None):
    result = bpy.data.materials.new(name)
    result.use_nodes = True
    shader = result.node_tree.nodes["Principled BSDF"]
    shader.inputs["Base Color"].default_value = (*colour, 1.0)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    if emission is not None:
        # The heat tint's slot: black emission at rest; the game drives it.
        shader.inputs["Emission Color"].default_value = (*emission, 1.0)
        shader.inputs["Emission Strength"].default_value = 0.0
    return result


def box(name, centre, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=centre)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    return obj


def cylinder_x(name, centre, radius, length, mat, vertices=32):
    """A cylinder along X."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=length,
                                        location=centre, rotation=(0.0, math.pi / 2, 0.0))
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    obj.data.materials.append(mat)
    return obj


def cylinder_y(name, centre, radius, length, mat, vertices=24):
    """A cylinder along Y."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=length,
                                        location=centre, rotation=(math.pi / 2, 0.0, 0.0))
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    obj.data.materials.append(mat)
    return obj


def join(name, parts):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.active_object
    obj.name = name
    obj.data.name = name
    return obj


def action_fcurves(obj):
    """The F-curves of an object's action: through its slot's channel bag
    (Blender 4.4 and later), or the action's own list (earlier)."""
    animation = obj.animation_data
    action = animation.action
    if hasattr(action, "layers") and action.layers:
        from bpy_extras import anim_utils
        bag = anim_utils.action_get_channelbag_for_slot(action, animation.action_slot)
        return list(bag.fcurves) if bag is not None else []
    return list(action.fcurves)


def port(spec_port):
    empty = bpy.data.objects.new(spec_port["node"], None)
    empty.empty_display_type = "SINGLE_ARROW"
    empty.empty_display_size = 0.25
    empty.location = spec_port["position_m"]
    empty.rotation_mode = "QUATERNION"
    empty.rotation_quaternion = Vector((0.0, 0.0, 1.0)).rotation_difference(
        Vector(spec_port["direction"]).normalized())
    # Exported as glTF extras (export_extras).
    empty["port_id"] = spec_port["id"]
    empty["port_type"] = spec_port["type"]
    bpy.context.scene.collection.objects.link(empty)
    return empty


def build_generator(spec):
    """The electric generator: plinth, stator with fins, rotor, terminal box,
    coolant inlet and three ports."""
    parameters = spec["parameters"]
    length, width, height = spec["dimensions_m"]
    axis_z = spec["ports"][0]["position_m"][2]   # the shaft's height

    mats = spec["materials"]
    housing_mat = material(mats["housing"], (0.32, 0.36, 0.40), 0.6, 0.45)
    rotor_mat = material(mats["rotor"], (0.75, 0.74, 0.70), 0.9, 0.25)
    heat_mat = material(mats["heat"], (0.28, 0.30, 0.32), 0.7, 0.4, emission=(1.0, 0.35, 0.05))

    plinth = box("plinth", (0.0, 0.0, 0.1), (length, width, 0.2), housing_mat)
    stator_length = 1.7
    stator_radius = 0.65
    stator = cylinder_x("stator_shell", (0.0, 0.0, axis_z), stator_radius, stator_length,
                        housing_mat, vertices=48)
    supports = [box(f"support_{side}", (side * 0.6, 0.0, (0.2 + axis_z) / 2),
                    (0.25, 1.1, axis_z - 0.2), housing_mat) for side in (-1, 1)]
    terminal_x = spec["ports"][1]["position_m"][0]
    terminal_top = spec["ports"][1]["position_m"][2]
    terminal_bottom = axis_z + stator_radius - 0.05   # sunk into the stator
    terminal = box("terminal_box", (terminal_x, 0.0, (terminal_bottom + terminal_top) / 2),
                   (0.4, 0.4, terminal_top - terminal_bottom), housing_mat)
    inlet = spec["ports"][2]["position_m"]
    coolant = cylinder_y("coolant_inlet", (inlet[0], inlet[1] / 2, inlet[2]), 0.07,
                         abs(inlet[1]), housing_mat)
    housing = join("housing", [plinth, stator, *supports, terminal, coolant])

    # Cooling fins on the stator, the heat tint's surface.
    fins = []
    count = parameters["fin_count"]
    for index in range(count):
        x = -stator_length / 2 + 0.1 + index * (stator_length - 0.2) / (count - 1)
        fins.append(cylinder_x(f"fin_{index}", (x, 0.0, axis_z), stator_radius + 0.07, 0.03,
                               heat_mat, vertices=48))
    cooling = join("cooling_fins", fins)

    # The rotor: hub, shaft out to the mechanical input, and a hexagonal
    # coupling so that its turning shows. Its origin is on the axis.
    shaft_end = spec["ports"][0]["position_m"][0]
    hub = cylinder_x("rotor_hub", (0.0, 0.0, axis_z), parameters["rotor_radius_m"],
                     parameters["rotor_length_m"], rotor_mat)
    shaft_length = stator_length / 2 - shaft_end
    shaft = cylinder_x("rotor_shaft", (shaft_end + shaft_length / 2, 0.0, axis_z), 0.08,
                       shaft_length, rotor_mat, vertices=16)
    coupling = cylinder_x("rotor_coupling", (shaft_end + 0.06, 0.0, axis_z), 0.2, 0.08,
                          rotor_mat, vertices=6)
    rotor = join(spec["animations"]["rotor_speed"]["node"], [hub, shaft, coupling])
    bpy.context.scene.cursor.location = (0.0, 0.0, axis_z)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    # Keep the rotor where it is: without the inverse, parenting would move
    # it by the housing's origin (the validator's pivot check).
    rotor.parent = housing
    rotor.matrix_parent_inverse = housing.matrix_world.inverted()

    for spec_port in spec["ports"]:
        port(spec_port)

    # The rotor's cycle: one turn per cycle, linear, about the spec's axis.
    animation = spec["animations"]["rotor_speed"]
    scene = bpy.context.scene
    scene.render.fps = 24
    frames = round(animation["cycle_s"] * scene.render.fps)
    scene.frame_start = 0
    scene.frame_end = frames
    rotor.rotation_mode = "XYZ"
    axis = [abs(round(v)) for v in animation["axis"]].index(1)
    for frame, turn in ((0, 0.0), (frames, animation["turns_per_cycle"])):
        rotation = [0.0, 0.0, 0.0]
        rotation[axis] = 2.0 * math.pi * turn
        rotor.rotation_euler = rotation
        rotor.keyframe_insert(data_path="rotation_euler", index=axis, frame=frame)
    action = rotor.animation_data.action
    action.name = animation["action"]
    # A constant speed: linear keys (Blender's default is Bézier, which
    # eases in and out; the preference does not apply in background mode).
    for curve in action_fcurves(rotor):
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
    rotor.rotation_euler = (0.0, 0.0, 0.0)
    scene.frame_set(0)
    return [housing, cooling, rotor]


GENERATORS = {"machine_generator": build_generator}


def main():
    args = arguments()
    with open(args.spec) as stream:
        spec = json.load(stream)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    GENERATORS[spec["generator"]](spec)

    os.makedirs(args.out, exist_ok=True)
    glb = os.path.join(args.out, spec["id"] + ".glb")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", export_extras=True,
                              export_yup=True, export_apply=True, export_animations=True)

    manifest = {
        "id": spec["id"],
        "type": spec["type"],
        "geometry": os.path.basename(glb),
        "dimensions_m": spec["dimensions_m"],
        "components": spec["components"],
        "ports": [{"id": p["id"], "type": p["type"], "node": p["node"]} for p in spec["ports"]],
        "animations": {key: value["action"] for key, value in spec["animations"].items()},
        "materials": spec["materials"],
    }
    with open(os.path.join(args.out, spec["id"] + ".manifest.json"), "w") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")

    provenance = {
        "asset": spec["id"],
        "specification": os.path.relpath(args.spec),
        "specification_sha256": sha256(args.spec),
        "generator": os.path.relpath(__file__),
        "generator_sha256": sha256(__file__),
        "toolchain": {"blender": bpy.app.version_string},
        "commit": args.commit,
        "seed": spec["parameters"].get("seed"),
        "geometry_sha256": sha256(glb),
        "licence": "procedural; generated from repository sources",
    }
    with open(os.path.join(args.out, spec["id"] + ".provenance.json"), "w") as stream:
        json.dump(provenance, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("assetforge: wrote", glb)


if __name__ == "__main__":
    main()
