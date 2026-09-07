#!/usr/bin/env python3
"""Evaluate provisional face partitions against source-bound METHOD-040 costs."""
from __future__ import annotations
import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'benchmarks' / 'runners'))
from curvature_boundary_viewer import mesh_geometry, validate_run

PROFILE = {'feature_weight': 4.0, 'feature_exponent': 3.0, 'boundary_scale': 0.04,
           'region_cost': math.pi * 0.04**2, 'hard_feature_exclusion_ratio': 0.04,
           'minimum_exclusion_curve_length': 0.04, 'minimum_region_area': math.pi * 0.04**2}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f'duplicate key: {key}')
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=pairs,
                      parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))


def components(labels, adjacent):
    if len(labels) != len(adjacent) or any(type(x) is not int or x < 0 for x in labels):
        raise ValueError('one nonnegative integer label per face is required')
    result = [-1] * len(labels)
    count = 0
    for first in range(len(labels)):
        if result[first] >= 0:
            continue
        result[first] = count
        stack = [first]
        while stack:
            current = stack.pop()
            for other, _ in adjacent[current]:
                if result[other] < 0 and labels[other] == labels[first]:
                    result[other] = count
                    stack.append(other)
        count += 1
    return result


def assemble(vertices, faces, evidence, params=PROFILE):
    """Independent fixed-profile assembly, including long-hard-curve attenuation."""
    if any(type(params.get(k)) not in (int, float) or
           not math.isfinite(params[k]) or not math.isclose(params[k], v, rel_tol=2e-15, abs_tol=0)
           for k, v in PROFILE.items()):
        raise ValueError('only the frozen curve-coverage profile is supported')
    vertices, faces, areas, incidence = mesh_geometry(vertices, faces)
    if any(len(fs) > 2 for fs in incidence.values()):
        raise ValueError('nonmanifold edge')
    lower = [min(v[k] for v in vertices) for k in range(3)]
    upper = [max(v[k] for v in vertices) for k in range(3)]
    diagonal = math.dist(lower, upper)
    centers = [[sum(vertices[i][k] for i in face) / 3 for k in range(3)] for face in faces]
    adjacent = [[] for _ in faces]
    edges = []
    seen = set()
    parent = list(range(len(vertices)))

    def root(v):
        while parent[v] != v:
            parent[v] = parent[parent[v]]
            v = parent[v]
        return v

    for a, b, hard, confidence in evidence:
        edge = tuple(sorted((a, b)))
        if edge in seen or edge not in incidence or len(incidence[edge]) != 2:
            raise ValueError('duplicate, noninterior or unknown evidence edge')
        if type(a) is not int or type(b) is not int or type(hard) is not int or hard not in (0, 1):
            raise ValueError('invalid edge identity or hard flag')
        if not isinstance(confidence, (int, float)) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError('invalid confidence')
        seen.add(edge)
        f, g = incidence[edge]
        midpoint = [(vertices[a][k] + vertices[b][k]) / 2 for k in range(3)]
        halves = (math.dist(centers[f], midpoint) / diagonal, math.dist(centers[g], midpoint) / diagonal)
        length = math.dist(vertices[a], vertices[b]) / diagonal
        index = len(edges)
        edges.append({'vertices': edge, 'faces': (f, g), 'hard': hard, 'confidence': confidence,
                      'length': length, 'halves': halves})
        adjacent[f].append((g, index)); adjacent[g].append((f, index))
        if hard:
            ra, rb = root(a), root(b)
            parent[max(ra, rb)] = min(ra, rb)
    if seen != {edge for edge, fs in incidence.items() if len(fs) == 2}:
        raise ValueError('evidence does not cover every interior edge')
    curve_length = {}
    for e in edges:
        if e['hard']:
            r = root(e['vertices'][0])
            curve_length[r] = curve_length.get(r, 0) + e['length']
    distance = [math.inf] * len(faces)
    queue = []
    for e in edges:
        if e['hard'] and curve_length[root(e['vertices'][0])] >= params['minimum_exclusion_curve_length']:
            for face, cost in zip(e['faces'], e['halves']):
                if cost < distance[face]:
                    distance[face] = cost
                    heapq.heappush(queue, (cost, face))
    while queue:
        cost, face = heapq.heappop(queue)
        if cost != distance[face]:
            continue
        for other, index in adjacent[face]:
            step = sum(edges[index]['halves'])
            if cost + step < distance[other]:
                distance[other] = cost + step
                heapq.heappush(queue, (cost + step, other))
    for e in edges:
        confidence = e['confidence']
        if not e['hard']:
            d = min(distance[f] + h for f, h in zip(e['faces'], e['halves']))
            ratio = d / params['hard_feature_exclusion_ratio']
            confidence *= -math.expm1(-ratio * ratio)
        e['effective_confidence'] = confidence
        e['cost'] = params['boundary_scale'] * e['length'] * (1 - params['feature_weight'] * confidence**params['feature_exponent'])
    return {'vertices': vertices, 'faces': faces, 'centers': centers, 'areas': [a / diagonal**2 for a in areas],
            'diagonal': diagonal, 'lower': lower, 'upper': upper, 'edges': edges, 'adjacent': adjacent}


