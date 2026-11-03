import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np

from check_coast_cell_fan_spike import audit


class CellFanIncidenceTests(unittest.TestCase):
    def check(self, triangles):
        vertices = np.array([(*point, 0.0) for triangle in triangles for point in triangle],
                            dtype="<f4")
        with TemporaryDirectory() as directory:
            path = Path(directory) / "fan.bin"
            with path.open("wb") as stream:
                np.array([len(vertices)], dtype="<u4").tofile(stream)
                vertices.tofile(stream)
            return audit(path, interior_radius=0.2)

    def test_shared_diagonal_has_only_outer_boundary(self):
        a, b, c, d = (-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)
        result = self.check([(a, b, c), (a, c, d)])
        self.assertEqual(result["unpaired"], 4)
        self.assertEqual(result["interior_unpaired"], 0)

    def test_split_edge_against_unsplit_edge_is_detected(self):
        a, b, c, d = (-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)
        midpoint = (0, 0, 0)
        result = self.check([(a, b, c), (a, midpoint, d), (midpoint, c, d)])
        self.assertGreater(result["interior_unpaired"], 0)

    def test_distinct_float_edges_are_not_rounded_together(self):
        first = ((0.4, 0, 0), (0.5, 0, 0), (0.4, 0.1, 0))
        second = tuple((x + 0.00000006, y, z) for x, y, z in first)
        result = self.check([first, second])
        self.assertEqual(result["edges"], 6)
        self.assertEqual(result["nonmanifold"], 0)


if __name__ == "__main__":
    unittest.main()
