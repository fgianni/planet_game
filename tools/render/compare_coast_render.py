#!/usr/bin/env python3
"""R1-05's rendered gates (ADR-0030 Amendment 1): a zoom sweep against the
canonical coast.

Usage: compare_coast_render.py SWEEP_DIR LEVEL SEED [--reference BIN]

SWEEP_DIR holds godot/tests/capture_coast_sweep.gd's frames (NN_arrive and
NN_settled: PNG water masks and camera files). For each frame the reference
(tools/render/coast_render_reference.cpp) casts the same rays at the
canonical water field, with enough rays a pixel to resolve the relief's
finest features (at most 16 x 16). Each pose reports:

- each frame against the reference;
- the handoff: the arriving frame (previous pose's tiles and pages) against
  the settled one, at the same pose.

A pixel is water at coverage above 0.5. A differing pixel is deep when the
reference has no shore in it or its eight neighbours (all nine fully water,
or fully land): the drawn shore is then more than a pixel from the
canonical one (ADR-0030's "farther than one projected pixel from the
canonical contour"). Deep regions are 8-connected regions of
differences of at least four pixels that hold a deep pixel. The gate
(ADR-0030 and A1.5a), for each comparison: where a pixel resolves the
relief's finest features (no wider than half its wavelength), no deep
pixel; where it does not, no pixel's coverage changing by 0.5 or more.
For the handoff also: no deep region (an island or inlet of four pixels or
more that appears or disappears solely with LOD). With --warmup N, the
first N poses (the sweep's cold start) are reported, not gated.
"""

import argparse
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

RADIUS_KM = 6371.0
EIGHT = np.ones((3, 3), dtype=bool)


def frame_info(path):
    info = {}
    for line in Path(path).read_text().splitlines():
        key, *values = line.split()
        info[key] = values
    return info


def reference(binary, level, seed, frame, rays):
    out = frame.with_suffix("")
    subprocess.run([binary, "reference", str(level), str(seed), str(frame), str(out), str(rays)], check=True)
    info = frame_info(frame)
    shape = (int(info["height"][0]), int(info["width"][0]))
    coverage = np.fromfile(f"{out}.coverage", dtype=np.float32).reshape(shape)
    visible = np.fromfile(f"{out}.visible", dtype=np.uint8).reshape(shape).astype(bool)
    return coverage, visible


def rendered(png):
    grey = np.asarray(Image.open(png).convert("RGB"), dtype=np.float32)[:, :, 0]
    return grey / 255.0


def compare(drawn, truth, valid):
    """drawn and truth: coverages; valid: the pixels to judge."""
    wet_drawn = drawn > 0.5
    wet_truth = truth > 0.5
    differ = (wet_drawn != wet_truth) & valid
    # Deep: no shore in the reference's 3 x 3 neighbourhood (all of it fully
    # water or fully land).
    all_wet = ndimage.binary_erosion(truth >= 1.0, structure=EIGHT, border_value=1)
    all_dry = ndimage.binary_erosion(truth <= 0.0, structure=EIGHT, border_value=1)
    deep = differ & (all_wet | all_dry)
    labels, count = ndimage.label(differ, structure=EIGHT)
    deep_regions = 0
    largest = 0
    if count:
        sizes = ndimage.sum(differ, labels, index=np.arange(1, count + 1))
        deep_in = ndimage.maximum(deep, labels, index=np.arange(1, count + 1))
        deep_regions = int(np.sum((sizes >= 4) & (deep_in > 0)))
        largest = int(sizes.max())
    change = np.abs(drawn - truth)[valid]
    return dict(differ=int(differ.sum()), deep=int(deep.sum()), deep_regions=deep_regions, largest=largest,
                jumps=int(np.sum(change >= 0.5)), max_change=float(change.max()) if change.size else 0.0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sweep")
    parser.add_argument("level", type=int)
    parser.add_argument("seed", type=int)
    parser.add_argument("--reference", default="build/coast_render_reference")
    parser.add_argument("--warmup", type=int, default=0)
    args = parser.parse_args()
    sweep = Path(args.sweep)
    # The relief's finest octave wavelength (terrain_relief.hpp: 1.5
    # features a cell, 8 octaves), from the mesh's cell spacing.
    cells = 10 * 4 ** args.level + 2
    spacing_km = math.sqrt(4.0 * math.pi / cells) * RADIUS_KM
    finest_km = spacing_km / (1.5 * 2 ** 7)
    failures = 0
    print(f"L{args.level} seed {args.seed}: finest wavelength {finest_km:.3f} km")
    print("pose altitude band pixel_km resolved | arrive vs field | settled vs field | handoff arrive->settled"
          "  (differ/deep/deep regions/largest/jumps/max change)")
    for settled_txt in sorted(sweep.glob("*_settled.txt")):
        stem = settled_txt.name.split("_")[0]
        arrive_txt = sweep / f"{stem}_arrive.txt"
        info = frame_info(settled_txt)
        altitude_km = float(info["altitude_km"][0])
        tangent = float(info["tangent"][0])
        height = int(info["height"][0])
        # The nadir pixel's ground size (the camera may tilt at low altitude:
        # an estimate).
        pixel_km = altitude_km * 2.0 * tangent / height
        resolved = pixel_km <= finest_km / 2.0
        rays = int(min(16, max(4, math.ceil(pixel_km / (finest_km / 2.0)))))
        truth, visible = reference(args.reference, args.level, args.seed, settled_txt, rays)
        valid = ndimage.binary_erosion(visible, structure=EIGHT, iterations=2)
        settled = rendered(sweep / f"{stem}_settled.png")
        results = [compare(settled, truth, valid)]
        arrive = None
        if arrive_txt.exists():
            arrive = rendered(sweep / f"{stem}_arrive.png")
            results.insert(0, compare(arrive, truth, valid))
            results.append(compare(arrive, settled, valid))
        line = f"{stem} {altitude_km:.0f} {info['band'][0]} {pixel_km:.3f} {'yes' if resolved else 'no'}"
        for r in results:
            line += " | {differ}/{deep}/{deep_regions}/{largest}/{jumps}/{max_change:.2f}".format(**r)
        print(line)
        if int(stem) < args.warmup:
            continue
        for index, r in enumerate(results):
            handoff = arrive is not None and index == len(results) - 1
            bad = (r["deep"] > 0 if resolved else r["jumps"] > 0) or (handoff and r["deep_regions"] > 0)
            failures += 1 if bad else 0
    print(f"{failures} comparisons outside the gate")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
