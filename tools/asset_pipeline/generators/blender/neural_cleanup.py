"""AssetForge Phase 2 (docs/assetforge_document §2, §5; ADR-0031): a neural
mesh candidate cleaned up for the game, in Blender.

    blender -b --factory-startup --python neural_cleanup.py -- \
        --spec SPEC.json --out DIR [--commit HASH]

From the specification's source GLB (whose hash it checks): one object at
the base centre (+Z up), scaled to the specified height, decimated to the
triangle budget, its textures downsized to the texture budget and its
material named. Writes DIR/<id>.glb, .manifest.json and .provenance.json,
whose provenance chains the concept image, the image-to-3D step and this
one. Nothing random and nothing timed: the same inputs give the same bytes.
"""

import argparse
import hashlib
import json
import os
import sys

import bmesh
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


def hull_leftovers(result):
    """The points convex_hull left inside or unused, each once, in order."""
    points = [g for g in result["geom_interior"] + result["geom_unused"]
              if isinstance(g, bmesh.types.BMVert)]
    return list(dict.fromkeys(points))


def main():
    args = arguments()
    spec = json.load(open(args.spec))
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))))))
    source = os.path.join(root, spec["source"]["mesh"])
    if sha256(source) != spec["source"]["mesh_sha256"]:
        raise SystemExit(f"assetforge: {source} does not match the specification's hash")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    rock = bpy.context.active_object
    # Free it from the importer's empties, keeping its world placement.
    matrix = rock.matrix_world.copy()
    rock.parent = None
    rock.matrix_world = matrix
    for obj in list(bpy.context.scene.objects):
        if obj is not rock:
            bpy.data.objects.remove(obj, do_unlink=True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # The origin at the base centre, then the specified height.
    points = [rock.matrix_world @ v.co for v in rock.data.vertices]
    low = Vector([min(p[i] for p in points) for i in range(3)])
    high = Vector([max(p[i] for p in points) for i in range(3)])
    base = Vector(((low.x + high.x) / 2, (low.y + high.y) / 2, low.z))
    scale = spec["height_m"] / (high.z - low.z)
    for vertex in rock.data.vertices:
        vertex.co = (vertex.co - base) * scale
    rock.data.update()

    # The triangle budget, with a margin.
    triangles = sum(len(polygon.vertices) - 2 for polygon in rock.data.polygons)
    target = 0.95 * spec["budgets"]["max_triangles"]
    if triangles > target:
        modifier = rock.modifiers.new("decimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = target / triangles
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)

    # Names, and the textures within budget.
    rock.name = spec["id"]
    rock.data.name = spec["id"]
    materials = [slot.material for slot in rock.material_slots if slot.material]
    if len(materials) != 1:
        raise SystemExit(f"assetforge: expected one material, found {len(materials)}")
    materials[0].name = spec["materials"]["surface"]
    limit = spec["budgets"]["max_texture_px"]
    for image in bpy.data.images:
        width, height = image.size
        if max(width, height) > limit:
            factor = limit / max(width, height)
            image.scale(round(width * factor), round(height * factor))

    # The collision shape: the convex hull of the visual mesh, reduced to
    # its vertex budget. Godot's import turns a node named with the
    # suffix into a body with a convex shape (and no visual mesh).
    collision = spec.get("collision")
    if collision:
        hull_mesh = bpy.data.meshes.new(spec["id"] + collision["node_suffix"])
        work = bmesh.new()
        for vertex in rock.data.vertices:
            work.verts.new(vertex.co)
        result = bmesh.ops.convex_hull(work, input=work.verts)
        # Drop interior points and the hull's unused geometry.
        bmesh.ops.delete(work, geom=hull_leftovers(result), context="VERTS")
        work.to_mesh(hull_mesh)
        work.free()
        hull = bpy.data.objects.new(hull_mesh.name, hull_mesh)
        bpy.context.scene.collection.objects.link(hull)
        if len(hull_mesh.vertices) > collision["max_vertices"]:
            bpy.ops.object.select_all(action="DESELECT")
            hull.select_set(True)
            bpy.context.view_layer.objects.active = hull
            modifier = hull.modifiers.new("reduce", "DECIMATE")
            modifier.decimate_type = "COLLAPSE"
            modifier.ratio = 0.9 * collision["max_vertices"] / len(hull_mesh.vertices)
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            # Collapsing moves points inwards: the hull of what is left.
            work = bmesh.new()
            work.from_mesh(hull_mesh)
            result = bmesh.ops.convex_hull(work, input=work.verts)
            bmesh.ops.delete(work, geom=hull_leftovers(result), context="VERTS")
            work.to_mesh(hull_mesh)
            work.free()

    os.makedirs(args.out, exist_ok=True)
    glb = os.path.join(args.out, spec["id"] + ".glb")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", export_yup=True,
                              export_apply=True, export_animations=False)

    manifest = {
        "id": spec["id"],
        "type": spec["type"],
        "geometry": os.path.basename(glb),
        "height_m": spec["height_m"],
        "materials": spec["materials"],
        "ports": [],
        "animations": {},
    }
    if collision:
        manifest["collision"] = {"shape": collision["shape"],
                                 "node": spec["id"] + collision["node_suffix"]}
    with open(os.path.join(args.out, spec["id"] + ".manifest.json"), "w") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")
    concept = json.load(open(os.path.join(root, spec["source"]["concept_provenance"])))
    # The image-to-3D step's own record (sf3d_remote.py), which must be of
    # this mesh, from this concept image.
    image_to_3d = json.load(open(os.path.join(root, spec["source"]["image_to_3d_provenance"])))
    if image_to_3d["output_sha256"] != spec["source"]["mesh_sha256"] or \
            image_to_3d["input_image_sha256"] != concept["image_sha256"]:
        raise SystemExit("assetforge: the image-to-3D record does not match the mesh and image")
    provenance = {
        "asset": spec["id"],
        "specification": os.path.relpath(args.spec, root),
        "specification_sha256": sha256(args.spec),
        "chain": [
            {"step": "concept image", **concept},
            image_to_3d,
            {"step": "clean-up", "generator": os.path.relpath(__file__, root),
             "generator_sha256": sha256(__file__), "blender": bpy.app.version_string,
             "commit": args.commit},
        ],
        "geometry_sha256": sha256(glb),
        "licences": [concept["licence"], image_to_3d["licence"]],
        "approval": "pending human review: a neural candidate",
    }
    with open(os.path.join(args.out, spec["id"] + ".provenance.json"), "w") as stream:
        json.dump(provenance, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("assetforge: wrote", glb)


if __name__ == "__main__":
    main()
