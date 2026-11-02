import unittest

import numpy as np
from measure_coast_render_mesh import compare, face_pair_metrics


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


if __name__ == "__main__":
    unittest.main()