def evaluate(graph, labels):
    canonical = components(labels, graph['adjacent'])
    count = max(canonical) + 1
    cuts = [e for e in graph['edges'] if canonical[e['faces'][0]] != canonical[e['faces'][1]]]
    violations = [e['vertices'] for e in graph['edges'] if e['hard'] and canonical[e['faces'][0]] == canonical[e['faces'][1]]]
    boundary_energy = math.fsum(e['cost'] for e in cuts)
    region_energy = PROFILE['region_cost'] * count
    areas = [0.0] * count
    for label, area in zip(canonical, graph['areas']):
        areas[label] += area
    total = math.fsum(areas)
    length = math.fsum(e['length'] for e in cuts)
    return {'feasible': not violations, 'hard_violations': violations, 'regions': count,
            'energy': boundary_energy + region_energy if not violations else None,
            'unconstrained_boundary_energy': boundary_energy, 'region_energy': region_energy,
            'boundary_length_over_D': length, 'boundary_edges': len(cuts),
            'area_fractions': [a / total for a in areas],
            'regions_below_cleanup_area': sum(a < PROFILE['minimum_region_area'] for a in areas),
            'labels': canonical}


def bound_run(cohort, run):
    required = {'.json', '.labels', '.edges', '.geometry.json'}
    if not required.issubset(run.get('output_sha256', {})):
        raise ValueError('complete native geometry/label/edge/result hash bindings required')
    return validate_run(cohort, run, {})


def plane_values(graph, plane):
    normal, offset = plane['normal'], plane['offset']
    if len(normal) != 3 or not all(type(x) in (int, float) and math.isfinite(x) for x in [*normal, offset]):
        raise ValueError('plane requires three finite coefficients and offset')
    gradient = [normal[k] / (graph['upper'][k] - graph['lower'][k]) if graph['upper'][k] != graph['lower'][k] else 0 for k in range(3)]
    norm = math.hypot(*gradient)
    if not norm:
        raise ValueError('zero plane gradient on mesh extent')
    values = [math.fsum(gradient[k] * (v[k] - graph['lower'][k]) for k in range(3)) - offset for v in graph['vertices']]
    return values, norm


def plane_candidate(graph, base, planes):
    area = {}
    for label, a in zip(base, graph['areas']):
        area[label] = area.get(label, 0) + a
    largest = max(area, key=lambda x: (area[x], -x))
    values = [plane_values(graph, plane)[0] for plane in planes]
    offset = max(base) + 1
    labels = list(base)
    for i, face in enumerate(graph['faces']):
        if base[i] == largest:
            bits = sum((sum(v[x] for x in face) > 0) << k for k, v in enumerate(values))
            labels[i] = offset + bits
    return components(labels, graph['adjacent']), largest


def approximation(graph, base, labels, largest, plane):
    values, gradient_norm = plane_values(graph, plane)
    segments = []
    for i, face in enumerate(graph['faces']):
        if base[i] != largest:
            continue
        points = []
        for a, b in zip(face, [face[1], face[2], face[0]]):
            va, vb = values[a], values[b]
            if va == 0:
                points.append(graph['vertices'][a])
            if va * vb < 0:
                t = va / (va - vb)
                points.append([(1-t) * x + t * y for x, y in zip(graph['vertices'][a], graph['vertices'][b])])
        unique = list(dict.fromkeys(tuple(p) for p in points))
        if len(unique) == 2:
            segments.append(unique)
    cut = [e for e in graph['edges'] if all(base[i] == largest for i in e['faces']) and labels[e['faces'][0]] != labels[e['faces'][1]]]
    length = math.fsum(e['length'] for e in cut)
    exact_length = math.fsum(math.dist(*s) for s in segments) / graph['diagonal']
    distances = [abs((values[e['vertices'][0]] + values[e['vertices'][1]]) / 2) / gradient_norm / graph['diagonal'] for e in cut]
    mean = math.fsum(e['length'] * d for e, d in zip(cut, distances)) / length if length else None
    edge_segments = [[graph['vertices'][v] for v in e['vertices']] for e in cut]
    return {'exact_planar_intersection_length_over_D': exact_length, 'source_edge_seam_length_over_D': length,
            'length_ratio': length / exact_length if exact_length else None,
            'midpoint_plane_distance_mean_over_D': mean, 'midpoint_plane_distance_max_over_D': max(distances, default=None),
            'distance_scope': 'Distance to the defining plane, a lower bound on distance to its surface intersection; not Hausdorff error.',
            'surface_curve_hausdorff_bounds_over_D': curve_distance_bounds(edge_segments, segments, graph['diagonal']),
            'positive_cost_length_fraction': math.fsum(e['length'] for e in cut if e['cost'] > 0) / length if length else None,
            'length_weighted_mean_signed_factor': math.fsum(e['cost'] for e in cut) / (PROFILE['boundary_scale'] * length) if length else None,
            'exact_intersection_segments': segments}


