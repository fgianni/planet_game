"""AssetForge Phase 3: render a layout (layout.py) for review, from the
modules' built GLBs and the routes (pipes grey, cables orange). For people,
not part of any check.

    blender -b --factory-startup --python render_assembly.py -- \
        --layout LAYOUT.json --built assets/generated --out IMAGE.png
"""

import argparse
import math
import sys

import bpy
from mathutils import Vector

COLOURS = {"pipe": (0.55, 0.57, 0.6), "cable": (0.95, 0.45, 0.05), "conveyor": (0.3, 0.3, 0.3)}


def segment(name, a, b, radius, material):
    a, b = Vector(a), Vector(b)
    length = (b - a).length
    if length < 1e-9:
        return
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=radius, depth=length, location=(a + b) / 2)
    obj = bpy.context.active_object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = Vector((0, 0, 1)).rotation_difference((b - a).normalized())
    obj.data.materials.append(material)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True)
    parser.add_argument("--built", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    import json
    layout = json.load(open(args.layout))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    for module in layout["modules"]:
        before = set(scene.objects)
        bpy.ops.import_scene.gltf(filepath=f"{args.built}/{module['asset']}/{module['asset']}.glb")
        holder = bpy.data.objects.new(module["id"], None)
        scene.collection.objects.link(holder)
        for obj in set(scene.objects) - before:
            if obj.parent is None and obj is not holder:
                obj.parent = holder
        holder.location = module["position_m"]
        holder.rotation_euler = (0.0, 0.0, math.radians(module["yaw_deg"]))

    materials = {}
    for medium, colour in COLOURS.items():
        material = bpy.data.materials.new(medium)
        material.use_nodes = True
        shader = material.node_tree.nodes["Principled BSDF"]
        shader.inputs["Base Color"].default_value = (*colour, 1.0)
        shader.inputs["Roughness"].default_value = 0.5
        materials[medium] = material
    for index, route in enumerate(layout["routes"]):
        if route["medium"] == "shaft":
            continue
        for leg, (a, b) in enumerate(zip(route["points"], route["points"][1:])):
            segment(f"route_{index}_{leg}", a, b, route["radius_m"], materials[route["medium"]])

    points = [obj.matrix_world @ Vector(c) for obj in scene.objects if obj.type == "MESH" for c in obj.bound_box]
    low = Vector([min(p[i] for p in points) for i in range(3)])
    high = Vector([max(p[i] for p in points) for i in range(3)])
    centre, radius = (low + high) / 2, (high - low).length / 2
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    scene.collection.objects.link(camera)
    camera.location = centre + Vector((-0.9, -1.3, 1.1)).normalized() * radius * 2.6
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(45), 0, math.radians(-30))
    scene.collection.objects.link(sun)
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.6, 0.65, 0.7, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.4
    scene.world = world
    bpy.ops.mesh.primitive_plane_add(size=40, location=(centre.x, centre.y, 0))
    ground = bpy.data.materials.new("ground")
    ground.use_nodes = True
    ground.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.35, 0.37, 0.33, 1)
    bpy.context.active_object.data.materials.append(ground)

    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 32
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 800
    scene.render.filepath = args.out
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()
