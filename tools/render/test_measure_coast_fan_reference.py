import unittest

import numpy as np

from measure_coast_fan_reference import metrics


class CanonicalReferenceTests(unittest.TestCase):
    def test_missing_island_in_common_interior(self):
        visible = np.zeros((24, 24), dtype=bool)
        visible[4:20, 4:20] = True
        reference = np.zeros((24, 24), dtype=bool)
        reference[9:15, 9:15] = True
        drawn = np.zeros_like(reference)
        result = metrics(drawn, visible, reference, np.ones_like(visible))
        self.assertEqual(result["common_interior"], 144)
        self.assertEqual(result["water"]["pixels"], 36)
        self.assertEqual(result["water"]["components_at_least_four"], 1)
        self.assertGreater(result["water"]["beyond_one_px"], 0)

    def test_different_image_size_is_rejected(self):
        a = np.zeros((8, 8), dtype=bool)
        b = np.zeros((9, 9), dtype=bool)
        with self.assertRaisesRegex(ValueError, "dimensions differ"):
            metrics(a, a, b, b)


if __name__ == "__main__":
    unittest.main()
