"""Exact-energy and geometric-error contracts for the offline seam experiment."""
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/diagnostics/curvature'))
import evaluate_part_seams as seam


class SeamOracleTests(unittest.TestCase):
    def graph(self, hard=0, confidence=0.):
        return seam.assemble([[0,0,0],[1,0,0],[0,1,0],[1,1,0]],[[0,1,2],[1,3,2]],[(1,2,hard,confidence)])

    def test_exact_two_triangle_energy_and_region_penalty(self):
        graph = self.graph(confidence=.75)
        self.assertAlmostEqual(seam.evaluate(graph,[0,0])['energy'],seam.PROFILE['region_cost'])
        expected = .04*(1-4*.75**3)+2*seam.PROFILE['region_cost']
        self.assertAlmostEqual(seam.evaluate(graph,[0,1])['energy'],expected,places=15)

    def test_hard_cut_cannot_be_bought_off(self):
        graph = self.graph(hard=1,confidence=1.)
        self.assertFalse(seam.evaluate(graph,[0,0])['feasible'])
        self.assertIsNone(seam.evaluate(graph,[0,0])['energy'])
        self.assertTrue(seam.evaluate(graph,[0,1])['feasible'])
        self.assertAlmostEqual(graph['edges'][0]['cost'],-.12)

    def test_disconnected_same_label_counts_each_region(self):
        self.assertEqual(seam.components([7,8,7],[[(1,0)],[(0,0),(2,1)],[(1,1)]]),[0,1,2])

    def test_invalid_labels_and_incomplete_binding_fail_closed(self):
        for labels in ([0],[True,0],[-1,0],[0.,1.]):
            with self.assertRaises(ValueError):
                seam.evaluate(self.graph(),labels)
        with self.assertRaisesRegex(ValueError,'hash bindings'):
            seam.bound_run({}, {'output_sha256':{'.geometry.json':'x'}})

    def test_bad_confidence_and_wrong_profile_are_rejected(self):
        for confidence in (math.nan,math.inf,-.1,1.1):
            with self.assertRaises(ValueError):
                self.graph(confidence=confidence)
        with self.assertRaises(ValueError):
            seam.assemble([[0,0,0],[1,0,0],[0,1,0]],[[0,1,2]],[],dict(seam.PROFILE,feature_weight=3.))

    def test_duplicate_and_missing_evidence_are_rejected(self):
        vertices,faces = [[0,0,0],[1,0,0],[0,1,0],[1,1,0]],[[0,1,2],[1,3,2]]
        for evidence in ([],[(1,2,0,.1),(2,1,0,.1)]):
            with self.assertRaises(ValueError):
                seam.assemble(vertices,faces,evidence)

    def test_curve_union_hausdorff_bounds(self):
        a = [[[0,0,0],[1,0,0]]]
        b = [[[0,2,0],[1,2,0]]]
        result = seam.curve_distance_bounds(a,b,1.)
        self.assertEqual(result['lower'],2.)
        self.assertEqual(result['upper'],2.25)
        self.assertIsNone(seam.curve_distance_bounds(a,[],1.))

    def test_planar_contour_and_edge_staircase_are_distinct(self):
        graph=self.graph()
        plane={'normal':[1,0,0],'offset':.5}
        labels,largest=seam.plane_candidate(graph,[0,0],[plane])
        result=seam.approximation(graph,[0,0],labels,largest,plane)
        self.assertAlmostEqual(result['length_ratio'],math.sqrt(2.))
        self.assertAlmostEqual(result['midpoint_plane_distance_max_over_D'],0.)
        self.assertGreater(result['surface_curve_hausdorff_bounds_over_D']['lower'],.3)

    def test_json_rejects_duplicate_keys_and_nonfinite(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'bad.json'
            for source in ('{"x":1,"x":2}','{"x":NaN}'):
                path.write_text(source)
                with self.assertRaises(ValueError):
                    seam.read_json(path)


if __name__ == '__main__':
    unittest.main()
