"""AssetForge: render a preview of a built GLB (Cycles on the CPU, so it runs
without a display). A preview is for people; it is not part of the
reproducibility check.

    blender -b --factory-startup --python render_preview.py -- --glb ASSET.glb --out IMAGE.png
"""

import argparse
import math
import sys

import bpy
from mathutils import Vector


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=args.glb)
    scene = bpy.context.scene

    # Frame the asset from the front-left, above.
    points = [obj.matrix_world @ Vector(corner) for obj in scene.objects if obj.type == "MESH"
              for corner in obj.bound_box]
    low = Vector([min(p[i] for p in points) for i in range(3)])
    high = Vector([max(p[i] for p in points) for i in range(3)])
    centre = (low + high) / 2
    radius = (high - low).length / 2

    camera_data = bpy.data.cameras.new("camera")
    camera_data.lens = 50
    camera = bpy.data.objects.new("camera", camera_data)
    scene.collection.objects.link(camera)
    direction = Vector((-1.0, -1.4, 0.8)).normalized()
    camera.location = centre + direction * radius * 3.4
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera

    for name, energy, rotation in (("key", 4.0, (math.radians(50), 0, math.radians(-30))),
                                   ("fill", 1.5, (math.radians(70), 0, math.radians(140)))):
        light = bpy.data.objects.new(name, bpy.data.lights.new(name, "SUN"))
        light.data.energy = energy
        light.rotation_euler = rotation
        scene.collection.objects.link(light)
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.6, 0.65, 0.7, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    scene.world = world

    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 32
    scene.render.resolution_x = 800
    scene.render.resolution_y = 600
    scene.render.filepath = args.out
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()
