#!/usr/bin/env python3
"""Independent cut oracle, known-crease relocation and preservation controls."""
import itertools
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tools/diagnostics/atlas'))
import boundary_refine as br
import patch_merge as pm


def folded_sheet(n=16, rows=8):
    v = np.array([(x, y, 0.) for y in range(rows+1) for x in range(n+1)])
    fold = v[:, 0] > 9
    offset = v[fold, 0]-9
    v[fold, 0] = 9+offset/np.sqrt(2); v[fold, 2] = offset/np.sqrt(2)
    faces = []
    for y in range(rows):
        for x in range(n):
            a = y*(n+1)+x
            faces.extend(((a, a+1, a+n+2), (a, a+n+2, a+n+1)))
    f = np.array(faces); labels = np.array([int((face[0] % (n+1)) >= 7) for face in f])
    g = pm.geometry(v, f)
    score = np.array([float(a % (n+1) == 9 and b % (n+1) == 9) for *_, a, b in g['edges']])
    return v, f, labels, score


class BoundaryRefineTests(unittest.TestCase):
    def test_binary_cut_matches_exhaustive_energy(self):
        rng = np.random.default_rng(11)
        for _ in range(12):
            edges = [(i, j, float(rng.integers(1, 25))) for i in range(7) for j in range(i+1, 7)]
            fixed = {0: 0, 6: 1}
            def energy(labels):
                return sum(w for i, j, w in edges if labels[i] != labels[j])
            optimum = min(energy((0, *inner, 1)) for inner in itertools.product((0, 1), repeat=5))
            actual = br.binary_cut(7, edges, fixed)
            self.assertEqual(energy(actual), optimum)

    def test_integer_rounding_error_is_bounded(self):
        edges = [(i, j, float(10.**((i*7+j) % 18-9)))
                 for i in range(7) for j in range(i+1, 7)]
        def energy(labels):
            return sum(w for i, j, w in edges if labels[i] != labels[j])
        best = min(energy((0, *inner, 1)) for inner in itertools.product((0, 1), repeat=5))
        actual = energy(br.binary_cut(7, edges, {0: 0, 6: 1}))
        bound = 2*len(edges)*sum(w for _, _, w in edges)/10_000_000
        self.assertLessEqual(actual-best, bound)

    def test_native_evidence_validation(self):
        v, f, _, _ = folded_sheet(); g = pm.geometry(v, f)
        rows = np.array([[a, b, 0, .3, 0] for *_, a, b in g['edges']])
        hard, soft = br.edge_evidence(g, rows)
        self.assertFalse(hard.any()); np.testing.assert_allclose(soft, .3)
        with self.assertRaisesRegex(ValueError, 'missing'):
            br.edge_evidence(g, rows[:-1])
        rows[0, 3] = 1.1
        with self.assertRaisesRegex(ValueError, 'confidence'):
            br.edge_evidence(g, rows)

    def test_feature_guidance_reaches_known_crease(self):
        v, f, labels, score = folded_sheet(); original = labels.copy()
        regions = np.zeros(len(f), int)
        plain = br.execute(v, f, regions, labels, score, beta=0)
        guided = br.execute(v, f, regions, labels, score)
        np.testing.assert_array_equal(plain['labels'], labels)
        g = pm.geometry(v, f)
        border = np.array([guided['labels'][a] != guided['labels'][b] for a, b, *_ in g['edges']])
        np.testing.assert_array_equal(border, score.astype(bool))
        self.assertGreater(guided['metrics']['accepted_moves'], 0)
        self.assertLess(guided['metrics']['max_stretch'], 1.000001)
        self.assertTrue(all(e['energy_after'] < e['energy_before'] for e in guided['events'] if e['status'] == 'accepted'))
        np.testing.assert_array_equal(labels, original)

    def test_original_regions_and_hard_seams_fixed(self):
        v, f, labels, score = folded_sheet(); g = pm.geometry(v, f)
        hard = np.array([labels[a] != labels[b] for a, b, *_ in g['edges']])
        for regions, flags in ((labels.copy(), None), (np.zeros(len(f), int), hard)):
            result = br.execute(v, f, regions, labels, score, hard=flags)
            np.testing.assert_array_equal(result['labels'], labels)
            self.assertEqual(result['metrics']['lost_baseline_edges'], 0)

    def test_frozen_band_and_missing_core(self):
        v, f, labels, score = folded_sheet()
        result = br.execute(v, f, np.zeros(len(f), int), labels, score, band_hops=100)
        np.testing.assert_array_equal(result['labels'], labels)
        self.assertEqual(result['metrics']['statuses']['no_mutable_faces'], 1)
        result = br.execute(v, f, np.zeros(len(f), int), labels, score, band_hops=0, rounds=10)
        np.testing.assert_array_equal(result['labels'], labels)

    def test_uv_rejection_keeps_source_atlas(self):
        v, f, labels, score = folded_sheet(); solve = br.solve_chart; calls = 0
        def reject_proposal(*args):
            nonlocal calls
            calls += 1
            return solve(*args) if calls <= 2 else (None, 'distortion')
        with patch.object(br, 'solve_chart', reject_proposal):
            result = br.execute(v, f, np.zeros(len(f), int), labels, score)
        np.testing.assert_array_equal(result['labels'], labels)
        self.assertEqual(result['metrics']['statuses']['distortion'], 1)

    def test_internal_seams_increase_can_reject_border_descent(self):
        v, f, labels, score = folded_sheet(); solve = br.solve_chart; calls = 0
        # Force an admissible candidate to report expensive new internal cuts.
        # The acceptance guard must consider these even when border energy drops.
        def extra_cuts(g, *args):
            nonlocal calls
            calls += 1
            chart, reason = solve(g, *args)
            if calls > 2 and chart is not None:
                chart['internal_edges'] = set(range(len(g['edges'])))
            return chart, reason
        with patch.object(br, 'solve_chart', extra_cuts):
            result = br.execute(v, f, np.zeros(len(f), int), labels, score)
        np.testing.assert_array_equal(result['labels'], labels)
        self.assertEqual(result['metrics']['statuses']['internal_cut_energy'], 1)

    def test_curve_support_does_not_jump_disconnected_sheets(self):
        v = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0.]], float)
        f = np.array([[0, 1, 2], [1, 3, 2]])
        v = np.vstack((v, v+[0, 0, .001])); f = np.vstack((f, f+4))
        g = pm.geometry(v, f)
        curves = dict(positions=v.tolist(), triangles=f.tolist(), scales=[dict(radius=2.)],
                      points=[dict(position=[.8, .1, 0]), dict(position=[.1, .8, 0])],
                      segments=[dict(scale=0, kind='principal_ridge', confidence=.9, face=0, points=[0, 1])])
        score = br.curve_support(g, v, curves, 0)
        self.assertAlmostEqual(score[0], .9*(1-(.1/np.sqrt(2))/2))
        self.assertEqual(score[1], 0)
        with self.assertRaisesRegex(ValueError, 'scale'):
            br.curve_support(g, v, curves, 2)
        curves['scales'][0]['radius'] = .03
        np.testing.assert_array_equal(br.curve_support(g, v, curves, 0), [0., 0.])
        curves['scales'][0]['radius'] = 2.
        curves['points'] = [dict(position=[.1, .1, 0]), dict(position=[.8, .8, 0])]
        np.testing.assert_allclose(br.curve_support(g, v, curves, 0), [0., 0.], atol=1e-15)
        curves['positions'][0][0] = .01
        with self.assertRaisesRegex(ValueError, 'identity'):
            br.curve_support(g, v, curves, 0)

    def test_global_core_survives_overlapping_pair_bands(self):
        v, f, _, score = folded_sheet(n=20)
        x = f[:, 0] % 21
        labels = np.where(x < 6, 0, np.where(x < 11, 1, 2))
        g = pm.geometry(v, f); regions = np.zeros(len(f), int)
        # The thin middle chart has no deep core: it is frozen as a whole,
        # even though it participates in two independently optimized pairs.
        middle = set(np.flatnonzero(labels == 1))
        for _, mutable in br.frozen_bands(g, labels, regions, np.zeros(len(score), bool), 6):
            self.assertFalse(middle & mutable)
        result = br.execute(v, f, regions, labels, score)
        for stage in result['stages']:
            np.testing.assert_array_equal(stage[list(middle)], labels[list(middle)])
            self.assertEqual(len(np.unique(stage)), 3)

    def test_internal_cut_mask_matches_independent_corner_audit(self):
        n = 12; rows = 4
        v = np.array([(np.cos(a), np.sin(a), y/rows)
                      for y in range(rows+1) for a in np.arange(n)*2*np.pi/n])
        f = []
        for y in range(rows):
            for i in range(n):
                a = y*n+i; b = y*n+(i+1)%n
                f.extend(((a, b, b+n), (a, b+n, a+n)))
        f = np.array(f); labels = np.zeros(len(f), int); g = pm.geometry(v, f)
        result = br.execute(v, f, labels, labels, np.zeros(len(g['edges'])), rounds=0)
        self.assertEqual(result['metrics']['internal_uv_cut_edges'], rows)
        self.assertEqual(int(result['final_seams'].sum()), rows)
        lengths = np.array([e[2] for e in g['edges']])
        self.assertAlmostEqual(float(lengths@result['final_seams']),
                               result['metrics']['total_uv_seam_length_over_sqrt_area'])

    def test_repeat_and_invalid_inputs(self):
        v, f, labels, score = folded_sheet(); regions = np.zeros(len(f), int)
        a = br.execute(v, f, regions, labels, score); b = br.execute(v, f, regions, labels, score)
        np.testing.assert_array_equal(a['labels'], b['labels'])
        np.testing.assert_array_equal(a['corner_uv'], b['corner_uv'])
        for kwargs in (dict(beta=1), dict(rounds=-1), dict(band_hops=.5), dict(stretch_limit=float('nan'))):
            with self.assertRaises(ValueError):
                br.execute(v, f, regions, labels, score, **kwargs)
        with self.assertRaises(ValueError):
            br.execute(v, f, regions, labels, score+1)


if __name__ == '__main__':
    unittest.main()
