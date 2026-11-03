"""Small mask-geometry checks; no Godot or image fixtures required."""

import unittest

from measure_coast_handoff import components, shore_distance
from measure_coast_zoom import metrics


class CoastMaskTests(unittest.TestCase):
    def test_identical_masks_have_no_changes(self):
        valid = {(x, y) for x in range(10) for y in range(10)}
        water = {(x, y) for x, y in valid if x < 5}
        distances = shore_distance(valid, water)
        self.assertEqual(distances[(1, 4)], 3)
        self.assertEqual(distances[(4, 4)], 0)
        self.assertEqual(distances[(5, 4)], 0)

    def test_shifted_coast_is_not_disappearing_island(self):
        valid = {(x, y) for x in range(12) for y in range(12)}
        cell = {(x, y) for x, y in valid if x < 4}
        tile = {(x, y) for x, y in valid if x < 9}
        changed = cell ^ tile
        deep = {p for p in changed if shore_distance(valid, cell).get(p, 0) > 1
                and shore_distance(valid, tile).get(p, 0) > 1}
        self.assertEqual(len(changed), 60)
        self.assertTrue(deep)
        self.assertEqual(components(changed), [60])

    def test_components_are_eight_connected(self):
        self.assertEqual(components({(0, 0), (1, 1), (8, 8)}), [2, 1])

    def test_zoom_metrics_keep_blend_and_endpoints_separate(self):
        valid = {(x, y) for x in range(12) for y in range(12)}
        cell = {p for p in valid if p[0] < 4}
        tile = {p for p in valid if p[0] < 8}
        result = metrics(valid, cell, valid, tile, valid, cell)
        self.assertEqual(result["endpoint_changes"], 48)
        self.assertEqual(result["deep"], 0)
        self.assertEqual(result["blend_vs_cell"], 0)
        self.assertEqual(result["blend_vs_tile"], 48)

    def test_zoom_missing_island_has_no_cell_shore(self):
        valid = {(x, y) for x in range(16) for y in range(16)}
        island = {(x, y) for x in range(4, 12) for y in range(4, 12)}
        result = metrics(valid, set(), valid, island, valid, island)
        self.assertEqual(result["endpoint_changes"], 64)
        self.assertEqual(result["cell_shore_pixels"], 0)
        self.assertGreater(result["deep"], 0)


if __name__ == "__main__":
    unittest.main()
