#!/usr/bin/env python3
"""Run and inspect source-bound curvature-extremum curves without modifying OBJ files."""
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
from datetime import datetime, timezone
from collections import Counter
from pathlib import Path
from curvature_boundary_viewer import mesh_geometry, validate_run

SCHEMA = 'intrinsic.curvature-extrema.inspection.v1'
KINDS = ['principal_ridge', 'principal_valley', 'mean_ridge', 'mean_valley', 'sharp_edge']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finite(value):
    return type(value) in (float, int) and math.isfinite(value)


def validate(record):
    source, output = Path(record['input']), Path(record['output'])
    if record['exit_code'] != 0 and not output.exists():
        raise ValueError(record.get('stderr') or 'native extraction produced no output')
    if sha(source) != record['input_sha256'] or sha(output) != record['output_sha256']:
        raise ValueError('source/output SHA-256 mismatch')
    data = json.loads(output.read_text())
    if record['exit_code'] != 0 or data.get('status') != 'success':
        raise ValueError(data.get('error', 'native extraction failed'))
    if data.get('schema') != SCHEMA or data.get('implementation') != 'normal_variation_extrema_reference_v1':
        raise ValueError('unknown schema or implementation')
    if Path(data['input']).resolve() != source.resolve():
        raise ValueError('source path mismatch')
    vertices, faces, _, adjacency = mesh_geometry(data['positions'], data['triangles'])
    diagonal = math.dist([min(v[k] for v in vertices) for k in range(3)],
                         [max(v[k] for v in vertices) for k in range(3)])
    if not diagonal > 0:
        raise ValueError('zero-size geometry')
    points = data['points']
    for point in points:
        a, b = point['edge']; t = point['fraction']; p = point['position']
        if type(a) is not int or type(b) is not int or not 0 <= a <= b < len(vertices):
            raise ValueError('invalid point edge')
        if (a != b and (a, b) not in adjacency) or not finite(t) or not 0 <= t <= 1:
            raise ValueError('point is not bound to a mesh edge')
        if a != b and not 0 < t < 1:
            raise ValueError('edge-interior point requires an interior fraction')
        if a == b and t != 0:
            raise ValueError('vertex point requires zero fraction')
        expected = [(1-t)*vertices[a][k]+t*vertices[b][k] for k in range(3)]
        if len(p) != 3 or not all(finite(x) for x in p) or math.dist(expected, p) > 2e-7*diagonal:
            raise ValueError('point differs from source interpolation')
    counts = Counter()
    degrees = Counter()
    curve_points = {}
    point_curves = {}
    lengths = Counter()
    parents = list(range(len(points)))
    def component(point):
        while parents[point] != point:
            parents[point] = parents[parents[point]]
            point = parents[point]
        return point
    segments_seen = set()
    for segment in data['segments']:
        a, b = segment['points']; face = segment['face']; scale = segment['scale']; kind = segment['kind']
        if any(type(i) is not int for i in (a, b, face, scale, segment['curve'], segment['scale_mask'])):
            raise ValueError('noninteger segment index')
        if not 0 <= a < len(points) or not 0 <= b < len(points) or a == b or not 0 <= face < len(faces):
            raise ValueError('invalid segment reference')
        if kind not in KINDS or scale not in (range(3) if kind != 'sharp_edge' else [3]):
            raise ValueError('invalid signal/scale')
        if not 1 <= segment['scale_mask'] <= 7 or (kind != 'sharp_edge' and not segment['scale_mask'] & (1 << scale)):
            raise ValueError('invalid scale agreement mask')
        if not 0 <= segment['curve'] < len(data['curves']):
            raise ValueError('invalid curve reference')
        for key in ('strength', 'sharpness', 'confidence', 'fit_residual'):
            if not finite(segment[key]) or segment[key] < 0:
                raise ValueError('invalid curve evidence')
        if segment['confidence'] > 1:
            raise ValueError('invalid confidence')
        if any(v not in faces[face] for endpoint in (a, b) for v in points[endpoint]['edge']):
            raise ValueError('segment does not lie on its source face')
        key = (kind, scale, min(a,b), max(a,b))
        if key in segments_seen:
            raise ValueError('duplicate curve segment')
        segments_seen.add(key); counts[segment['curve']] += 1
        length = math.dist(points[a]['position'], points[b]['position'])
        if length == 0:
            raise ValueError('zero-length curve segment')
        lengths[segment['curve']] += length
        parents[component(b)] = component(a)
        for point in (a, b):
            if point in point_curves and point_curves[point] != segment['curve']:
                raise ValueError('shared point belongs to different curve components')
            point_curves[point] = segment['curve']
            curve_points.setdefault(segment['curve'], set()).add(point)
            degrees[point] += 1
        curve = data['curves'][segment['curve']]
        if curve['kind'] != kind or curve['scale'] != scale:
            raise ValueError('curve signal mismatch')
    if any(counts[i] != c['segments'] for i, c in enumerate(data['curves'])):
        raise ValueError('curve count mismatch')
    for i, curve in enumerate(data['curves']):
        if len({component(p) for p in curve_points.get(i, set())}) != 1:
            raise ValueError('curve is not a connected component')
        if curve['endpoints'] != sum(degrees[p] == 1 for p in curve_points.get(i, set())) or curve['junctions'] != sum(degrees[p] > 2 for p in curve_points.get(i, set())):
            raise ValueError('curve degree diagnostics mismatch')
        if not finite(curve['length']) or abs(curve['length'] - lengths[i]) > 1e-8*diagonal:
            raise ValueError('curve length mismatch')
    for scale in range(3):
        actual = [sum(s['kind'] == kind and s['scale'] == scale for s in data['segments']) for kind in KINDS[:4]]
        if actual != data['scales'][scale]['segment_counts']:
            raise ValueError('scale segment count mismatch')
    return data


