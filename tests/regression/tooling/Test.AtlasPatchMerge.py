#!/usr/bin/env python3
"""Analytic and adversarial controls for the offline atlas experiment."""
import importlib.util
from pathlib import Path
import unittest
import sys
import os
import json
import subprocess
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patch_merge',ROOT/'tools/diagnostics/atlas/patch_merge.py')
pm = importlib.util.module_from_spec(spec); spec.loader.exec_module(pm)
sys.path.insert(0,str(ROOT/'tools/diagnostics/atlas'))
import compare_atlases as comparison


def grid(n=7, folded=False, curved=False):
    v = []
    for y in np.linspace(-1,1,n):
        for x in np.linspace(-1,1,n):
            v.append((x if not folded or x <= 0 else 0,y,x if folded and x > 0 else (.3*(x*x-y*y) if curved else 0)))
    f = []
    for j in range(n-1):
        for i in range(n-1):
            a=j*n+i; f.extend(((a,a+1,a+n+1),(a,a+n+1,a+n)))
    return np.array(v,float),np.array(f,np.int64)


class AtlasTests(unittest.TestCase):
    def test_plane_and_fold_isometric(self):
        for fold in (False,True):
            v,f = grid(folded=fold); g = pm.geometry(v,f)
            c,why = pm.parameterize(g,range(len(f)))
            self.assertEqual(why,'accepted')
            self.assertAlmostEqual(c['max_stretch'],1.,places=8)
            result = pm.execute(v,f,patches=8)
            self.assertEqual(result['metrics']['chart_count'],1)
            self.assertTrue(result['metrics']['all_charts_valid'])

    def test_topology_controls(self):
        self.assertIsNone(pm.disk_boundary(np.array([[0,1,2],[0,3,4]])))
        tetra=np.array([[0,2,1],[0,1,3],[1,2,3],[2,0,3]])
        self.assertIsNone(pm.disk_boundary(tetra))
        v,f=grid(4)
        self.assertIsNone(pm.disk_boundary(np.delete(f,[8,9],axis=0)))
        self.assertIsNotNone(pm.disk_boundary(f))

    def test_boundary_crossing_and_contact(self):
        self.assertTrue(pm.simple_boundary(np.array([[0,0],[1,0],[1,1],[0,1]],float)))
        self.assertFalse(pm.simple_boundary(np.array([[0,0],[1,1],[0,1],[1,0]],float)))
        self.assertFalse(pm.simple_boundary(np.array([[0,0],[2,0],[2,2],[1,0],[0,2]],float)))

    def test_distortion_independent_of_uniform_scale(self):
        v,f=grid(3)
        m=pm.measure_uv(v,f,v[:,:2]*4.)
        self.assertAlmostEqual(m['max_stretch'],1.)
        m=pm.measure_uv(v,f,v[:,:2]*[2.,1.])
        self.assertAlmostEqual(m['max_stretch'],np.sqrt(2.))
        self.assertAlmostEqual(m['max_anisotropy'],2.)
        self.assertIsNone(pm.measure_uv(v,f,v[:,:2]*[-1.,1.]))

    def test_deterministic_scale_and_coverage(self):
        v,f=grid(curved=True)
        a=pm.execute(v,f,patches=8); b=pm.execute(v*17+3,f,patches=8)
        np.testing.assert_array_equal(a['labels'],b['labels'])
        self.assertEqual(a['corner_uv'].shape,(len(f),3,2))
        self.assertTrue(np.isfinite(a['corner_uv']).all())
        self.assertLessEqual(a['metrics']['max_stretch'],1.5)

    def test_disconnected_coverage(self):
        v,f=grid(3)
        result=pm.execute(np.vstack((v,v+4)),np.vstack((f,f+len(v))),patches=2)
        self.assertEqual(result['metrics']['chart_count'],2)
        self.assertEqual(len(result['labels']),2*len(f))

    def test_invalid_inputs(self):
        v,f=grid(3)
        with self.assertRaisesRegex(ValueError,'degenerate'):
            pm.geometry(v,np.array([[0,0,1]]))
        with self.assertRaisesRegex(ValueError,'orientation'):
            pm.geometry(v,np.vstack((f,f[0])))

    def test_vectorized_disk_matches_reference(self):
        v,f=grid(4)
        rng=np.random.default_rng(44)
        for _ in range(300):
            subset=f[rng.random(len(f))>.3]
            a,b=pm.disk_boundary(subset),pm.disk_boundary_fast(subset)
            if a is None:
                self.assertIsNone(b)
            else:
                np.testing.assert_array_equal(a,b)
        pinch=np.array([[0,1,2],[0,3,4]])
        self.assertIsNone(pm.disk_boundary_fast(pinch))

    def test_direct_triangle_overlap(self):
        triangle=np.array([[0.,0.],[1.,0.],[0.,1.]])
        self.assertEqual(comparison.overlapping_triangles([triangle,triangle+[1.,0.]]),[])
        self.assertEqual(comparison.overlapping_triangles([triangle,triangle*.3+.1]),[(0,1)])
        self.assertEqual(comparison.overlapping_triangles([triangle,triangle]),[(0,1)])

    def test_audit_detects_packing_overlap(self):
        v,f=grid(3)
        v=np.vstack((v,v+4)); f=np.vstack((f,f+9))
        labels=np.repeat([0,1],8)
        corners=np.concatenate((v[f[:8],:2],v[f[:8],:2]))
        record=comparison.audit(v,f,labels,corners)
        self.assertFalse(record['all_charts_valid'])
        self.assertGreater(record['positive_area_overlap_count'],0)

    @unittest.skipUnless(os.environ.get('INTRINSIC_TEST_NATIVE_ATLAS'),'opt-in native diagnostic adapters')
    def test_native_adapters(self):
        v,f=grid(5,folded=True)
        with tempfile.TemporaryDirectory(prefix='atlas-adapter-test-') as directory:
            path=Path(directory)
            _,metrics=comparison.native(v,f,ROOT/'build/ci/bin/IntrinsicUvAtlasMeshDiagnostic',path)
            self.assertTrue(metrics['all_charts_valid'])
            result=pm.execute(v,f,patches=4)
            np.savez(path/'source.npz',vertices=v,faces=f,**{k:x for k,x in result.items() if k!='metrics'})
            (path/'source.json').write_text(json.dumps({'stretch_limit':1.5}))
            call=subprocess.run([sys.executable,str(ROOT/'tools/diagnostics/atlas/repack.py'),str(path/'source.npz'),str(path/'packed.json')],capture_output=True,text=True,cwd=ROOT)
            self.assertEqual(call.returncode,0,call.stderr)
            record=json.loads((path/'packed.json').read_text())
            self.assertTrue(record['postpack_bound_passed'])
            self.assertEqual(record['metrics']['chart_count'],1)
            self.assertLess(record['metrics']['max_stretch'],1.001)


if __name__=='__main__':
    unittest.main()
