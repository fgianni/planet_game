"""Small mask-geometry checks; no Godot or image fixtures required."""

import unittest

from measure_coast_handoff import components, shore_distance


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


if __name__ == "__main__":
    unittest.main()
