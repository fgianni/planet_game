#!/usr/bin/env python3
"""Read open-cap coastline samples from the dual-cell fan geometry probe.

Usage: python3 tools/render/measure_coast_fan_boundary.py PREFIX-fan-boundary.csv
Crossings are linear interpolations between 64 sampled intervals per edge,
not exact roots of the canonical relief. Filtered relief is NOT the actual
cell shader; coarse drawn heights only diagnose geometric hand-back.
"""

import argparse
import csv
from collections import defaultdict
import math
from pathlib import Path


FIELDS = ("t", "detailed_m", "filtered_m", "coarse_drawn_m", "edge_length_m")


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
                  filtered_class_misses=0, raised_height_proxy_misses=0,
                  max_matched_edge_gap_m=0.0, max_raised_height_gap_m=0.0)
    for samples in edges.values():
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    print(metrics(read_samples(args.csv)))


if __name__ == "__main__":
    main()
