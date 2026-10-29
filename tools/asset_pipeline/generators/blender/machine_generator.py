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


def canonical_triangle_order(path):
    """Rewrite each primitive's index buffer in place, its triangles in a
    canonical order: each one rotated to start at its smallest index (which
    keeps its winding, so its facing) and the triangles sorted. The
    exporter's order is not reproducible for meshes made by edit-mode
    operations; the geometry is unchanged, and the bytes become so."""
    import struct
    with open(path, "rb") as stream:
        data = bytearray(stream.read())
    json_length = struct.unpack_from("<I", data, 12)[0]
    document = json.loads(data[20:20 + json_length])
    binary_start = 20 + json_length + 8
    sizes = {5121: "B", 5123: "H", 5125: "I"}
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            if "indices" not in primitive or primitive.get("mode", 4) != 4:
                continue
            accessor = document["accessors"][primitive["indices"]]
            view = document["bufferViews"][accessor["bufferView"]]
            fmt = sizes[accessor["componentType"]]
            start = binary_start + view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            count = accessor["count"]
            values = struct.unpack_from(f"<{count}{fmt}", data, start)
            triangles = []
            for i in range(0, count, 3):
                a, b, c = values[i:i + 3]
                smallest = min(a, b, c)
                triangles.append((a, b, c) if a == smallest else (b, c, a) if b == smallest else (c, a, b))
            triangles.sort()
            struct.pack_into(f"<{count}{fmt}", data, start, *[v for t in triangles for v in t])
    with open(path, "wb") as stream:
        stream.write(data)


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


