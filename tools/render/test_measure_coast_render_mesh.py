import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np
from PIL import Image
from measure_coast_render_mesh import (cell_patch_metrics, compare,
                                       face_pair_metrics, fan_patch_metrics, read)


class MeasureCoastRenderMeshTest(unittest.TestCase):
    def test_equal_masks(self):
        mask = np.zeros((16, 16), dtype=bool)
        mask[:, :8] = True
        self.assertEqual(compare(mask, mask, np.ones_like(mask))["pixels"], 0)

    def test_detects_a_resolvable_feature_miss(self):
        detail = np.zeros((16, 16), dtype=bool)
        detail[4:8, 4:8] = True
        parent = np.zeros_like(detail)
        result = compare(parent, detail, np.ones_like(detail))
        self.assertEqual(result["pixels"], 16)
        self.assertEqual(result["components_at_least_four"], 1)
        self.assertEqual(result["max_component_px"], 16)

    def test_face_pair_reports_missing_footprint_separately(self):
        visible = np.ones((16, 16), dtype=bool)
        incomplete = visible.copy()
        incomplete[:, :4] = False
        coast = visible.copy()
        result = face_pair_metrics(coast, visible, coast, incomplete)
        self.assertEqual(result["visible_xor"], 64)
        self.assertEqual(result["water"]["pixels"], 0)

    def test_actual_cell_crop_and_colour_heuristic(self):
        # Map-style water and the unshaded patch water have distinct RGBs;
        # this only tests the heuristic, not equivalence of the materials.
        patch = np.zeros((24, 24, 3), dtype=np.uint8)
        patch[5:19, 5:19] = (242, 76, 6)
        patch[9:15, 9:15] = (6, 63, 242)
        cell = np.empty_like(patch)
        cell[:] = (136, 153, 62)
        cell[9:15, 9:15] = (37, 77, 107)
        cell[10:14, 10:14] = (136, 153, 62)
        cell[:3, :] = (37, 77, 107)  # Outside the patch; never compare.
        with TemporaryDirectory() as directory:
            patch_path = Path(directory) / "patch.png"
            cell_path = Path(directory) / "cell.png"
            Image.fromarray(patch).save(patch_path)
            Image.fromarray(cell).save(cell_path)
            pw, pv = read(patch_path)
            cw, cv = read(cell_path)
        result = cell_patch_metrics(cw, cv, pw, pv, pv)
        self.assertTrue(result["patch_center_visible"])
        self.assertEqual(result["common_interior"], 100)
        self.assertEqual(result["water"]["pixels"], 16)
        self.assertEqual(result["water"]["components_at_least_four"], 1)
        self.assertEqual(result["cell_water"], 20)
        self.assertEqual(result["patch_water"], 36)

    def test_fan_coverage_not_confused_with_water_mismatch(self):
        patch_visible = np.zeros((24, 24), dtype=bool)
        patch_visible[5:19, 5:19] = True
        fan_visible = patch_visible.copy()
        fan_visible[0:3, :] = True
        fan_visible[5:8, 5:19] = False
        patch = np.zeros((24, 24), dtype=bool)
        fan = patch.copy()
        result = fan_patch_metrics(fan, fan_visible, patch, patch_visible,
                                   patch_visible)
        self.assertEqual(result["patch_missing"], 42)
        self.assertEqual(result["fan_outside_patch"], 72)
        self.assertEqual(result["water"]["pixels"], 0)


if __name__ == "__main__":
    unittest.main()
