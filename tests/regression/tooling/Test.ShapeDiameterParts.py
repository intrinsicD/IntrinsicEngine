"""Analytic and invalid-input contracts for the offline thickness reference."""
import copy
import math
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/diagnostics/curvature'))
import shape_diameter_parts as sdf
import select_thickness_parts as parts
import refine_thickness_parts as refine
import thickness_withheld_controls as withheld


class ShapeDiameterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config=sdf.read_json(ROOT/'tools/diagnostics/curvature/shape_diameter_round1.json')

    def test_nearest_hit_does_not_skip_occluder(self):
        vertices=[[-2,-2,1],[2,-2,1],[0,2,1],[-2,-2,2],[2,-2,2],[0,2,2]]
        tree=sdf.RayTree(vertices,[[0,1,2],[3,4,5]],leaf_size=1)
        self.assertEqual(tree.nearest([0,0,0],[0,0,1]),(1.,0))
        self.assertEqual(tree.nearest([0,0,0],[0,0,1],exclude=0),(2.,1))
        self.assertIsNone(tree.nearest([0,0,0],[0,0,-1]))

    def test_bvh_matches_brute_force_and_handles_parallel_slabs(self):
        v,f=sdf.synthetic('sphere',8,8);tree=sdf.RayTree(v,f,leaf_size=2)
        for d in ([1,0,0],[0,1,0],[0,0,1],sdf.unit([1,2,3]),sdf.unit([-2,3,1])):
            hit=tree.nearest([0,0,0],d)
            brute=[(t,i) for i,triangle in enumerate(tree.triangles) if (t:=tree.triangle([0,0,0],d,triangle,1e-9,math.inf)) is not None]
            self.assertAlmostEqual(hit[0],min(brute)[0],places=13)
        self.assertIsNone(tree.nearest([3,0,0],[0,1,0]))

    def test_axis_ray_on_sphere_is_diameter(self):
        v,f=sdf.synthetic('sphere',16,16);tree=sdf.RayTree(v,f)
        self.assertAlmostEqual(tree.nearest([-1,0,0],[1,0,0])[0],2.,places=12)

    def test_cone_layout_is_unit_inward_and_deterministic(self):
        layout=sdf.ray_layout(self.config)
        self.assertEqual(layout,sdf.ray_layout(self.config));self.assertEqual(len(layout),30)
        for z,x,y,w in layout:
            self.assertAlmostEqual(z*z+x*x+y*y,1.,places=14)
            self.assertGreater(z,.5);self.assertGreater(w,0.)

    def test_sphere_field_is_supported_without_requiring_every_oblique_ray(self):
        v,f=sdf.synthetic('sphere',8,8);r=sdf.measure(sdf.geometry(v,f),self.config)
        self.assertEqual(r['supported_faces'],len(f))
        self.assertGreater(r['opposite_normal_rejections'],0)
        self.assertEqual(r['ray_misses'],0)
        self.assertTrue(all(v>0 for v in r['values_over_sqrt_area']))

    def test_fixed_ray_queries_are_rigid_and_scale_covariant(self):
        v,f=sdf.synthetic('sphere',8,8);direction=sdf.unit([1,2,3]);theta=.371
        rotate=lambda p:[p[0]*math.cos(theta)-p[1]*math.sin(theta),p[0]*math.sin(theta)+p[1]*math.cos(theta),p[2]]
        transformed=[[7*x+3 for x in rotate(p)] for p in v]
        first=sdf.RayTree(v,f).nearest([0,0,0],direction)[0]
        second=sdf.RayTree(transformed,f).nearest([3,3,3],rotate(direction))[0]
        self.assertAlmostEqual(second,7*first,places=12)

    def test_open_reversed_or_disconnected_surface_is_rejected(self):
        v,f=sdf.synthetic('sphere',8,8)
        for faces in (f[:-1],[x[::-1] for x in f]):
            with self.assertRaises(ValueError):sdf.geometry(v,faces)
        v2=v+[[x+5,y,z] for x,y,z in v]
        with self.assertRaises(ValueError):sdf.geometry(v2,f+[[i+len(v) for i in face] for face in f])

    def test_invalid_config_is_rejected(self):
        for key,value in [('ray_count',True),('ray_count',0),('minimum_valid_rays',31),('cone_opening_degrees',math.nan)]:
            c=copy.deepcopy(self.config);c[key]=value
            with self.assertRaises(ValueError):sdf.validate_config(c)

    def test_constant_plateau_is_one_peak(self):
        owner,peaks=parts.peak_tree([2.,2.,2.],[1.,1.,1.],[[1],[0,2],[1]])
        self.assertEqual(owner,[0,0,0]);self.assertEqual(len(peaks),1)

    def test_multi_plateau_saddle_uses_only_above_saddle_area(self):
        adjacent=[[4],[3,4],[3],[1,2],[0,1]]
        _,peaks=parts.peak_tree([5.,4.,3.,2.,2.],[.2]*5,adjacent)
        self.assertEqual(peaks[1]['parent_peak'],0);self.assertEqual(peaks[2]['parent_peak'],0)
        self.assertAlmostEqual(peaks[1]['core_area'],.2)
        self.assertAlmostEqual(peaks[2]['core_area'],.2)

    def test_persistence_selector_keeps_neck_and_cancels_shallow_dip(self):
        config=sdf.read_json(ROOT/'tools/diagnostics/curvature/thickness_parts_round2.json')
        graph={'areas':[.4,.2,.4],'adjacent':[[1],[0,2],[1]],'edges':[(0,1,1.,0,1),(1,2,1.,1,2)]}
        self.assertEqual(parts.select(graph,[3.,1.,3.],config)['regions'],2)
        self.assertEqual(parts.select(graph,[3.,2.9,3.],config)['regions'],1)
        self.assertEqual(parts.select(graph,[3.,2.9,3.],config,baseline=[0,1,2])['regions'],3)

    def test_missing_field_is_not_silently_imputed(self):
        with self.assertRaises(ValueError):parts.peak_tree([1.,None],[.5,.5],[[1],[0]])

    def test_explicit_small_field_completion_preserves_input(self):
        config=sdf.read_json(ROOT/'tools/diagnostics/curvature/thickness_parts_round3.json')
        graph={'areas':[.499,.002,.499],'adjacent':[[1],[0,2],[1]],'centers':[[0,0,0],[.001,0,0],[.003,0,0]]}
        values=[2.,None,3.]
        completed,diagnostics=refine.complete_field(graph,values,config)
        self.assertEqual(completed,[2.,2.,3.]);self.assertEqual(values,[2.,None,3.])
        self.assertEqual(diagnostics['filled_faces'],[1])
        self.assertEqual(diagnostics['source_face_for_fill'],[0])

    def test_field_completion_fails_closed_on_area_distance_and_invalid_values(self):
        config=sdf.read_json(ROOT/'tools/diagnostics/curvature/thickness_parts_round3.json')
        graph={'areas':[.999,.001],'adjacent':[[1],[0]],'centers':[[0,0,0],[.03,0,0]]}
        with self.assertRaises(ValueError):refine.complete_field(graph,[2.,None],config)
        graph['centers'][1]=[.001,0,0];graph['areas']=[.9,.1]
        with self.assertRaises(ValueError):refine.complete_field(graph,[2.,None],config)
        for values in ([2.,math.nan],[2.,0.],[2.,True],[2.]):
            with self.assertRaises(ValueError):refine.complete_field(graph,values,config)

    def test_connected_cleanup_merges_small_root_into_longest_shared_boundary(self):
        graph={'areas':[.49,.01,.50],'adjacent':[[1],[0,2],[1]],'edges':[(0,1,.2,0,1),(1,2,.3,1,2)]}
        labels,merges=refine.cleanup(graph,[1,0,2],.02)
        self.assertEqual(labels,[0,1,1]);self.assertEqual(len(merges),1)
        self.assertAlmostEqual(merges[0]['source_area_fraction'],.01)

    def test_cleanup_does_not_erase_baseline_boundaries(self):
        config=sdf.read_json(ROOT/'tools/diagnostics/curvature/thickness_parts_round3.json')
        graph={'areas':[.495,.01,.495],'adjacent':[[1],[0,2],[1]],'edges':[(0,1,1.,0,1),(1,2,1.,1,2)]}
        result=refine.select(graph,[2.,2.,2.],config,baseline=[0,1,0])
        self.assertEqual(result['clean_regions_before_baseline'],1)
        self.assertEqual(result['regions'],3)

    def test_withheld_fixtures_are_closed_and_have_exact_attachment_ring(self):
        config=sdf.read_json(ROOT/'tools/diagnostics/curvature/thickness_parts_round4.json')
        for kind in ('taper','bulge'):
            v,f=withheld.fixture(config,kind,16,12)
            self.assertEqual(len(sdf.geometry(v,f)['areas']),360)
            self.assertEqual(sum(abs(p[0]-config['junction_x'])<1e-12 for p in v),12)
            limb=[p for p in v if p[0]>config['junction_x']]
            self.assertTrue(all(p[0]<=config['junction_x']+config['limb_length'] for p in limb))

    def test_withheld_rigid_transform_preserves_pairwise_distance_up_to_scale(self):
        v=[[0.,0.,0.],[1.,2.,3.],[-3.,1.,2.]];other=withheld.transform(v)
        for i in range(3):
            for j in range(i):self.assertAlmostEqual(math.dist(other[i],other[j]),3.7*math.dist(v[i],v[j]),places=12)


if __name__=='__main__':unittest.main()