def build_payload(cohort, comparisons):
    meshes, rejected, cache = [], [], {}
    for record in cohort['runs']:
        try:
            data = validate(record)
            source = Path(record['input']).resolve()
            mesh = {'name': source.stem, 'source': str(source), 'sha256': record['input_sha256'],
                    'positions': [x for v in data['positions'] for x in v],
                    'triangles': [x for f in data['triangles'] for x in f],
                    'curvePoints': [p['position'] for p in data['points']], 'runs': []}
            for comparison in comparisons:
                for run in comparison['runs']:
                    if Path(run['command'][1]).resolve() != source or run['mode'] != 'curves':
                        continue
                    try:
                        _, _, vertices, triangles, entry = validate_run(comparison, run, cache)
                        if vertices != data['positions'] or triangles != data['triangles']:
                            raise ValueError('comparison geometry differs from curve source')
                        entry.update(key='METHOD-040 partition', cohort='previous comparison')
                        mesh['runs'].append(entry)
                    except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError) as error:
                        rejected.append({'mesh': source.stem, 'mode': 'METHOD-040', 'cohort': 'comparison', 'reason': str(error)})
            for group in ('principal', 'mean'):
                for scale in range(3):
                    segments = [s for s in data['segments'] if s['kind'] == 'sharp_edge' or
                                s['kind'].startswith(group+'_') and s['scale'] == scale]
                    ratio = data['parameters']['radius_ratio']*data['parameters']['scale_factors'][scale]
                    mesh['runs'].append({'mode': f'{group}_{scale}', 'key': f'{group.title()} curvature · radius {ratio:g} D',
                        'curveSegments': segments, 'radiusRatio': ratio,
                        'supportFraction': data['scales'][scale]['supported_vertices']/len(data['positions']), 'labels': [0]*len(data['triangles']), 'edges': [],
                        'implementation': data['implementation'], 'geometrySource': 'native curve export',
                        'cohort': 'curve inspection', 'sourceRevision': cohort['source_revision'], 'sourceDirty': cohort['source_dirty'],
                        'runnerHash': cohort['runner_sha256'], 'parameters': {}})
            meshes.append(mesh)
        except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError) as error:
            rejected.append({'mesh': Path(record['input']).stem, 'mode': 'curvature extrema', 'cohort': 'inspection', 'reason': str(error)})
    return {'meshes': meshes, 'rejected': rejected, 'cohorts': [], 'curveInspection': True}



