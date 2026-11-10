#!/usr/bin/env python3
"""Headless page-versus-independent-field mask diagnostics (not rendered gates).

Usage: python3 tools/render/measure_coast_page_screen.py PREFIX
The native probe writes raw pixel-centred masks at the same unit-sphere
camera pose. This measures binary contour distance, not antialiased matched
contours or production cell/tile geometry. Components are reported separately
from their change blobs; an inlet connected to the frame is not certified.
"""

import argparse
from pathlib import Path

import numpy as np
from scipy import ndimage


def read(prefix):
    info = dict(line.split() for line in Path(f"{prefix}-info.txt").read_text().splitlines())
    size = int(info["size"])
    def mask(kind):
        values = np.fromfile(f"{prefix}-{kind}.raw", dtype=np.uint8)
        if values.size != size * size or np.any(values > 1):
            raise ValueError(f"invalid {kind} mask")
        return values.reshape(size, size).astype(bool)
    return info, mask("visible"), mask("canonical"), mask("page")


def metric(visible, canonical, page):
    if visible.shape != canonical.shape or page.shape != canonical.shape:
        raise ValueError("mask dimensions differ")
    interior = ndimage.binary_erosion(visible, iterations=2)
    mismatch = (canonical ^ page) & interior
    structure = np.ones((3, 3), dtype=bool)
    def shore(mask):
        return (mask ^ ndimage.binary_erosion(mask, structure=structure)) | (
            mask ^ ndimage.binary_dilation(mask, structure=structure))
    # Both directions matter: a newly resolved shore can be far from the
    # coarse page's shore, even if it hugs the canonical field's shore.
    reference_depth = ndimage.distance_transform_edt(~shore(canonical))[mismatch]
    page_depth = ndimage.distance_transform_edt(~shore(page))[mismatch]
    result = dict(interior=int(interior.sum()), changes=int(mismatch.sum()),
                  beyond_one_reference_px=int((reference_depth > 1).sum()),
                  beyond_one_either_px=int(((reference_depth > 1) | (page_depth > 1)).sum()),
                  max_from_reference_shore_px=float(reference_depth.max(initial=0)),
                  max_from_page_shore_px=float(page_depth.max(initial=0)))
    labels, count = ndimage.label(mismatch, structure=structure)
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    deep = (reference_depth > 1) | (page_depth > 1)
    result["max_deep_change_component_px"] = int(sizes[labels[mismatch][deep]].max(initial=0))
    border = interior & ~ndimage.binary_erosion(interior, structure=structure)
    for name, mask in (("reference", canonical), ("page", page)):
        counterpart = page if name == "reference" else canonical
        for kind, region in (("water", mask), ("land", ~mask)):
            labels, count = ndimage.label(region & interior, structure=structure)
            sizes = np.bincount(labels.ravel(), minlength=count + 1)
            missing = 0
            other_region = counterpart if kind == "water" else ~counterpart
            for label in range(1, count + 1):
                component = labels == label
                if (sizes[label] >= 4 and not np.any(component & border) and
                        not np.any(component & other_region)):
                    missing += 1
            result[f"{name}_{kind}_components_ge4"] = int(np.count_nonzero(sizes[1:] >= 4))
            result[f"{name}_{kind}_components_missing_ge4"] = missing
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix")
    parser.add_argument("--other", help="another resident page level at the same camera pose")
    args = parser.parse_args()
    info, visible, canonical, page = read(args.prefix)
    print(info)
    print(metric(visible, canonical, page))
    if args.other:
        other_info, other_visible, other_canonical, other_page = read(args.other)
        for key in ("size", "level", "seed", "altitude", "fov"):
            if info[key] != other_info[key]:
                parser.error(f"different matched pose: {key}")
        if not np.array_equal(visible, other_visible) or not np.array_equal(canonical, other_canonical):
            parser.error("reference or visibility masks differ at matched pose")
        print("page-to-page:", metric(visible, page, other_page))


if __name__ == "__main__":
    main()
