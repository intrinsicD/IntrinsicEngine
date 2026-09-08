#!/usr/bin/env python3
"""Topology, source preservation and UV-cut packing regression controls."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/diagnostics/atlas'))
import baseline_atlas as ba
import patch_merge as pm
from trace_atlas import Trace


def cylinder(n=16, rows=4, planar=False):
    v = []
    for j in range(rows+1):
        for a in np.arange(n)*2*np.pi/n:
            radius = 1+j/rows if planar else 1
            v.append((radius*np.cos(a),radius*np.sin(a),0 if planar else j/rows))
    f = []
    for j in range(rows):
        for i in range(n):
            a = j*n+i; b = j*n+(i+1)%n
            f.extend(((a,b,b+n),(a,b+n,a+n)))
    return np.array(v),np.array(f)


class BaselineAtlasTests(unittest.TestCase):
    def test_stage_observation_preserves_reference_decisions(self):
        v,f = cylinder(n=8,rows=3,planar=True)
        regions = np.zeros(len(f),dtype=np.int64)
        for arm in ('feature','protected'):
            def run():
                return (pm.execute(v,f,patches=64,feature_weight=2.,stretch_limit=1.35)
                        if arm == 'feature' else ba.execute(v,f,regions,merge=True))
            expected = run()
            trace = Trace(arm,pm.geometry(v,f),regions,{})
            previous = sys.getprofile()
            sys.setprofile(trace.profile)
            try:
                actual = run()
            finally:
                sys.setprofile(previous)
            np.testing.assert_array_equal(actual['labels'],expected['labels'])
            np.testing.assert_array_equal(actual['corner_uv'],expected['corner_uv'])
            events = [e for e in trace.events if e['kind']=='merge']
            self.assertEqual(sum(e['status']=='accepted' for e in events),
                             actual['metrics']['accepted_merges'])
            count = actual['metrics']['merge_attempts']
            self.assertEqual(len(events),count if isinstance(count,int) else len(count))
            self.assertTrue(all(len(stage['labels'])==len(f) for stage in trace.stages))

    def test_cylinder_cut_is_one_isometric_chart(self):
        v,f = cylinder(); regions = np.full(len(f),7)
        result = ba.execute(v,f,regions)
        self.assertEqual(result['metrics']['chart_count'],1)
        self.assertAlmostEqual(result['metrics']['max_stretch'],1.,places=8)
        self.assertEqual(result['metrics']['internal_uv_cut_edges'],4)
        np.testing.assert_array_equal(result['region_labels'],regions)
        self.assertTrue(result['metrics']['all_charts_valid'])

    def test_annulus_cuts_without_new_region(self):
        v,f = cylinder(planar=True)
        result = ba.execute(v,f,np.zeros(len(f),int))
        # The planar conformal solution closes the slit again. A simple-boundary
        # rejection must trigger explicit UV subdivision, not a false disk pass.
        self.assertEqual(result['metrics']['region_count'],1)
        self.assertEqual(result['metrics']['chart_count'],2)
        self.assertGreater(result['metrics']['split_reasons']['boundary_overlap'],0)
        self.assertLess(result['metrics']['max_stretch'],1.35)

    def test_protected_regions_are_not_merged(self):
        v,f = cylinder(); original_v=v.copy(); original_f=f.copy()
        regions=np.repeat([11,22],len(f)//2); before=regions.copy()
        result=ba.execute(v,f,regions,merge=True)
        self.assertEqual(result['metrics']['chart_count'],2)
        self.assertEqual(result['metrics']['retained_baseline_edges'],16)
        self.assertEqual(result['metrics']['lost_baseline_edges'],0)
        np.testing.assert_array_equal(v,original_v); np.testing.assert_array_equal(f,original_f)
        np.testing.assert_array_equal(regions,before)
        for label in np.unique(result['labels']):
            self.assertEqual(len(np.unique(regions[result['labels']==label])),1)

    def test_same_label_disconnected_components(self):
        v,f=cylinder(n=8,rows=2)
        v=np.vstack((v,v+5)); f=np.vstack((f,f+len(v)//2))
        result=ba.execute(v,f,np.zeros(len(f),int))
        self.assertEqual(result['metrics']['region_count'],1)
        self.assertEqual(result['metrics']['region_component_count'],2)
        self.assertEqual(result['metrics']['chart_count'],2)

    def test_repeat_and_similarity(self):
        v,f=cylinder(); labels=np.zeros(len(f),int)
        a=ba.execute(v,f,labels); b=ba.execute(v,f,labels)
        np.testing.assert_array_equal(a['corner_uv'],b['corner_uv'])
        # Generic shortest paths can have equal-cost alternatives: similarity
        # must preserve quality/coverage, not an arbitrarily chosen seam edge.
        c=ba.execute(v[:,[1,2,0]]*13+7,f,labels)
        self.assertEqual(c['metrics']['chart_count'],1)
        self.assertLess(c['metrics']['max_stretch'],1.000001)

    def test_pinched_source_rejected_without_repair(self):
        v=np.array([[0,0,0],[1,0,0],[0,1,0],[-1,0,0],[0,-1,0]],float)
        f=np.array([[0,1,2],[0,3,4]])
        with self.assertRaisesRegex(ValueError,'vertex fan'):
            ba.execute(v,f,np.array([0,1]))

    def test_closed_topology_explicit_split(self):
        v=np.array([[1,1,1],[-1,-1,1],[-1,1,-1],[1,-1,-1]],float)
        f=np.array([[0,2,1],[0,1,3],[1,2,3],[2,0,3]])
        result=ba.execute(v,f,np.full(4,5))
        self.assertGreater(result['metrics']['split_reasons']['unsupported_topology'],0)
        self.assertEqual(result['metrics']['outcome'],'protected_with_chart_splits')
        self.assertEqual(result['metrics']['region_count'],1)
        self.assertTrue(result['metrics']['all_charts_valid'])

    def test_invalid_labels_and_limits(self):
        v,f=cylinder()
        for labels in (np.zeros(len(f)-1,int),np.zeros(len(f)),np.full(len(f),-1)):
            with self.assertRaises(ValueError):
                ba.execute(v,f,labels)
        with self.assertRaises(ValueError):
            ba.execute(v,f,np.zeros(len(f),int),stretch_limit=float('nan'))

    @unittest.skipUnless(os.environ.get('INTRINSIC_TEST_NATIVE_ATLAS'),'opt-in native packer')
    def test_native_packer_preserves_internal_cut(self):
        v,f=cylinder(); result=ba.execute(v,f,np.zeros(len(f),int))
        with tempfile.TemporaryDirectory(prefix='baseline-atlas-test-') as directory:
            path=Path(directory)
            np.savez(path/'source.npz',vertices=v,faces=f,
                     **{k:x for k,x in result.items() if k not in ('metrics','component_attempts')})
            (path/'source.json').write_text(json.dumps({'stretch_limit':1.35}))
            call=subprocess.run([sys.executable,str(ROOT/'tools/diagnostics/atlas/repack.py'),
                                 str(path/'source.npz'),str(path/'packed.json'),'--brute-force'],cwd=ROOT,capture_output=True,text=True)
            self.assertEqual(call.returncode,0,call.stderr)
            record=json.loads((path/'packed.json').read_text())
            self.assertEqual(record['metrics']['chart_count'],1)
            self.assertTrue(record['postpack_bound_passed'])
            self.assertTrue(record['packer']['brute_force'])
            data=np.load(path/'packed.npz')
            np.testing.assert_array_equal(data['region_labels'],result['region_labels'])
            borders=ba.boundary_metrics(pm.geometry(v,f),np.zeros(len(f),int),data['labels'],data['corner_uv'])
            self.assertEqual(borders['internal_uv_cut_edges'],4)


if __name__=='__main__':
    unittest.main()
