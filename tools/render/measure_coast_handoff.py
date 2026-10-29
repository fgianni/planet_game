#!/usr/bin/env python3
"""Read-only map-style water-mask comparison for the ADR-0030 screen gate.

Usage: python3 tools/render/measure_coast_handoff.py cell.png tile.png
The map-style colour heuristic is a diagnostic, not a georeferenced contour
or a substitute for the final rendered zoom validation.
"""

import argparse
from collections import deque

from PIL import Image


def classify(path: str):
    image = Image.open(path).convert("RGB")
    width, height = image.size
    if width <= 160 or height <= 160:
        raise ValueError(f"{path}: image must be larger than 160 px in both dimensions")
    pixels = image.load()
    # The Godot probe uses a 512px square and a fixed camera. Ignore the
    # silhouette and compare its central 352px square only.
    box = (80, 80, width - 80, height - 80)
    valid = set()
    water = set()
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            r, g, b = pixels[x, y]
            if max(r, g, b) < 21:
                continue
            valid.add((x, y))
            if b > r * 1.25 and b > g * 1.15:
                water.add((x, y))
    return valid, water


NEIGHBOURS = [(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy]


def shore_distance(valid: set, water: set):
    edge = {p for p in valid if any(
        (p[0] + dx, p[1] + dy) in valid and
        ((p in water) != ((p[0] + dx, p[1] + dy) in water))
        for dx, dy in NEIGHBOURS
    )}
    distance = dict.fromkeys(edge, 0)
    queue = deque(edge)
    while queue:
        x, y = queue.popleft()
        for dx, dy in NEIGHBOURS:
            neighbour = (x + dx, y + dy)
            if neighbour in valid and neighbour not in distance:
                distance[neighbour] = distance[(x, y)] + 1
                queue.append(neighbour)
    return distance


def components(points: set):
    remaining = set(points)
    sizes = []
    while remaining:
        start = remaining.pop()
        queue = [start]
        count = 0
        while queue:
            x, y = queue.pop()
            count += 1
            for dx, dy in NEIGHBOURS:
                neighbour = (x + dx, y + dy)
                if neighbour in remaining:
                    remaining.remove(neighbour)
                    queue.append(neighbour)
        sizes.append(count)
    return sorted(sizes, reverse=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cell")
    parser.add_argument("tile")
    args = parser.parse_args()
    cell_valid, cell_water = classify(args.cell)
    tile_valid, tile_water = classify(args.tile)
    if Image.open(args.cell).size != Image.open(args.tile).size:
        parser.error("cell and tile images must have the same dimensions")
    common = cell_valid & tile_valid
    mismatch = {p for p in common if (p in cell_water) != (p in tile_water)}
    cell_dist = shore_distance(common, cell_water)
    tile_dist = shore_distance(common, tile_water)
    # Deep disagreement: both boundaries are more than one pixel away.
    deep = {p for p in mismatch if cell_dist.get(p, 0) > 1 and tile_dist.get(p, 0) > 1}
    changed_components = components(mismatch)
    print(f"compared {len(common)} pixels; water classification changes {len(mismatch)} "
          f"({100 * len(mismatch) / max(1, len(common)):.2f}%)")
    print(f"disagreements >1 px inside both masks: {len(deep)}; "
          f"largest changed component {changed_components[0] if changed_components else 0} px; "
          f"changed components >=4 px: {sum(size >= 4 for size in changed_components)}")
    print("NOTE: components of disagreement are not necessarily disappearing islands; "
          "a shifted shoreline can also make a large connected component.")


if __name__ == "__main__":
    main()
