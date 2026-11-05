#!/usr/bin/env python3
"""Audit triangle-list incidence inside a probe-only dual-fan coastal cap.

Usage: python3 tools/render/check_coast_cell_fan_spike.py /tmp/opencode/coast-planet-surrogate-L4-fan.bin
Unpaired edges near the outer cap are expected: no production-fan hand-back
exists yet. Exact exported float32 positions diagnose T-junctions, not GPU
cracks, correct normals, shoreline fidelity, or a completed ADR-0030 gate.
"""

import argparse
from collections import Counter
from pathlib import Path

import numpy as np


def audit(path: Path, interior_radius: float = 0.10):
    count = np.fromfile(path, dtype="<u4", count=1)
    if len(count) != 1 or count[0] == 0 or count[0] % 3:
        raise ValueError("invalid triangle-list vertex count")
    vertices = np.fromfile(path, dtype="<f4", offset=4)
    if vertices.size != int(count[0]) * 4 or not np.isfinite(vertices).all():
        raise ValueError("invalid triangle-list payload")
    vertices = vertices.reshape(-1, 4)
    if not np.isin(vertices[:, 3], (0.0, 1.0)).all():
        raise ValueError("invalid wet/dry class")
    # Do not quantize: L6 has distinct sub-metre coastline edges that a
    # 0.6 m position grid merges into spurious non-manifold incidences.
    positions = vertices[:, :3].view("<u4").reshape(-1, 3, 3)
    degenerate = sum(len({tuple(point) for point in triangle}) < 3
                     for triangle in positions)
    edges = Counter()
    for start, end in ((0, 1), (1, 2), (2, 0)):
        for a, b in zip(positions[:, start], positions[:, end]):
            key = tuple(sorted((tuple(a), tuple(b))))
            if key[0] != key[1]:
                edges[key] += 1
    unpaired = [edge for edge, incidence in edges.items() if incidence == 1]
    interior_unpaired = sum(np.linalg.norm((np.array(a, dtype="<u4").view("<f4") +
                                           np.array(b, dtype="<u4").view("<f4")) / 2)
                            < interior_radius for a, b in unpaired)
    return dict(vertices=int(count[0]), edges=len(edges),
                unpaired=len(unpaired), interior_unpaired=int(interior_unpaired),
                nonmanifold=sum(value > 2 for value in edges.values()),
                degenerate=int(degenerate))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesh", type=Path)
    parser.add_argument("--interior-radius", type=float, default=0.10)
    args = parser.parse_args()
    result = audit(args.mesh, args.interior_radius)
    print(result)
    if result["interior_unpaired"] or result["nonmanifold"] or result["degenerate"]:
        parser.exit(1, "interior open, non-manifold or degenerate triangle edges\n")


if __name__ == "__main__":
    main()
