"""Small deterministic contracts; the external frog is never a CI fixture."""
import copy
import math
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/diagnostics/curvature'))
import neck_sweep as neck


class NeckSweepTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config=neck.read_json(ROOT/'tools/diagnostics/curvature/neck_sweep_v4.json')

    def test_convex_surface_has_positive_cut_costs(self):
        v,f=neck.revolution('ellipsoid',axial=16,radial=12,aspect=4.)
        g=neck.prepare(v,f,self.config)
        self.assertTrue(all(e['neck_cost']>0 for e in g['edges']))
        self.assertEqual(neck.run(g)['regions'],1)

    def test_severe_coarse_neck_splits_at_waist_deterministically(self):
        v,f=neck.revolution('neck',rho=.3,aspect=2.)
        g=neck.prepare(v,f,self.config)
        a,b=neck.run(g),neck.run(g)
        self.assertEqual(a,b)
        self.assertEqual(a['regions'],2)
        self.assertLess(a['final_energy'],a['initial_energy'])
        self.assertEqual(len(a['accepted_splits']),1)
        for e in g['edges']:
            if a['labels'][e['faces'][0]] != a['labels'][e['faces'][1]]:
                self.assertLess(max(abs(v[i][0]) for i in e['vertices']),1e-10)

    def test_frozen_sampling_failure_is_retained(self):
        v,f=neck.revolution('neck',axial=64,radial=48,rho=.3,aspect=2.)
        self.assertEqual(neck.run(neck.prepare(v,f,self.config))['regions'],1)

    def test_both_decorative_controls_remain_unsplit(self):
        for kind in ('ridge','groove'):
            v,f=neck.revolution(kind)
            self.assertEqual(neck.run(neck.prepare(v,f,self.config))['regions'],1)

    def test_fixed_partition_energy_is_scale_translation_invariant(self):
        v,f=neck.revolution('neck',rho=.3,aspect=2.)
        labels=[int(sum(v[i][0] for i in face)>0) for face in f]
        expected=neck.energy(neck.prepare(v,f,self.config),labels)
        changed=[[7*x+3,7*y-5,7*z+2] for x,y,z in v]
        actual=neck.energy(neck.prepare(changed,f,self.config),labels)
        self.assertAlmostEqual(actual,expected,places=10)

    def test_aabb_normalization_has_documented_rotation_dependence(self):
        v,f=neck.revolution('neck',rho=.3,aspect=2.)
        labels=[int(sum(v[i][0] for i in face)>0) for face in f]
        original=neck.prepare(v,f,self.config)
        theta=.37
        changed=[[x*math.cos(theta)-y*math.sin(theta),x*math.sin(theta)+y*math.cos(theta),z] for x,y,z in v]
        rotated=neck.prepare(changed,f,self.config)
        delta=neck.energy(rotated,labels)-neck.energy(original,labels)
        length=math.fsum(e['length']*original['diagonal'] for e in original['edges'] if labels[e['faces'][0]]!=labels[e['faces'][1]])
        self.assertAlmostEqual(delta,length*(1/rotated['diagonal']-1/original['diagonal']),places=10)
        self.assertGreater(abs(delta),1e-4)

    def test_open_and_inverted_meshes_are_rejected_without_repair(self):
        v,f=neck.revolution('ellipsoid',axial=8,radial=8)
        for bad in (f[:-1],[face[::-1] for face in f]):
            with self.assertRaises(ValueError):
                neck.prepare(v,bad,self.config)

    def test_unknown_nonfinite_and_invalid_config(self):
        for key,value in [('curvature_filter','made_up'),('landmarks',True),('concavity_weight',math.nan),('proposal_band_half_window',-1)]:
            config=copy.deepcopy(self.config);config[key]=value
            with self.assertRaises(ValueError):
                neck.validate_config(config)

    def test_refinement_preserves_every_old_boundary(self):
        v,f=neck.revolution('neck',axial=8,radial=8,rho=.3,aspect=2.)
        g=neck.prepare(v,f,self.config)
        initial=[int(sum(v[i][0] for i in face)>0) for face in f]
        candidate=neck.split_candidate(g,initial,[c[1] for c in g['centers']],0.,0)
        for e in g['edges']:
            a,b=e['faces']
            if initial[a]!=initial[b]:
                self.assertNotEqual(candidate[a],candidate[b])


if __name__ == '__main__':
    unittest.main()