def cylinder_z(name, centre, radius, length, mat, vertices=32, open_ends=False):
    """A cylinder along Z; with open_ends, a tube (a fan's shroud)."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=length,
                                        location=centre,
                                        end_fill_type="NOTHING" if open_ends else "NGON")
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def dome(name, centre, radius, height, mat):
    """The upper half of an ellipsoid: a vessel's head, its base at `centre`."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1.0, location=centre)
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.object.mode_set(mode="OBJECT")
    for vertex in obj.data.vertices:
        vertex.select = vertex.co.z < -1e-6
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.delete(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.scale = (radius, radius, height)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    return obj


def moving_part(obj, parent, pivot):
    """A part that moves: its origin at the pivot, parented without moving
    (the parent inverse; without it the part shifts by the parent's origin)."""
    bpy.context.scene.cursor.location = pivot
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    obj.parent = parent
    obj.matrix_parent_inverse = parent.matrix_world.inverted()
    return obj


def animate(obj, animation):
    """One linear cycle: a rotation of turns_per_cycle turns about the axis
    (kind "rotation", the default), or a movement of travel_m along it
    (kind "position", a level gauge whose state sets its place)."""
    scene = bpy.context.scene
    scene.render.fps = 24
    frames = round(animation["cycle_s"] * scene.render.fps)
    scene.frame_start = 0
    scene.frame_end = frames
    axis = [abs(round(v)) for v in animation["axis"]].index(1)
    if animation.get("kind", "rotation") == "rotation":
        obj.rotation_mode = "XYZ"
        for frame, turn in ((0, 0.0), (frames, animation["turns_per_cycle"])):
            rotation = [0.0, 0.0, 0.0]
            rotation[axis] = 2.0 * math.pi * turn * animation["axis"][axis]
            obj.rotation_euler = rotation
            obj.keyframe_insert(data_path="rotation_euler", index=axis, frame=frame)
        rest = ("rotation_euler", (0.0, 0.0, 0.0))
    else:
        start = obj.location.copy()
        for frame, along in ((0, 0.0), (frames, animation["travel_m"])):
            obj.location = start + Vector(animation["axis"]) * along
            obj.keyframe_insert(data_path="location", index=axis, frame=frame)
        rest = ("location", start)
    action = obj.animation_data.action
    action.name = animation["action"]
    # Linear keys (Blender's default is Bézier, which eases in and out; the
    # preference does not apply in background mode).
    for curve in action_fcurves(obj):
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
    setattr(obj, rest[0], rest[1])


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


def build_tank(spec):
    """A storage tank: base plate, shell with a domed head, an outlet pipe,
    and a sight glass whose float shows the fill level."""
    length, width, height = spec["dimensions_m"]
    mats = spec["materials"]
    shell_mat = material(mats["shell"], (0.55, 0.58, 0.55), 0.5, 0.5)
    glass_mat = material(mats["glass"], (0.75, 0.85, 0.90), 0.0, 0.1)
    float_mat = material(mats["float"], (0.95, 0.45, 0.05), 0.0, 0.5)
    radius, head = 0.8, 0.4
    shell_top = height - head
    base = box("base", (0.0, 0.0, 0.1), (length, width, 0.2), shell_mat)
    shell = cylinder_z("shell", (0.0, 0.0, (0.2 + shell_top) / 2), radius, shell_top - 0.2,
                       shell_mat, vertices=48)
    top = dome("head", (0.0, 0.0, shell_top), radius, head, shell_mat)
    outlet = spec["ports"][0]["position_m"]
    pipe = cylinder_x("outlet", ((radius - 0.05 + outlet[0]) / 2, 0.0, outlet[2]), 0.08,
                      outlet[0] - radius + 0.05, shell_mat, vertices=16)
    tank = join("tank", [base, shell, top, pipe])
    level = spec["animations"]["level"]
    glass_y = level["pivot_m"][1]
    glass = cylinder_z("sight_glass", (0.0, glass_y, (0.3 + shell_top - 0.1) / 2), 0.035,
                       shell_top - 0.4, glass_mat, vertices=12)
    glass.parent = tank
    glass.matrix_parent_inverse = tank.matrix_world.inverted()
    marker = box(level["node"], level["pivot_m"], (0.1, 0.1, 0.06), float_mat)
    moving_part(marker, tank, level["pivot_m"])
    for spec_port in spec["ports"]:
        port(spec_port)
    animate(marker, level)
    bpy.context.scene.frame_set(0)


def build_pump(spec):
    """A centrifugal pump: base plate, motor, volute with suction and
    discharge pipes, a terminal box, and the motor's cooling fan, whose
    turning shows the pump's speed."""
    length, width, height = spec["dimensions_m"]
    mats = spec["materials"]
    body_mat = material(mats["body"], (0.15, 0.35, 0.60), 0.4, 0.45)
    shaft_mat = material(mats["shaft"], (0.75, 0.74, 0.70), 0.9, 0.25)
    suction, discharge, electric = (p["position_m"] for p in spec["ports"])
    axis_z = suction[2]
    base = box("base", (0.0, 0.0, 0.05), (length, width, 0.1), body_mat)
    motor = cylinder_x("motor", (-0.3, 0.0, axis_z), 0.25, 0.7, body_mat, vertices=32)
    feet = box("feet", (-0.3, 0.0, (0.1 + axis_z - 0.15) / 2), (0.5, 0.4, axis_z - 0.25), body_mat)
    volute = cylinder_y("volute", (discharge[0], 0.0, axis_z), 0.33, 0.25, body_mat, vertices=32)
    inlet = cylinder_x("suction", ((discharge[0] + suction[0]) / 2 + 0.07, 0.0, axis_z), 0.08,
                       suction[0] - discharge[0] - 0.14, body_mat, vertices=16)
    outlet = cylinder_z("discharge", (discharge[0], 0.0, (axis_z + 0.3 + discharge[2]) / 2), 0.08,
                        discharge[2] - axis_z - 0.3, body_mat, vertices=16)
    terminal = box("terminal_box", (electric[0], (electric[1] - 0.2) / 2, axis_z),
                   (0.18, abs(electric[1]) - 0.2, 0.18), body_mat)
    pump = join("pump", [base, motor, feet, volute, inlet, outlet, terminal])
    animation = spec["animations"]["speed"]
    pivot = animation["pivot_m"]
    fan = cylinder_x(animation["node"], pivot, 0.2, 0.04, shaft_mat, vertices=6)
    moving_part(fan, pump, pivot)
    for spec_port in spec["ports"]:
        port(spec_port)
    animate(fan, animation)
    bpy.context.scene.frame_set(0)


def build_reactor(spec):
    """A reactor: skirt, vessel with a domed head and heat bands, feed and
    coolant nozzles, and a turbine on the side driving the output shaft."""
    length, width, height = spec["dimensions_m"]
    mats = spec["materials"]
    vessel_mat = material(mats["vessel"], (0.60, 0.60, 0.62), 0.6, 0.4)
    heat_mat = material(mats["heat"], (0.30, 0.28, 0.27), 0.7, 0.4, emission=(1.0, 0.35, 0.05))
    shaft_mat = material(mats["shaft"], (0.75, 0.74, 0.70), 0.9, 0.25)
    feed, coolant, shaft_port = (p["position_m"] for p in spec["ports"])
    radius, head = 0.75, 0.4
    shell_top = height - head
    skirt = box("skirt", (0.0, 0.0, 0.15), (length, width, 0.3), vessel_mat)
    shell = cylinder_z("vessel", (0.0, 0.0, (0.3 + shell_top) / 2), radius, shell_top - 0.3,
                       vessel_mat, vertices=48)
    top = dome("head", (0.0, 0.0, shell_top), radius, head, vessel_mat)
    feed_pipe = cylinder_x("feed", ((feed[0] - radius + 0.05) / 2, 0.0, feed[2]), 0.09,
                           -feed[0] - radius + 0.05, vessel_mat, vertices=16)
    coolant_pipe = cylinder_y("coolant", (0.0, (coolant[1] + radius - 0.05) / 2, coolant[2]), 0.07,
                              coolant[1] - radius + 0.05, vessel_mat, vertices=16)
    # The turbine casing stops short of the port, so the coupling shows.
    turbine = cylinder_x("turbine", (shaft_port[0] - 0.25, 0.0, shaft_port[2]), 0.3, 0.3,
                         vessel_mat, vertices=32)
    reactor = join("reactor", [skirt, shell, top, feed_pipe, coolant_pipe, turbine])
    bands = [cylinder_z(f"band_{i}", (0.0, 0.0, z), radius + 0.04, 0.12, heat_mat, vertices=48)
             for i, z in enumerate((1.0, 1.5, 2.0))]
    heat = join("heat_bands", bands)
    heat.parent = reactor
    heat.matrix_parent_inverse = reactor.matrix_world.inverted()
    animation = spec["animations"]["speed"]
    pivot = animation["pivot_m"]
    stub = cylinder_x("shaft_stub", (shaft_port[0] - 0.09, 0.0, shaft_port[2]), 0.06,
                      0.04, shaft_mat, vertices=16)
    coupling = cylinder_x("coupling", (shaft_port[0] - 0.04, 0.0, shaft_port[2]), 0.16, 0.08,
                          shaft_mat, vertices=6)
    output = join(animation["node"], [stub, coupling])
    moving_part(output, reactor, pivot)
    for spec_port in spec["ports"]:
        port(spec_port)
    animate(output, animation)
    bpy.context.scene.frame_set(0)


def build_cooler(spec):
    """A dry cooler: a cabinet with two fans on top, a power inlet and two
    coolant outlets."""
    length, width, height = spec["dimensions_m"]
    mats = spec["materials"]
    cabinet_mat = material(mats["cabinet"], (0.80, 0.80, 0.78), 0.3, 0.5)
    fan_mat = material(mats["fan"], (0.10, 0.10, 0.12), 0.2, 0.6)
    ports = [p["position_m"] for p in spec["ports"]]
    cabinet = box("cabinet", (0.0, 0.0, 0.7), (length - 0.1, width, 1.4), cabinet_mat)
    parts = [cabinet]
    fans = [a for key, a in sorted(spec["animations"].items())]
    for animation in fans:
        x = animation["pivot_m"][0]
        parts.append(cylinder_z(f"shroud_{animation['node']}", (x, 0.0, 1.5), 0.45, 0.2,
                                cabinet_mat, vertices=32, open_ends=True))
    power = ports[0]
    parts.append(box("power_inlet", (power[0] + 0.05, 0.0, power[2]), (0.1, 0.25, 0.25),
                     cabinet_mat))
    for index, outlet in enumerate(ports[1:]):
        parts.append(cylinder_x(f"outlet_{index}", (outlet[0] - 0.05, outlet[1], outlet[2]), 0.07,
                                0.1, cabinet_mat, vertices=16))
    cooler = join("cooler", parts)
    for animation in fans:
        pivot = animation["pivot_m"]
        blade_a = box(f"{animation['node']}_a", pivot, (0.8, 0.12, 0.02), fan_mat)
        blade_b = box(f"{animation['node']}_b", pivot, (0.12, 0.8, 0.02), fan_mat)
        fan = join(animation["node"], [blade_a, blade_b])
        moving_part(fan, cooler, pivot)
        animate(fan, animation)
    for spec_port in spec["ports"]:
        port(spec_port)
    bpy.context.scene.frame_set(0)


# The specification's "family" (default: the generator, spec v0.1).
FAMILIES = {"generator": build_generator, "tank": build_tank, "pump": build_pump,
            "reactor": build_reactor, "cooler": build_cooler}


def main():
    args = arguments()
    with open(args.spec) as stream:
        spec = json.load(stream)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    FAMILIES[spec.get("family", "generator")](spec)

    os.makedirs(args.out, exist_ok=True)
    glb = os.path.join(args.out, spec["id"] + ".glb")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", export_extras=True,
                              export_yup=True, export_apply=True, export_animations=True)
    canonical_triangle_order(glb)

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
