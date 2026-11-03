import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from measure_coast_fan_boundary import crossings, metrics, read_samples


def sample(t, detailed, filtered, coarse=0.0, length=100.0):
    return dict(t=t, detailed_m=detailed, filtered_m=filtered,
                coarse_drawn_m=coarse, edge_length_m=length)


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
        self.assertAlmostEqual(result["max_matched_edge_gap_m"], 50.0)
        self.assertEqual(result["raised_height_proxy_misses"], 1)
        self.assertEqual(crossings(edges[0], "filtered_m"), [])

    def test_reader_rejects_unordered_samples(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "boundary.csv"
            path.write_text("edge,t,detailed_m,filtered_m,coarse_drawn_m,edge_length_m\n"
                            "0,0,1,1,0,100\n0,1,0,0,0,100\n0,0.5,-1,-1,0,100\n")
            with self.assertRaisesRegex(ValueError, "invalid samples"):
                read_samples(path)


if __name__ == "__main__":
    unittest.main()
