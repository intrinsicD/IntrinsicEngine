#!/usr/bin/env python3
"""Embed validated local METHOD-040 cohort outputs in a standalone WebGL viewer."""
import argparse
import hashlib
import json
import math
import struct
from collections import Counter, defaultdict
from pathlib import Path

IMPLEMENTATIONS = {
    'multicut': 'boundary_multicut_isotropic_v2',
    'regional': 'curvature_region_scale_v2',
    'contrast': 'boundary_feature_contrast_v1',
    'clean': 'boundary_feature_area_cleanup_v1',
    'clean_medium': 'boundary_feature_area_cleanup_v1',
    'clean_coarse': 'boundary_feature_area_cleanup_v1',
    'curves': 'boundary_feature_curve_coverage_v1',
    'local': 'method_039_local_patch_unadopted',
}


def read_obj(path):
    """Keep triangle face order; resolve negative references at the face line."""
    vertices, triangles = [], []
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        tokens = line.partition('#')[0].split()
        if not tokens:
            continue
        if tokens[0] == 'v':
            if len(tokens) < 4:
                raise ValueError(f'OBJ line {line_number}: incomplete vertex')
            vertex = [float(x) for x in tokens[1:4]]
            if not all(math.isfinite(x) for x in vertex):
                raise ValueError(f'OBJ line {line_number}: nonfinite vertex')
            vertices.append(vertex)
        elif tokens[0] == 'f':
            if len(tokens) != 4:
                raise ValueError(f'OBJ line {line_number}: nontriangle; face order cannot be remapped')
            face = []
            for token in tokens[1:]:
                index = int(token.split('/')[0])
                if index == 0 or (index < 0 and -index > len(vertices)):
                    raise ValueError(f'OBJ line {line_number}: invalid face index')
                face.append(index - 1 if index > 0 else len(vertices) + index)
            triangles.append(face)
    return mesh_geometry(vertices, triangles)


def mesh_geometry(vertices, triangles):
    """Validate loaded arrays and derive areas and interior edge adjacency."""
    if any(len(v) != 3 or any(not isinstance(x, (int, float)) or not math.isfinite(x) for x in v) for v in vertices):
        raise ValueError('geometry contains an invalid vertex')
    if any(len(face) != 3 or any(type(i) is not int for i in face) for face in triangles):
        raise ValueError('geometry contains an invalid triangle')
    if not vertices or not triangles:
        raise ValueError('OBJ contains no triangle mesh')
    if any(i < 0 or i >= len(vertices) for face in triangles for i in face):
        raise ValueError('OBJ face index outside vertex array')
    areas = []
    adjacency = defaultdict(list)
    for face_index, face in enumerate(triangles):
        a, b, c = [vertices[i] for i in face]
        u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
        cross = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        area = math.hypot(*cross) / 2
        if not math.isfinite(area) or area <= 0:
            raise ValueError(f'OBJ face {face_index}: degenerate triangle')
        areas.append(area)
        for i in range(3):
            adjacency[tuple(sorted((face[i], face[(i+1) % 3])))].append(face_index)
    return vertices, triangles, areas, adjacency


