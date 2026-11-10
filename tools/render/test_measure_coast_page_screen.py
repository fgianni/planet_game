import unittest

import numpy as np

from measure_coast_page_screen import metric


class PageScreenMetricTests(unittest.TestCase):
    def test_missing_four_pixel_island_is_not_just_a_change_blob(self):
        visible = np.ones((32, 32), dtype=bool)
        reference = visible.copy()
        reference[14:17, 14:17] = False
        page = visible.copy()
        result = metric(visible, reference, page)
        self.assertEqual(result["reference_land_components_missing_ge4"], 1)
        self.assertEqual(result["reference_land_components_ge4"], 1)
        self.assertEqual(result["page_land_components_missing_ge4"], 0)
        self.assertEqual(result["changes"], 9)

    def test_partial_island_overlap_is_not_disappearance(self):
        visible = np.ones((32, 32), dtype=bool)
        reference = visible.copy()
        reference[14:17, 14:17] = False
        page = visible.copy()
        page[15:18, 14:17] = False
        result = metric(visible, reference, page)
        self.assertEqual(result["reference_land_components_missing_ge4"], 0)
        self.assertEqual(result["changes"], 6)

    def test_dimensions(self):
        a = np.zeros((10, 10), dtype=bool)
        with self.assertRaisesRegex(ValueError, "dimensions differ"):
            metric(a, a, np.zeros((9, 9), dtype=bool))

    def test_new_small_shore_requires_both_distance_directions(self):
        visible = np.ones((32, 32), dtype=bool)
        coarse = np.zeros((32, 32), dtype=bool)
        coarse[:, :4] = True
        finer = coarse.copy()
        finer[16, 16] = True
        result = metric(visible, coarse, finer)
        self.assertGreater(result["max_from_reference_shore_px"], 1)
        self.assertEqual(result["beyond_one_either_px"], 1)


if __name__ == "__main__":
    unittest.main()