def seal_default_measurement(record, cohort):
    """Keep inspection data separate from non-claim-eligible v2 measurements."""
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / 'tools' / 'benchmark'))
    import yaml
    from seal_benchmark_results import canonicalize
    manifest_path = root / 'benchmarks/geometry/manifests/curvature_extrema_mesh_local_cohort.yaml'
    manifest = yaml.safe_load(manifest_path.read_text())
    try:
        data = validate(record)
    except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError):
        return  # The complete failed inspection record stays in the cohort.
    if data['parameters'] != manifest['params']['detector_params']:
        record['measurement_note'] = 'Custom parameters: outside the frozen local-cohort manifest.'
        return
    diagonal = data['diagnostics']['bounding_box_diagonal']
    error = 0.0
    for point in data['points']:
        a, b = point['edge']; t = point['fraction']
        expected = [(1-t)*data['positions'][a][k]+t*data['positions'][b][k] for k in range(3)]
        error = max(error, math.dist(expected, point['position'])/diagonal)
    raw = {'benchmark_id':manifest['benchmark_id'], 'method':manifest['method'], 'dataset':manifest['dataset'],
           'backend':'cpu_reference', 'status':'passed', 'metrics':{
               'runtime_ms':data['diagnostics']['runtime_ms'], 'quality_error_l2':error,
               'quality_error_linf':0, 'population_count':len(data['segments']), 'sample_count':len(data['positions'])},
           'diagnostics':{'input_sha256':record['input_sha256'], 'runner_sha256':cohort['runner_sha256'],
               'inspection_sha256':record['output_sha256'], 'input':record['input'], 'parameters':data['parameters'],
               'stages':data['diagnostics'], 'quality_scope':'source interpolation and graph references only'}}
    run_id = 'method041-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    result = canonicalize(raw,manifest_path=manifest_path,manifest=manifest,manifests_root=root/'benchmarks',
        run_id=run_id,attempt_id='attempt-001',source_state='dirty_worktree' if cohort['source_dirty'] else 'unverified_commit',
        source_revision=cohort['source_revision'],claim_eligible=False,snapshot_sha256=None,diff_sha256=None)
    output=Path(record['output']).with_suffix('.benchmark.json')
    if output.exists():
        raise ValueError(f'refusing to overwrite measurement: {output}')
    output.write_text(json.dumps(result,indent=2))
    record['benchmark_output']=str(output);record['benchmark_sha256']=sha(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--mesh', action='append', type=Path, default=[])
    parser.add_argument('--params', type=Path)
    parser.add_argument('--cohort', type=Path)
    parser.add_argument('--comparison-cohort', action='append', type=Path, default=[])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.cohort:
        if args.runner or args.mesh or args.params:
            parser.error('--cohort cannot be combined with extraction arguments')
        cohort = json.loads(args.cohort.read_text())
    else:
        if not args.runner or not args.mesh:
            parser.error('provide --runner and --mesh, or a saved --cohort')
        root = args.output.resolve().parent
        root.mkdir(parents=True, exist_ok=True)
        runner = args.runner.resolve()
        cohort = {'schema': 'intrinsic.curvature-extrema.cohort.v1', 'runner_sha256': sha(runner),
                  'source_revision': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                  'source_dirty': bool(subprocess.check_output(['git','status','--porcelain'],text=True)), 'runs': []}
        if args.params:
            cohort['params_sha256'] = sha(args.params)
        for i, source in enumerate(args.mesh):
            source = source.resolve(); output = root / f'{i}-{source.stem}.extrema.json'
            if output.exists():
                parser.error(f'refusing to overwrite inspection evidence: {output}')
            before = sha(source); command = [str(runner), str(source), str(output)]
            if args.params:
                command.append(str(args.params.resolve()))
            try:
                completed = subprocess.run(command, text=True, capture_output=True, timeout=180)
            except subprocess.TimeoutExpired:
                completed = subprocess.CompletedProcess(command, 124, '', 'native extraction exceeded 180 seconds')
            if sha(source) != before or sha(runner) != cohort['runner_sha256']:
                raise RuntimeError('input or runner changed during extraction')
            cohort['runs'].append({'input':str(source), 'input_sha256':before, 'output':str(output),
                'output_sha256':sha(output) if output.exists() else None, 'command':command,
                'exit_code':completed.returncode, 'stdout':completed.stdout, 'stderr':completed.stderr})
            (root / 'cohort.json').write_text(json.dumps(cohort,indent=2))
        for record in cohort['runs']:
            seal_default_measurement(record, cohort)
        (root / 'cohort.json').write_text(json.dumps(cohort,indent=2))
    comparisons = [json.loads(p.read_text()) for p in args.comparison_cohort]
    payload = build_payload(cohort, comparisons)
    template = Path(__file__).with_name('curvature_boundary_viewer.html').read_text()
    encoded = json.dumps(payload,separators=(',',':'),allow_nan=False).replace('<','\\u003c')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(template.replace('__COHORT_DATA__',encoded))
    print(f"{args.output}: {len(payload['meshes'])} meshes; {len(payload['rejected'])} excluded records")
    return 0 if payload['meshes'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
