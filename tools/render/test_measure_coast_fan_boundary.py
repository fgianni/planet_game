import unittest
import math
from pathlib import Path
from tempfile import TemporaryDirectory

from measure_coast_fan_boundary import (crossings, metrics, project,
                                        projected_metrics, read_samples)


def sample(t, detailed, filtered, coarse=0.0, length=100.0, direction=(0, 0, 1)):
    return dict(t=t, detailed_m=detailed, filtered_m=filtered,
                coarse_drawn_m=coarse, edge_length_m=length,
                **dict(zip(("direction_x", "direction_y", "direction_z"), direction)))


class BoundarySamplesTests(unittest.TestCase):
    def test_missing_counterpart_and_shifted_crossing(self):
        edges = {
            0: [sample(0, 2, 2, 1), sample(0.5, -2, 2, 1), sample(1, 2, 2, 1)],
            1: [sample(0, 2, 2), sample(0.5, -2, 2), sample(1, -2, -2)],
        }
        result = metrics(edges)
        self.assertEqual(result["crossed_edges"], 2)
        self.assertEqual(result["detailed_crossings"], 3)
        self.assertEqual(result["filtered_crossings"], 1)
        self.assertEqual(result["detailed_without_filtered_edge"], 2)
        self.assertEqual(result["incompatible_sampled_edges"], 2)
        self.assertAlmostEqual(result["max_matched_edge_gap_m"], 50.0)
        self.assertEqual(result["raised_height_proxy_misses"], 1)
        self.assertEqual(crossings(edges[0], "filtered_m"), [])

    def test_reader_rejects_unordered_samples(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "boundary.csv"
            path.write_text("edge,t,detailed_m,filtered_m,coarse_drawn_m,edge_length_m,"
                            "direction_x,direction_y,direction_z\n"
                            "0,0,1,1,0,100,0,0,1\n0,1,0,0,0,100,0,0,1\n"
                            "0,0.5,-1,-1,0,100,0,0,1\n")
            with self.assertRaisesRegex(ValueError, "invalid samples"):
                read_samples(path)

    def test_projected_unmatched_crossing_leaves_close_pose_frame(self):
        def direction(x):
            return x, 0.0, math.sqrt(1 - x * x)
        edges = {0: [sample(0, 1, 1, direction=direction(0.09)),
                     sample(1, -1, 1, direction=direction(0.11))]}
        self.assertEqual(project((0, 0, 1), (0, 0, 1), 0.7), (256, 256))
        results = projected_metrics(edges, (0, 0, 1), [0.7, 0.04])
        self.assertEqual(results[0.7]["unmatched_central"], 1)
        self.assertEqual(results[0.04]["in_frame"], 0)

    def test_matched_crossing_projection_reports_pixel_gap(self):
        def direction(x):
            return x, 0.0, math.sqrt(1 - x * x)
        edges = {0: [sample(0, 1, 3, direction=direction(0.08)),
                     sample(1, -1, -1, direction=direction(0.12))]}
        result = projected_metrics(edges, (0, 0, 1), [0.7])[0.7]
        self.assertEqual(result["unmatched_in_frame"], 0)
        self.assertGreater(result["max_matched_gap_px"], 1)

    def test_matched_crossings_can_still_be_incompatible(self):
        edges = {0: [sample(0, 1, 1), sample(0.5, -1, 1),
                     sample(1, -1, -1)]}
        result = metrics(edges)
        self.assertEqual(result["detailed_without_filtered_edge"], 0)
        self.assertEqual(result["incompatible_sampled_edges"], 1)
        self.assertEqual(result["filtered_class_misses"], 1)


if __name__ == "__main__":
    unittest.main()
