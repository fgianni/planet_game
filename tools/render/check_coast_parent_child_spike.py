"""Reproducible geometry-only ADR-0030 probe checks.

Usage: python3 tools/render/check_coast_parent_child_spike.py /tmp/opencode/coast_parent_child_spike [--all-faces|--coastal-faces]
The C++ exporter must be built separately against the presentation/core archives.
"""
import re
import subprocess
import sys


def run(executable, mode, seed, level, face, x, y):
    output = subprocess.check_output(
        [executable, str(level), "/tmp/opencode/coast-audit", mode, "audit",
         str(seed), str(face), str(x), str(y)], text=True)
    assert "66564 shared samples, 0 class flips, max height/position difference 0 / 0 m" in output
    def count(label):
        match = re.search(rf"{re.escape(label)} (\d+)", output)
        assert match, output
        return int(match.group(1))
    vertices = re.search(r"vertex counts (\d+)/(\d+)/(\d+)", output)
    assert vertices, output
    segments = re.search(r"clipped child/outer segments (\d+)/(\d+) mismatches (\d+)/(\d+)", output)
    assert segments and int(segments[1]) > 0 and int(segments[2]) > 0, output
    neighbour = re.search(r"same-face clipped segments (\d+) mismatches (\d+)", output)
    assert neighbour, output
    face = re.search(r"cube-face edges/crossings/segments (\d+)/(\d+)/(\d+) mismatches (\d+) max float-position gap (\S+) m", output)
    assert face, output
    mixed = re.search(r"mixed-face clipped segments (\d+) mismatches (\d+) max float-position gap (\S+) m", output)
    assert mixed, output
    if mode == "raised-sparse":
        assert int(face[4]) == 0 and float(face[5]) == 0, output
        assert int(mixed[2]) == 0 and float(mixed[3]) == 0, output
        assert int(mixed[1]) == int(face[3]), output
    assert count("boundary coverage errors") == 0, output
    return (count("child seam unmatched vertices"),
            count("parent/child outer edge unmatched vertices"),
            count("same-face neighbour unmatched vertices"),
            sum(map(int, vertices.groups()[:2])),
            (int(segments[3]), int(segments[4])),
            (int(neighbour[1]), int(neighbour[2])),
            (int(face[1]), int(face[2]), int(face[3])),
            int(mixed[1]))


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and
                                     sys.argv[2] not in ("--all-faces", "--coastal-faces")):
        raise SystemExit(__doc__)
    executable = sys.argv[1]
    cases = [
        (20260928, 4, 0, 7, 0, 9),
        (20260928, 6, 0, 16, 0, 19),
        (42, 4, 4, 0, 0, 46),
        (42, 4, 0, 8, 8, 0),  # interior tile without sampled coast
        (42, 4, 5, 15, 15, 0),  # face corner
    ]
    for seed, level, face, x, y, expected_seam in cases:
        args = (seed, level, face, x, y)
        old = run(executable, "raised", *args)
        sparse = run(executable, "raised-sparse", *args)
        full = run(executable, "raised-boundary", *args)
        assert old[0] == expected_seam, (args, old)
        assert sparse[:3] == (0, 0, 0), (args, sparse)
        assert full[:2] == (0, 0), (args, full)
        assert old[3] <= sparse[3] <= full[3], (args, old, sparse, full)
        assert sparse[4] == full[4] == (0, 0), (args, sparse, full)
        assert sparse[5][1] == 0, (args, sparse)
        if expected_seam:
            assert old[4][0] > 0 and old[4][1] > 0, (args, old)
        print(f"{args}: unmatched child/outer {old[:2]} -> {sparse[:2]}; "
              f"clipped segments {old[4]} -> {sparse[4]}; "
              f"same/mixed cube-face segments {sparse[6][2]}/{sparse[7]} "
              f"with {sparse[6][1]} crossings; "
              f"vertices {old[3]} -> {sparse[3]} (all-edge {full[3]})")
    if len(sys.argv) == 3:
        edges = crossings = 0
        coastal_edges = 0
        indices = (0, 3, 7, 11, 15) if sys.argv[2] == "--coastal-faces" else (8,)
        for face in range(6):
            for edge in range(4):
                edge_crossings = 0
                for i in indices:
                    x, y = ((i, 0), (15, i), (i, 15), (0, i))[edge]
                    result = run(executable, "raised-sparse", 42, 4, face, x, y)
                    assert result[:3] == (0, 0, 0) and result[4] == (0, 0)
                    # Corners also touch the perpendicular face edge.
                    assert result[6][0] in (1, 2) and result[7] == result[6][2]
                    edges += result[6][0]
                    crossings += result[6][1]
                    edge_crossings += result[6][1]
                coastal_edges += edge_crossings > 0
        if sys.argv[2] == "--coastal-faces":
            assert coastal_edges >= 20 and crossings >= 100, (coastal_edges, crossings)
            print(f"120 face-edge tile cases (including repeated corner checks): "
                  f"{edges} oriented comparisons, {crossings} crossings, "
                  f"{coastal_edges}/24 edge sweeps contain a crossing; "
                  "zero same/mixed clipped-interval or float-endpoint mismatches")
        else:
            assert edges == 24 and crossings == 2, (edges, crossings)
            print(f"all {edges} oriented face-edge midpoint tiles: {crossings} sampled shoreline crossings, "
                  "zero clipped-interval or float-endpoint mismatches")


if __name__ == "__main__":
    main()
