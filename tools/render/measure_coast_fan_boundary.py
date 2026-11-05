#!/usr/bin/env python3
"""Read open-cap coastline samples from the dual-cell fan geometry probe.

Usage: python3 tools/render/measure_coast_fan_boundary.py PREFIX-fan-boundary.csv
       ... --focus PREFIX-focus.txt --altitudes 0.7,0.15,0.04
Crossings are linear interpolations between 64 sampled intervals per edge,
not exact roots of the canonical relief. Filtered relief is NOT the actual
cell shader; coarse drawn heights only diagnose geometric hand-back.
Projected crossing locations use a fixed 512px nadir camera (60-degree
default, configurable FOV), not GlobeCamera's moving tilt or materials.
"""

import argparse
import csv
from collections import defaultdict
import math
from pathlib import Path


FIELDS = ("t", "detailed_m", "filtered_m", "coarse_drawn_m", "edge_length_m",
          "direction_x", "direction_y", "direction_z")


def read_samples(path: Path):
    edges = defaultdict(list)
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["edge", *FIELDS]:
            raise ValueError("unexpected boundary sample columns")
        for row in reader:
            edge = int(row["edge"])
            values = {key: float(row[key]) for key in FIELDS}
            if edge < 0 or not all(math.isfinite(value) for value in values.values()):
                raise ValueError("non-finite or invalid boundary sample")
            if abs(sum(values[f"direction_{axis}"] ** 2 for axis in "xyz") - 1) > 1e-6:
                raise ValueError("non-unit boundary direction")
            edges[edge].append(values)
    if not edges:
        raise ValueError("no boundary samples")
    for edge, samples in edges.items():
        if (len(samples) < 2 or samples[0]["t"] != 0.0 or
                samples[-1]["t"] != 1.0 or
                any(a["t"] >= b["t"] for a, b in zip(samples, samples[1:])) or
                samples[0]["edge_length_m"] <= 0 or
                any(s["edge_length_m"] != samples[0]["edge_length_m"] for s in samples)):
            raise ValueError(f"invalid samples on edge {edge}")
    return edges


def crossings(samples, field):
    roots = []
    for a, b in zip(samples, samples[1:]):
        first, last = a[field], b[field]
        if (first <= 0) != (last <= 0):
            roots.append(a["t"] + (b["t"] - a["t"]) * first / (first - last))
    return roots


def metrics(edges):
    result = dict(edges=len(edges), crossed_edges=0, detailed_crossings=0,
                   filtered_crossings=0, detailed_without_filtered_edge=0,
                   incompatible_sampled_edges=0,
                   filtered_class_misses=0, raised_height_proxy_misses=0,
                  max_matched_edge_gap_m=0.0, max_raised_height_gap_m=0.0)
    for samples in edges.values():
        result["incompatible_sampled_edges"] += any(
            (row["detailed_m"] <= 0) != (row["filtered_m"] <= 0)
            for row in samples)
        detailed = crossings(samples, "detailed_m")
        filtered = crossings(samples, "filtered_m")
        result["crossed_edges"] += bool(detailed)
        result["detailed_crossings"] += len(detailed)
        result["filtered_crossings"] += len(filtered)
        length = samples[0]["edge_length_m"]
        for root in detailed:
            if not filtered:
                result["detailed_without_filtered_edge"] += 1
            else:
                result["max_matched_edge_gap_m"] = max(
                    result["max_matched_edge_gap_m"],
                    min(abs(root - other) for other in filtered) * length)
        for row in samples:
            result["max_raised_height_gap_m"] = max(
                result["max_raised_height_gap_m"],
                abs(max(0.0, row["detailed_m"]) - row["coarse_drawn_m"]))
        for row in samples[1:-1]:
            wet = row["detailed_m"] <= 0
            result["filtered_class_misses"] += wet != (row["filtered_m"] <= 0)
            result["raised_height_proxy_misses"] += wet != (row["coarse_drawn_m"] <= 0)
    return result


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(vector):
    magnitude = math.sqrt(dot(vector, vector))
    if not magnitude or not math.isfinite(magnitude):
        raise ValueError("invalid direction")
    return tuple(value / magnitude for value in vector)


def direction_at(samples, t):
    for a, b in zip(samples, samples[1:]):
        if a["t"] <= t <= b["t"]:
            weight = (t - a["t"]) / (b["t"] - a["t"])
            return unit(tuple(a[f"direction_{axis}"] * (1 - weight) +
                              b[f"direction_{axis}"] * weight for axis in "xyz"))
    raise ValueError("crossing outside edge")


def project(direction, focus, altitude, size=512, fov_degrees=60):
    """Sea-level point in the fixed nadir camera; None if behind the globe."""
    focus = unit(focus)
    direction = unit(direction)
    if altitude <= 0:
        raise ValueError("camera altitude must be positive")
    forward_cos = dot(direction, focus)
    if forward_cos <= 1 / (1 + altitude):
        return None
    up = (0.0, 1.0, 0.0) if abs(focus[1]) < 0.9 else (0.0, 0.0, -1.0)
    north = unit(tuple(up[i] - focus[i] * dot(up, focus) for i in range(3)))
    east = (north[1] * focus[2] - north[2] * focus[1],
            north[2] * focus[0] - north[0] * focus[2],
            north[0] * focus[1] - north[1] * focus[0])
    focal = size / 2 / math.tan(math.radians(fov_degrees) / 2)
    depth = altitude + 1 - forward_cos
    return (size / 2 + focal * dot(direction, east) / depth,
            size / 2 - focal * dot(direction, north) / depth)


def projected_metrics(edges, focus, altitudes, size=512, margin=80, fov_degrees=60):
    results = {}
    for altitude in altitudes:
        result = dict(in_frame=0, central=0, unmatched_in_frame=0,
                      unmatched_central=0, max_matched_gap_px=0.0)
        for samples in edges.values():
            detailed = crossings(samples, "detailed_m")
            filtered = crossings(samples, "filtered_m")
            for root in detailed:
                point = project(direction_at(samples, root), focus, altitude, size,
                                fov_degrees)
                if point is None or not all(0 <= value < size for value in point):
                    continue
                result["in_frame"] += 1
                central = all(margin <= value < size - margin for value in point)
                result["central"] += central
                if not filtered:
                    result["unmatched_in_frame"] += 1
                    result["unmatched_central"] += central
                else:
                    neighbours = [project(direction_at(samples, other), focus,
                                          altitude, size, fov_degrees) for other in filtered]
                    neighbours = [candidate for candidate in neighbours if candidate is not None]
                    if neighbours:
                        result["max_matched_gap_px"] = max(
                            result["max_matched_gap_px"],
                            min(math.dist(point, other) for other in neighbours))
        results[altitude] = result
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--focus", type=Path, help="probe focus.txt for projected diagnostics")
    parser.add_argument("--altitudes", default="0.7,0.15,0.04")
    parser.add_argument("--fov", type=float, default=60.0,
                        help="fixed nadir vertical FOV in degrees (default: patch probe's 60)")
    args = parser.parse_args()
    if not 0 < args.fov < 180:
        parser.error("fov must be between 0 and 180 degrees")
    edges = read_samples(args.csv)
    print(metrics(edges))
    if args.focus:
        focus = tuple(float(value) for value in args.focus.read_text().split())
        if len(focus) != 3:
            parser.error("focus must contain three coordinates")
        altitudes = [float(value) for value in args.altitudes.split(",")]
        for altitude, result in projected_metrics(edges, focus, altitudes,
                                                  fov_degrees=args.fov).items():
            print(f"altitude {altitude:.2f}: {result}")


if __name__ == "__main__":
    main()
