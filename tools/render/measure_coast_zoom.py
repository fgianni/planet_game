#!/usr/bin/env python3
"""Summarize probe-only planet/cell-to-tile zoom images, not an ADR-0030 gate.

Usage: python3 tools/render/measure_coast_zoom.py /tmp/opencode/coast-zoom-baseline
The captured blend uses the real camera's band weight. Endpoint comparisons
are fixed-pose map-style colour heuristics, not canonical contour distances.
"""

import argparse
import glob

from measure_coast_handoff import classify, components, shore_distance


def metrics(cell_valid, cell_water, tile_valid, tile_water, blend_valid, blend_water):
    common = cell_valid & tile_valid & blend_valid
    changed = {p for p in common if (p in cell_water) != (p in tile_water)}
    cell_dist = shore_distance(common, cell_water)
    tile_dist = shore_distance(common, tile_water)
    # If a feature is absent, that mask has no boundary near its pixels.
    # Treat the missing boundary as infinitely far, not zero distance.
    deep = sum(cell_dist.get(p, float("inf")) > 1 and
               tile_dist.get(p, float("inf")) > 1 for p in changed)
    return {
        "common": len(common), "endpoint_changes": len(changed), "deep": deep,
        "largest_component": max(components(changed), default=0),
        "cell_shore_pixels": len(cell_dist) and sum(value == 0 for value in cell_dist.values()),
        "tile_shore_pixels": len(tile_dist) and sum(value == 0 for value in tile_dist.values()),
        "blend_vs_cell": sum((p in blend_water) != (p in cell_water) for p in common),
        "blend_vs_tile": sum((p in blend_water) != (p in tile_water) for p in common),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix")
    args = parser.parse_args()
    stems = sorted(path.removesuffix("-cell.png") for path in
                   glob.glob(args.prefix + "-zoom-??-cell.png"))
    if not stems:
        parser.error("no zoom captures found")
    for stem in stems:
        cell = classify(stem + "-cell.png")
        tile = classify(stem + "-tile.png")
        blend = classify(stem + "-blend.png")
        print(stem, metrics(*cell, *tile, *blend))


if __name__ == "__main__":
    main()
