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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix")
    args = parser.parse_args()
    import glob
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


if __name__ == "__main__":
    main()
