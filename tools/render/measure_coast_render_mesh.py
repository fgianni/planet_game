"""Measure fixed-pose ADR-0030 geometry probe masks; not an acceptance gate.

Usage: python3 tools/render/measure_coast_render_mesh.py /tmp/opencode/coast-render-L4
Requires Pillow, NumPy and SciPy. Images are probe-only unshaded classes.
"""
import argparse
import os

import numpy as np
from PIL import Image
from scipy import ndimage


def read(path):
    rgb = np.asarray(Image.open(path).convert("RGB"))
    visible = rgb.max(axis=2) > 32
    water = (rgb[:, :, 2] > rgb[:, :, 0]) & visible
    return water, visible


def compare(a, b, region):
    difference = (a != b) & region
    # Pixel-centre distance to the reference boundary; also report the
    # connected mismatch islands (8-connectivity). A disagreement at the
    # antialiased line is still counted, but not assumed to be >1 px movement.
    boundary = ndimage.binary_dilation(b) != ndimage.binary_erosion(b)
    distance = ndimage.distance_transform_edt(~boundary)
    labels, count = ndimage.label(difference, structure=np.ones((3, 3)))
    sizes = np.bincount(labels.ravel())[1:]
    return dict(pixels=int(difference.sum()), max_distance_px=float(distance[difference].max())
                if difference.any() else 0.0,
                beyond_one_px=int((difference & (distance > 1)).sum()),
                components=count, components_at_least_four=int((sizes >= 4).sum()),
                 max_component_px=int(sizes.max()) if sizes.size else 0)


def face_pair_metrics(same, same_visible, mixed, mixed_visible):
    common = ndimage.binary_erosion(same_visible & mixed_visible, iterations=2)
    return dict(visible_same=int(same_visible.sum()),
                visible_mixed=int(mixed_visible.sum()),
                visible_xor=int((same_visible ^ mixed_visible).sum()),
                common_interior=int(common.sum()),
                 water=compare(same, mixed, common))


def cell_patch_metrics(cell, cell_visible, patch, patch_visible, common):
    # The patch silhouette, not the much larger cell globe, defines the
    # comparison. Erode again to exclude its clipped and antialiased edge.
    region = ndimage.binary_erosion(common & cell_visible, iterations=2)
    return dict(common_interior=int(region.sum()),
                cell_water=int((cell & region).sum()),
                patch_water=int((patch & region).sum()),
                patch_center_visible=bool(patch_visible[patch.shape[0] // 2,
                                                        patch.shape[1] // 2]),
                water=compare(cell, patch, region))


def fan_patch_metrics(fan, fan_visible, patch, patch_visible, common):
    region = ndimage.binary_erosion(common & fan_visible, iterations=2)
    return dict(common_interior=int(region.sum()),
                patch_missing=int((patch_visible & ~fan_visible).sum()),
                fan_outside_patch=int((fan_visible & ~patch_visible).sum()),
                water=compare(fan, patch, region))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix")
    parser.add_argument("--face-pair", action="store_true",
                        help="compare complete same-level and mixed-level opposite-face meshes")
    args = parser.parse_args()
    import glob
    if args.face_pair:
        poses = sorted(glob.glob(args.prefix + "-*-face-same.png"))
        if not poses:
            parser.error("no face-pair renders found")
        for same_path in poses:
            stem = same_path.removesuffix("-face-same.png")
            same, sv = read(same_path)
            mixed, mv = read(stem + "-face-mixed.png")
            print(stem, face_pair_metrics(same, sv, mixed, mv))
        return
    poses = sorted(glob.glob(args.prefix + "-*-parent.png"))
    if not poses:
        parser.error("no parent renders found")
    for parent_path in poses:
        stem = parent_path.removesuffix("-parent.png")
        parent, pv = read(parent_path)
        children, cv = read(stem + "-children.png")
        detail, dv = read(stem + "-detail.png")
        common = ndimage.binary_erosion(pv & cv & dv, iterations=2)
        print(stem, "common interior pixels", int(common.sum()),
              "visible-mask differences", int(((pv != cv) | (pv != dv)).sum()))
        print("  parent vs children:", compare(parent, children, common))
        print("  parent vs detailed relief:", compare(parent, detail, common))
        print("  children vs detailed relief:", compare(children, detail, common))
        if os.path.exists(stem + "-ultra.png"):
            ultra, uv = read(stem + "-ultra.png")
            finer_region = ndimage.binary_erosion(common & uv, iterations=2)
            print("  children vs twice-fine relief:", compare(children, ultra, finer_region))
            print("  detail vs twice-fine relief:", compare(detail, ultra, finer_region))
        if os.path.exists(stem + "-smooth.png"):
            smooth, sv = read(stem + "-smooth.png")
            smooth_region = ndimage.binary_erosion(common & sv, iterations=2)
            print("  smooth vs shared-coast parent:", compare(smooth, parent, smooth_region))
            print("  smooth vs shared-coast children:", compare(smooth, children, smooth_region))
        if os.path.exists(stem + "-cell.png"):
            cell, cell_visible = read(stem + "-cell.png")
            print("  actual cell fan vs shared patch (colour heuristic):",
                  cell_patch_metrics(cell, cell_visible, parent, pv, common))
        if os.path.exists(stem + "-fan.png"):
            fan, fv = read(stem + "-fan.png")
            print("  sampled dual-fan constraint vs shared patch:",
                  fan_patch_metrics(fan, fv, parent, pv, common))


if __name__ == "__main__":
    main()
