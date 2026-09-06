"""Small adversarial checks for the offline viewer's face and identity bindings."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "benchmarks" / "runners"))
from curvature_boundary_viewer import read_obj, validate_run


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='curvature-viewer-test-')
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.source = root / 'mesh.obj'
        self.source.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nf -4/-4 -3/-3 -2/-2\nf -3 -1 -2\n')
        self.prefix = root / 'run'
        result = {'status': 'passed', 'benchmark_id': 'geometry.curvature_segmentation.clean_mesh.local_cohort',
                  'metrics': {'population_count': 2}, 'diagnostics': {'input': str(self.source),
                  'implementation': 'boundary_feature_area_cleanup_v1', 'faces': 2, 'vertices': 4,
                  'region_sizes': [1, 1], 'region_area_fractions': [.5, .5]}}
        self.run = {'mode': 'clean', 'mesh': 'mesh', 'exit_code': 0, 'result': result,
                    'command': ['runner', str(self.source), str(self.prefix), 'clean']}
        self.cohort = {'inputs': {'mesh': {'path': str(self.source), 'sha256': hashlib.sha256(self.source.read_bytes()).hexdigest()}}}
        Path(str(self.prefix) + '.json').write_text(json.dumps(result))
        Path(str(self.prefix) + '.labels').write_text('0\n1\n')
        Path(str(self.prefix) + '.edges').write_text('1 2 1 0.375 1\n')

    def test_negative_indices_fractional_soft_and_interior_edges(self):
        self.assertEqual(read_obj(self.source)[1], [[0, 1, 2], [1, 3, 2]])
        data = validate_run(self.cohort, self.run, {})[-1]
        self.assertEqual(data['largestArea'], .5)
        self.assertEqual(data['edges'], [1, 2, 1, .375, 1])

    def test_native_geometry_owns_vertex_order(self):
        exported = {'positions': [[1,0,0], [0,1,0], [1,1,0], [0,0,0]],
                    'triangles': [[3,0,1], [0,2,1]]}
        native = Path(str(self.prefix) + '.geometry.json')
        native.write_text(json.dumps(exported))
        self.run['output_sha256'] = {'.geometry.json': hashlib.sha256(native.read_bytes()).hexdigest()}
        Path(str(self.prefix) + '.edges').write_text('0 1 1 0.375 1\n')
        result = validate_run(self.cohort, self.run, {})
        self.assertEqual(result[2], exported['positions'])
        self.assertEqual(result[-1]['geometrySource'], 'native runner export')
        self.assertEqual(result[-1]['largestArea'], .5)

    def test_native_float32_roundtrip_preserves_small_triangle_areas(self):
        u = 2 ** -23
        positions = [[1,1,0], [1+u,1,0], [1,1+u,0], [1+8*u,1+5*u,0]]
        exported = {'positions': [[float(format(v, '.9g')) for v in p] for p in positions],
                    'triangles': [[0,1,2], [1,3,2]]}
        native = Path(str(self.prefix) + '.geometry.json')
        native.write_text(json.dumps(exported))
        self.run['output_sha256'] = {'.geometry.json': hashlib.sha256(native.read_bytes()).hexdigest()}
        self.run['result']['diagnostics']['region_area_fractions'] = [1/13, 12/13]
        Path(str(self.prefix) + '.json').write_text(json.dumps(self.run['result']))
        result = validate_run(self.cohort, self.run, {})
        self.assertEqual(result[2], positions)
        self.assertAlmostEqual(result[-1]['largestArea'], 12/13)

    def test_unbound_native_geometry_is_rejected(self):
        native = Path(str(self.prefix) + '.geometry.json')
        native.write_text(json.dumps({'positions': [[10,0,0], [11,0,0], [10,1,0], [11,1,0]],
                                      'triangles': [[0,1,2], [1,3,2]]}))
        with self.assertRaisesRegex(ValueError, 'no recorded output SHA-256'):
            validate_run(self.cohort, self.run, {})

    def test_identity_mismatch(self):
        self.run['result']['diagnostics']['implementation'] = 'method_039_local_patch_unadopted'
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            validate_run(self.cohort, self.run, {})

    def test_source_mutation(self):
        self.source.write_text(self.source.read_text() + '# changed\n')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            validate_run(self.cohort, self.run, {})

    def test_label_misalignment(self):
        Path(str(self.prefix) + '.labels').write_text('0\n')
        with self.assertRaisesRegex(ValueError, 'label count'):
            validate_run(self.cohort, self.run, {})

    def test_boundary_mismatch(self):
        Path(str(self.prefix) + '.edges').write_text('1 2 0 0.375 0\n')
        with self.assertRaisesRegex(ValueError, 'boundary disagrees'):
            validate_run(self.cohort, self.run, {})


    def test_export_hash_prevents_undetected_geometry_or_label_changes(self):
        labels = Path(str(self.prefix) + '.labels')
        self.run['output_sha256'] = {'.labels': hashlib.sha256(labels.read_bytes()).hexdigest()}
        labels.write_text('1\n0\n')
        with self.assertRaisesRegex(ValueError, 'output SHA-256 mismatch'):
            validate_run(self.cohort, self.run, {})

    def test_changed_cohort_is_not_displayed_as_valid(self):
        self.cohort['runner_unchanged'] = False
        with self.assertRaisesRegex(ValueError, 'changed during execution'):
            validate_run(self.cohort, self.run, {})


if __name__ == '__main__':
    unittest.main()