def curve_distance_bounds(first, second, diagonal):
    """Bound Euclidean Hausdorff distance of two segment unions without a KD tree.

    Distance to a set is 1-Lipschitz. Endpoints and midpoints cover every segment
    to within one quarter its length; nearest distances are to entire segments.
    """
    if not first or not second:
        return None

    def directed(source, target):
        cached = []
        for a, b in target:
            direction = [y - x for x, y in zip(a, b)]
            cached.append((a, direction, sum(x*x for x in direction)))
        maximum = 0.0
        for a, b in source:
            for t in (0.0, 0.5, 1.0):
                point = [x + t*(y-x) for x, y in zip(a, b)]
                best = math.inf
                for origin, direction, squared in cached:
                    s = min(1., max(0., sum((x-y)*z for x, y, z in zip(point, origin, direction)) / squared)) if squared else 0.
                    best = min(best, math.dist(point, [x+s*y for x, y in zip(origin, direction)]))
                maximum = max(maximum, best)
        return maximum, maximum + max(math.dist(a, b) for a, b in source) / 4

    a, b = directed(first, second), directed(second, first)
    return {'lower': max(a[0], b[0]) / diagonal, 'upper': max(a[1], b[1]) / diagonal,
            'metric': 'Euclidean Hausdorff distance between complete piecewise-linear curve unions'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cohort', type=Path, required=True)
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('use a fresh output directory')
    cohort, spec = read_json(args.cohort), read_json(args.spec)
    if spec.get('schema') != 'intrinsic.parts-pilot.v1':
        raise ValueError('unknown pilot schema')
    matches = [name for name, source in cohort['inputs'].items() if source['sha256'] == spec['input_sha256']]
    if len(matches) != 1:
        raise ValueError('spec must identify exactly one cohort input by hash')
    runs = {r['mode']: r for r in cohort['runs'] if r['mesh'] == matches[0]}
    selected = runs['curves']
    _, source_hash, vertices, faces, current = bound_run(cohort, selected)
    _, _, bv, bf, baseline = bound_run(cohort, runs['local'])
    if vertices != bv or faces != bf:
        raise ValueError('native comparison geometry differs')
    d = selected['result']['diagnostics']
    if d['model_weight'] != 0:
        raise ValueError('regional fitting is not supported by this evaluator')
    params = {k: d[k] for k in PROFILE}
    evidence = []
    for line in Path(selected['command'][2] + '.edges').read_text().splitlines():
        a, b, hard, confidence, _ = line.split()
        evidence.append((int(a), int(b), int(hard), float(confidence)))
    graph = assemble(vertices, faces, evidence, params)
    native = evaluate(graph, current['labels'])
    if not native['feasible'] or not math.isclose(native['energy'], d['energy'], rel_tol=1e-9, abs_tol=1e-9):
        raise ValueError('independent evaluator disagrees with native fixed-profile final energy')
    base = evaluate(graph, baseline['labels'])
    planes = {p['name']: p for p in spec['planes']}
    if len(planes) != len(spec['planes']) or len({c['name'] for c in spec['candidates']}) != len(spec['candidates']):
        raise ValueError('duplicate plane or candidate name')
    records = []
    for candidate in spec['candidates']:
        if not candidate['planes'] or len(set(candidate['planes'])) != len(candidate['planes']):
            raise ValueError('candidate requires distinct nonempty plane selections')
        chosen = [planes[name] for name in candidate['planes']]
        labels, largest = plane_candidate(graph, base['labels'], chosen)
        result = evaluate(graph, labels)
        result.update(name=candidate['name'], role='provisional coordinate competitor', cleanup='not applied')
        result['delta_vs_local_labels_under_curves_energy'] = result['energy'] - base['energy'] if result['feasible'] else None
        result['delta_vs_curves_final'] = result['energy'] - native['energy'] if result['feasible'] else None
        if len(chosen) == 1:
            result['approximation'] = approximation(graph, base['labels'], labels, largest, chosen[0])
        records.append(result)
    args.output.mkdir(parents=True)
    output = {'schema': 'intrinsic.parts-oracle.v1', 'input_sha256': source_hash,
              'cohort_sha256': digest(args.cohort), 'spec_sha256': digest(args.spec),
              'evaluator_sha256': digest(__file__), 'source_revision': cohort['source_revision'],
              'claim_eligible': False, 'profile': params, 'native_curves': native,
              'local_labels_under_curves_energy': base, 'candidates': records,
              'geometry': {'positions': vertices, 'triangles': faces}}
    (args.output / 'oracle.json').write_text(json.dumps(output, indent=2, allow_nan=False) + '\n')
    for row in [dict(native, name='native_curves'), dict(base, name='local_labels'), *records]:
        print(json.dumps({k: row[k] for k in ('name', 'feasible', 'regions', 'energy', 'regions_below_cleanup_area')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