def validate_run(cohort, run, cache):
    result = run.get('result') or {}
    if run.get('exit_code') != 0 or result.get('status') != 'passed':
        raise ValueError(f"run exit {run.get('exit_code')}; {result.get('stage', '')} {result.get('diagnostic', result.get('status', 'no result'))}".strip())
    mode, name = run['mode'], run['mesh']
    diagnostic = result['diagnostics']
    expected = IMPLEMENTATIONS.get(mode)
    if expected is None or diagnostic.get('implementation') != expected:
        raise ValueError(f"identity mismatch: {mode} reports {diagnostic.get('implementation')!r}; expected {expected!r}")
    expected_id = f"geometry.curvature_segmentation.{'boundary' if mode == 'multicut' else mode}_mesh.local_cohort"
    valid_ids = {expected_id}
    if mode == 'local':  # Older local cohorts use a shared benchmark ID but a distinct implementation ID.
        valid_ids.add('geometry.curvature_segmentation.boundary_mesh.local_cohort')
    if result.get('benchmark_id') not in valid_ids:
        raise ValueError(f"benchmark identity mismatch: {result.get('benchmark_id')}")
    command = run['command']
    if len(command) < 4 or command[3] != mode:
        raise ValueError('recorded command mode mismatch')
    source = Path(command[1]).resolve()
    recorded = cohort['inputs'][name]
    if source != Path(recorded['path']).resolve() or source != Path(diagnostic['input']).resolve():
        raise ValueError('source path mismatch')
    if source not in cache:
        cache[source] = hashlib.sha256(source.read_bytes()).hexdigest()
    source_hash = cache[source]
    if source_hash != recorded.get('sha256'):
        raise ValueError('source SHA-256 mismatch')
    prefix = command[2]
    for suffix, expected_hash in run.get('output_sha256', {}).items():
        output = Path(prefix + suffix)
        if not output.is_file() or hashlib.sha256(output.read_bytes()).hexdigest() != expected_hash:
            raise ValueError(f'output SHA-256 mismatch: {suffix}')
    if cohort.get('inputs_unchanged') is False or cohort.get('runner_unchanged') is False:
        raise ValueError('cohort input or runner changed during execution')
    native = Path(prefix + '.geometry.json')
    if native.exists() and '.geometry.json' not in run.get('output_sha256', {}):
        raise ValueError('native geometry has no recorded output SHA-256')
    geometry_path = native if native.exists() else source
    geometry_key = ('geometry', geometry_path)
    if geometry_key not in cache:
        if native.exists():
            exported = json.loads(native.read_text())
            # The native mesh stores float32 positions. Decimal max_digits10
            # guarantees that roundtrip, not identical double-precision areas.
            positions = [[struct.unpack('f', struct.pack('f', value))[0]
                          for value in vertex] for vertex in exported['positions']]
            cache[geometry_key] = mesh_geometry(positions, exported['triangles'])
        else:
            cache[geometry_key] = read_obj(source)
    vertices, triangles, areas, adjacency = cache[geometry_key]
    if len(triangles) != diagnostic.get('faces') or len(vertices) != diagnostic.get('vertices'):
        raise ValueError('loaded vertex/face count differs from runner diagnostics')
    if json.loads(Path(prefix + '.json').read_text()) != result:
        raise ValueError('output JSON differs from the cohort record')
    labels = [int(x) for x in Path(prefix + '.labels').read_text().split()]
    if len(labels) != len(triangles):
        raise ValueError('label count differs from source face count')
    counts = Counter(labels)
    if sorted(counts) != list(range(len(counts))):
        raise ValueError('labels are not contiguous assigned nonnegative region IDs')
    if len(counts) != result['metrics'].get('population_count'):
        raise ValueError('region count differs from reported population_count')
    if 'region_sizes' in diagnostic and [counts[i] for i in range(len(counts))] != diagnostic['region_sizes']:
        raise ValueError('region sizes differ from reported face counts')
    edges, seen = [], set()
    for line_number, line in enumerate(Path(prefix + '.edges').read_text().splitlines(), 1):
        tokens = line.split()
        if len(tokens) != 5:
            raise ValueError(f'edge line {line_number}: expected vA vB hard soft finalBoundary')
        a, b, hard, boundary = (int(tokens[i]) for i in (0, 1, 2, 4))
        soft = float(tokens[3])
        row = [a, b, hard, soft, boundary]
        edge = tuple(sorted((a, b)))
        if edge not in adjacency or edge in seen or hard not in (0, 1) or boundary not in (0, 1) or not (0 <= soft <= 1):
            raise ValueError(f'edge line {line_number}: invalid, duplicate, or nonmesh edge')
        seen.add(edge)
        faces = adjacency[edge]
        if len(faces) == 2 and bool(boundary) != (labels[faces[0]] != labels[faces[1]]):
            raise ValueError(f'edge line {line_number}: boundary disagrees with labels')
        if hard and not boundary:
            raise ValueError(f'edge line {line_number}: hard feature is not a final boundary')
        if hard or soft or boundary:
            edges.extend(row)
    if seen != {edge for edge, faces in adjacency.items() if len(faces) == 2}:
        raise ValueError('edge output does not cover the source mesh interior edges')
    region_areas = [0.0] * len(counts)
    for label, area in zip(labels, areas):
        region_areas[label] += area
    total_area = math.fsum(region_areas)
    fractions = [a / total_area for a in region_areas]
    reported = diagnostic.get('region_area_fractions')
    if reported is not None and (len(reported) != len(fractions) or any(abs(a-b) > 1e-5 for a, b in zip(reported, fractions))):
        raise ValueError('recomputed region areas differ from runner diagnostics')
    data = {
        'mode': mode, 'implementation': expected, 'labels': labels, 'edges': edges,
        'geometrySource': 'native runner export' if native.exists() else 'source OBJ fallback',
        'regions': len(counts), 'largestArea': max(fractions),
        'smallRegions': sum(x < 0.001 for x in fractions),
        'runtimeMs': result['metrics'].get('runtime_ms'), 'benchmarkId': result['benchmark_id'],
        'sourceRevision': cohort.get('source_revision', 'unknown'),
        'sourceDirty': cohort.get('source_dirty'), 'runnerHash': cohort.get('runner_sha256', 'unknown'),
        'parameters': {k: diagnostic[k] for k in ('feature_weight', 'feature_exponent', 'boundary_scale', 'region_cost', 'minimum_region_area', 'minimum_exclusion_curve_length') if k in diagnostic},
    }
    return source, source_hash, vertices, triangles, data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cohort', action='append', required=True, type=Path, help='Cohort directory or cohort.json; repeat for comparisons')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    meshes, rejected, cache, cohort_paths = {}, [], {}, []
    for path in args.cohort:
        path = (path / 'cohort.json' if path.is_dir() else path).resolve()
        cohort_paths.append(str(path))
        try:
            cohort = json.loads(path.read_text())
            runs = cohort['runs']
        except (OSError, ValueError, KeyError, TypeError) as error:
            rejected.append({'mesh': 'cohort', 'mode': '—', 'cohort': path.parent.name, 'reason': str(error)})
            continue
        for run in runs:
            try:
                source, digest, vertices, triangles, data = validate_run(cohort, run, cache)
                geometry_digest = hashlib.sha256(json.dumps([vertices, triangles], separators=(',', ':')).encode()).hexdigest()
                mesh_key = str(source) + ':' + digest + ':' + geometry_digest
                if mesh_key not in meshes:
                    meshes[mesh_key] = {'name': run['mesh'], 'source': str(source), 'sha256': digest,
                                        'positions': [x for v in vertices for x in v],
                                        'triangles': [i for face in triangles for i in face], 'runs': []}
                data['cohort'] = path.parent.name
                data['key'] = f"{data['mode']} · {path.parent.name.removeprefix('method040-cohort-')}"
                meshes[mesh_key]['runs'].append(data)
            except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError) as error:
                rejected.append({'mesh': run.get('mesh', '?'), 'mode': run.get('mode', '?'), 'cohort': path.parent.name, 'reason': str(error)})
    payload = {'meshes': list(meshes.values()), 'rejected': rejected, 'cohorts': cohort_paths}
    template = Path(__file__).with_name('curvature_boundary_viewer.html').read_text()
    encoded = json.dumps(payload, separators=(',', ':'), allow_nan=False).replace('<', '\\u003c')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(template.replace('__COHORT_DATA__', encoded))
    count = sum(len(m['runs']) for m in meshes.values())
    print(f'{args.output}: {len(meshes)} meshes, {count} validated runs, {len(rejected)} excluded records')
    return 0 if count else 1


if __name__ == '__main__':
    raise SystemExit(main())
