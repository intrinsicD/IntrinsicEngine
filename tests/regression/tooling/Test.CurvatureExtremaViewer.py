"""Check exact curve/source bindings and reject misleading inspection payloads."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'benchmarks/runners'))
from curvature_extrema_viewer import validate, build_payload

class CurveValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='extrema-viewer-');self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name);self.source=root/'surface.obj';self.output=root/'curves.json'
        self.source.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
        self.data={'schema':'intrinsic.curvature-extrema.inspection.v1','implementation':'normal_variation_extrema_reference_v1',
            'input':str(self.source),'status':'success','positions':[[0,0,0],[1,0,0],[0,1,0]],'triangles':[[0,1,2]],
            'points':[{'position':[0.5,0,0],'edge':[0,1],'fraction':0.5},{'position':[0,0.5,0],'edge':[0,2],'fraction':0.5}],
            'segments':[{'points':[0,1],'face':0,'curve':0,'kind':'principal_ridge','scale':1,'scale_mask':2,
                         'strength':0.2,'sharpness':0.1,'confidence':0.5,'fit_residual':0.01}],
            'curves':[{'kind':'principal_ridge','scale':1,'segments':1,'endpoints':2,'junctions':0,'length':2**-0.5}],
            'scales':[{'radius':r,'segment_counts':counts} for r,counts in [(0.01,[0,0,0,0]),(0.02,[1,0,0,0]),(0.04,[0,0,0,0])]],
            'parameters':{'radius_ratio':0.02,'scale_factors':[0.5,1,2]}}
        self.record={'input':str(self.source),'output':str(self.output),'input_sha256':hashlib.sha256(self.source.read_bytes()).hexdigest(),'exit_code':0}
        self.save()
    def save(self):
        self.output.write_text(json.dumps(self.data));self.record['output_sha256']=hashlib.sha256(self.output.read_bytes()).hexdigest()
    def test_valid_source_interpolation(self):
        self.assertEqual(validate(self.record)['segments'][0]['points'],[0,1])
    def test_reject_off_surface_points(self):
        self.data['points'][0]['position'][2]=0.01;self.save()
        with self.assertRaisesRegex(ValueError,'interpolation'):validate(self.record)
    def test_reject_source_mutation(self):
        self.source.write_text(self.source.read_text()+'# changed\n')
        with self.assertRaisesRegex(ValueError,'SHA-256'):validate(self.record)
    def test_reject_fraction_and_vertex_disagreement(self):
        self.data['points'][0]['edge']=[0,0];self.save()
        with self.assertRaisesRegex(ValueError,'zero fraction'):validate(self.record)
    def test_reject_wrong_face(self):
        self.data['segments'][0]['face']=1;self.save()
        with self.assertRaisesRegex(ValueError,'segment reference'):validate(self.record)
    def test_reject_wrong_curve_identity(self):
        self.data['curves'][0]['kind']='mean_ridge';self.save()
        with self.assertRaisesRegex(ValueError,'signal mismatch'):validate(self.record)
    def test_reject_missing_scale_agreement(self):
        self.data['segments'][0]['scale_mask']=1;self.save()
        with self.assertRaisesRegex(ValueError,'agreement mask'):validate(self.record)
    def test_reject_nonfinite_evidence(self):
        self.data['segments'][0]['confidence']=float('nan');self.save()
        with self.assertRaisesRegex(ValueError,'curve evidence'):validate(self.record)
    def test_failed_native_record_remains_excluded(self):
        self.record.update(exit_code=124,stderr='native extraction exceeded 180 seconds');self.output.unlink()
        payload=build_payload({'runs':[self.record]},[])
        self.assertEqual(payload['meshes'],[]);self.assertIn('180 seconds',payload['rejected'][0]['reason'])
    def test_reject_duplicate_segment(self):
        self.data['segments']*=2;self.save()
        with self.assertRaisesRegex(ValueError,'duplicate curve segment'):validate(self.record)
    def test_reject_false_endpoint_diagnostics(self):
        self.data['curves'][0]['endpoints']=0;self.save()
        with self.assertRaisesRegex(ValueError,'degree diagnostics'):validate(self.record)
    def test_reject_false_curve_length(self):
        self.data['curves'][0]['length']=5;self.save()
        with self.assertRaisesRegex(ValueError,'curve length'):validate(self.record)
    def test_reject_disconnected_curve(self):
        self.data['points'] += [dict(p) for p in self.data['points']]
        self.data['segments'].append(dict(self.data['segments'][0], points=[2,3]))
        self.data['curves'][0].update(segments=2,endpoints=4,length=2**0.5)
        self.data['scales'][1]['segment_counts'][0]=2;self.save()
        with self.assertRaisesRegex(ValueError,'connected component'):validate(self.record)
    def test_reject_count_mismatch(self):
        self.data['scales'][1]['segment_counts'][0]=2;self.save()
        with self.assertRaisesRegex(ValueError,'count mismatch'):validate(self.record)
if __name__=='__main__':unittest.main()
