#!/usr/bin/env python3
"""Offline CPU neck-sweep experiment; not an engine backend or a paper replica."""
from __future__ import annotations
import argparse
import bisect
import heapq
import json
import math
from pathlib import Path
import sys

from evaluate_part_seams import assemble, bound_run, components, digest, read_json


def dot(a, b):
    return math.fsum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def validate_config(config):
    if config.get('schema') != 'intrinsic.neck-sweep.v1' or config.get('implementation') != 'neck_sweep_cpu_diagnostic_v1':
        raise ValueError('unknown neck sweep configuration')
    for key in ('landmarks', 'profile_half_window', 'prominence_half_window', 'maximum_splits'):
        if type(config.get(key)) is not int or not 1 <= config[key] <= 100:
            raise ValueError(f'invalid {key}')
    for key in ('smoothing_radius_over_D', 'concavity_weight', 'region_cost', 'minimum_child_area_fraction', 'quantile_step', 'minimum_relative_prominence'):
        if type(config.get(key)) not in (float, int) or not math.isfinite(config[key]) or config[key] <= 0:
            raise ValueError(f'invalid {key}')
    if not .001 <= config['quantile_step'] <= .1 or not 0 < config['minimum_child_area_fraction'] < .5:
        raise ValueError('invalid area sweep range')
    if config['smoothing_radius_over_D'] > .2 or config['minimum_relative_prominence'] >= 1:
        raise ValueError('invalid radius or prominence')
    if config.get('curvature_filter', 'face_mean_v1') not in ('face_mean_v1', 'transverse_alignment_v2', 'edge_unfiltered_v3'):
        raise ValueError('unknown curvature filter')
    if type(config.get('proposal_band_half_window', 0)) is not int or not 0 <= config.get('proposal_band_half_window', 0) <= 10:
        raise ValueError('invalid proposal band')
    return config


def distances(graph, source, limit=math.inf):
    found = {source: 0.}
    queue = [(0., source)]
    while queue:
        d, face = heapq.heappop(queue)
        if d != found[face]:
            continue
        for other, index in graph['adjacent'][face]:
            cost = d + sum(graph['edges'][index]['halves'])
            if cost <= limit and cost < found.get(other, math.inf):
                found[other] = cost
                heapq.heappush(queue, (cost, other))
    return found


def prepare(vertices, faces, config, evidence=None):
    validate_config(config)
    incidence = {}
    for face in faces:
        for a, b in zip(face, [face[1], face[2], face[0]]):
            incidence.setdefault(tuple(sorted((a, b))), []).append((a, b))
    if any(len(es) != 2 or es[0] != es[1][::-1] for es in incidence.values()):
        raise ValueError('closed consistently oriented triangle surface required')
    if evidence is None:
        evidence = [(a, b, 0, 0.) for a, b in sorted(incidence)]
    graph = assemble(vertices, faces, evidence)
    origin = graph['lower']
    volume6 = math.fsum(dot(sub(vertices[f[0]], origin), cross(sub(vertices[f[1]], origin), sub(vertices[f[2]], origin))) for f in faces)
    if volume6 <= 0:
        raise ValueError('outward orientation (positive signed volume) required; no repair performed')
    if len(distances(graph, 0)) != len(faces):
        raise ValueError('one connected surface required')
    normals = []
    for a, b, c in graph['faces']:
        n = cross(sub(vertices[b], vertices[a]), sub(vertices[c], vertices[a]))
        normals.append([v / math.hypot(*n) for v in n])
    accum, weights = [0.] * len(faces), [0.] * len(faces)
    for edge in graph['edges']:
        f, g = edge['faces']
        delta = sub(graph['centers'][g], graph['centers'][f])
        # D*kappa is dimensionless, symmetric in incident face order.
        kappa = dot(sub(normals[g], normals[f]), delta) / dot(delta, delta) * graph['diagonal']
        edge['raw_kappa_times_D'] = kappa
        edge['transverse'] = [x / math.hypot(*delta) for x in delta]
        for face in (f, g):
            accum[face] += kappa * edge['length']
            weights[face] += edge['length']
    values = [a/w for a, w in zip(accum, weights)]
    radius = config['smoothing_radius_over_D']
    smoothed, neighborhoods = [], []
    for face in range(len(faces)):
        neighborhood = {face: 0.} if config.get('curvature_filter') == 'edge_unfiltered_v3' else distances(graph, face, radius)
        neighborhoods.append(neighborhood)
        pairs = [(graph['areas'][i] * math.exp(-2*(d/radius)**2), values[i]) for i, d in neighborhood.items()]
        smoothed.append(math.fsum(w*v for w, v in pairs) / math.fsum(w for w, _ in pairs))
    for edge in graph['edges']:
        f, g = edge['faces']
        kappa = (smoothed[f]*graph['areas'][f] + smoothed[g]*graph['areas'][g]) / (graph['areas'][f]+graph['areas'][g])
        if config.get('curvature_filter') == 'transverse_alignment_v2':
            nearby = {}
            for face in (f, g):
                for other, distance in neighborhoods[face].items():
                    for _, index in graph['adjacent'][other]:
                        nearby[index] = min(nearby.get(index, math.inf), distance)
            samples = []
            for index, distance in nearby.items():
                sample = graph['edges'][index]
                weight = sample['length'] * math.exp(-2*(distance/radius)**2) * dot(sample['transverse'],edge['transverse'])**8
                samples.append((weight, sample['raw_kappa_times_D']))
            kappa = math.fsum(w*v for w,v in samples) / math.fsum(w for w,_ in samples)
        elif config.get('curvature_filter') == 'edge_unfiltered_v3':
            kappa = edge['raw_kappa_times_D']
        edge['neck_cost'] = edge['length'] * (1 + config['concavity_weight'] * radius * kappa)
    graph['config'] = config
    return graph


def energy(graph, labels):
    labels = components(labels, graph['adjacent'])
    if any(e['hard'] and labels[e['faces'][0]] == labels[e['faces'][1]] for e in graph['edges']):
        raise ValueError('hard-infeasible partition')
    boundary = math.fsum(e['neck_cost'] for e in graph['edges'] if labels[e['faces'][0]] != labels[e['faces'][1]])
    return boundary + graph['config']['region_cost'] * (max(labels)+1)


def profiles(graph):
    config = graph['config']
    total = math.fsum(graph['areas'])
    # Most distant face from the area centroid: intrinsic distances thereafter.
    center = [math.fsum(a*c[k] for a, c in zip(graph['areas'], graph['centers'])) / total for k in range(3)]
    first = max(range(len(graph['faces'])), key=lambda i: (math.dist(center, graph['centers'][i]), -i))
    nearest = [math.inf] * len(graph['faces'])
    seen, output = set(), []
    for _ in range(min(config['landmarks'], len(graph['faces']))):
        source = first if not seen else max((i for i in range(len(nearest)) if i not in seen), key=lambda i: (nearest[i], -i))
        seen.add(source)
        field = distances(graph, source)
        nearest = [min(n, field[i]) for i, n in enumerate(nearest)]
        order = sorted(field, key=lambda i: (field[i], i))
        cumulative, running = [], 0.
        for face in order:
            running += graph['areas'][face]
            cumulative.append(running)
        entries = []
        min_area, step = config['minimum_child_area_fraction'], config['quantile_step']
        count = int(math.floor((1-2*min_area)/step + 1e-10)) + 1
        for k in range(count):
            fraction = min_area + k*step
            index = min(len(order)-2, bisect.bisect_left(cumulative, total*fraction))
            threshold = field[order[index]]
            # Whole equal-distance plateaus stay together; no face-ID cuts.
            mask = [int(field[i] <= threshold) for i in range(len(order))]
            area = math.fsum(a for a, value in zip(graph['areas'], mask) if value)
            if min(area, total-area) <= 0:
                continue
            length = math.fsum(e['length'] for e in graph['edges'] if mask[e['faces'][0]] != mask[e['faces'][1]])
            entries.append({'fraction': area/total, 'threshold': threshold, 'score': length/math.sqrt(min(area,total-area))})
        half = config['profile_half_window']
        smooth = [math.fsum(r['score'] for r in entries[max(0,i-half):i+half+1]) / len(entries[max(0,i-half):i+half+1]) for i in range(len(entries))]
        wide = config['prominence_half_window']
        for i, entry in enumerate(entries):
            entry['smoothed_score'] = smooth[i]
            entry['relative_prominence'] = 0.
            if wide <= i < len(entries)-wide and smooth[i] < smooth[i-1] and smooth[i] <= smooth[i+1]:
                shoulder = min(max(smooth[i-wide:i]), max(smooth[i+1:i+wide+1]))
                entry['relative_prominence'] = (shoulder-smooth[i])/shoulder if shoulder else 0.
        band = config.get('proposal_band_half_window', 0)
        for i, entry in enumerate(entries):
            entry['proposal_prominence'] = max(e['relative_prominence'] for e in entries[max(0,i-band):i+band+1])
        output.append({'source_face': source, 'field': [field[i] for i in range(len(order))], 'entries': entries})
    return output


def split_candidate(graph, labels, field, threshold, target):
    fresh = max(labels)+1
    candidate = [fresh if label == target and field[i] <= threshold else label for i, label in enumerate(labels)]
    return components(candidate, graph['adjacent'])


def run(graph, initial=None, regional_gate=True, concavity_gate=True):
    initial = [0] * len(graph['faces']) if initial is None else initial
    labels = components(initial, graph['adjacent'])
    start = energy(graph, labels)
    config, total = graph['config'], math.fsum(graph['areas'])
    fields = profiles(graph)
    accepted, considered = [], 0
    for _ in range(config['maximum_splits']):
        before = energy(graph, labels)
        best = None
        region_areas = {}
        for label, area in zip(labels, graph['areas']):
            region_areas[label] = region_areas.get(label, 0.) + area
        targets = [label for label, area in region_areas.items() if area >= 2*config['minimum_child_area_fraction']*total]
        for p in fields:
            for entry in p['entries']:
                if regional_gate and entry['proposal_prominence'] < config['minimum_relative_prominence']:
                    continue
                for target in targets:
                    candidate = split_candidate(graph, labels, p['field'], entry['threshold'], target)
                    if max(candidate) <= max(labels):
                        continue
                    child_areas = {}
                    for old, child, area in zip(labels, candidate, graph['areas']):
                        if old == target:
                            child_areas[child] = child_areas.get(child, 0.) + area
                    if min(child_areas.values()) < config['minimum_child_area_fraction']*total:
                        continue
                    delta = energy(graph, candidate)-before
                    considered += 1
                    if concavity_gate and delta >= -1e-12:
                        continue
                    # Regional-only ablation ranks prominence, then energy.
                    key = (delta,) if concavity_gate else (-entry['proposal_prominence'], delta)
                    if best is None or key < best[0]:
                        best = (key, candidate, {'source_face': p['source_face'], 'target_region': target, 'delta_energy': delta,
                                                'delta_regions': max(candidate)-max(labels), **entry})
        if best is None:
            break
        _, labels, diagnostic = best
        accepted.append(diagnostic)
    region_areas = [0.] * (max(labels)+1)
    for label, area in zip(labels, graph['areas']):
        region_areas[label] += area
    return {'implementation': config['implementation'], 'backend': 'offline_cpu_reference', 'claim_eligible': False,
            'regional_gate': regional_gate, 'concavity_gate': concavity_gate, 'initial_energy': start,
            'final_energy': energy(graph, labels), 'regions': len(region_areas), 'area_fractions': [a/total for a in region_areas],
            'accepted_splits': accepted, 'considered_feasible_splits': considered, 'labels': labels,
            'profiles': [{k:v for k,v in p.items() if k != 'field'} for p in fields],
            'minimum_raw_kappa_times_D': min(e['raw_kappa_times_D'] for e in graph['edges'])}


def revolution(kind, axial=32, radial=24, diagonal=0, clustered=False, rho=.5, aspect=1.):
    """Explicit closed synthetic surface; rho is waist radius, not lobe ratio."""
    vertices = [[-aspect,0.,0.]]
    for i in range(1, axial):
        u = -math.cos(math.pi*i/axial)
        if clustered:
            u = math.copysign(abs(u)**1.4, u)
        radius = math.sqrt(max(0.,1-u*u))
        if kind == 'neck':
            radius *= rho+(1-rho)*math.sin(math.pi*u)**2
        elif kind == 'ridge':
            radius *= 1+.12*math.exp(-(u/.10)**2)
        elif kind == 'groove':
            radius *= 1-.05*math.exp(-(u/.10)**2)
        elif kind != 'ellipsoid':
            raise ValueError('unknown synthetic shape')
        for j in range(radial):
            theta = 2*math.pi*j/radial
            vertices.append([aspect*u, radius*math.cos(theta), radius*math.sin(theta)])
    pole = len(vertices)
    vertices.append([aspect,0.,0.])
    faces = []
    for j in range(radial):
        faces.append([0,1+(j+1)%radial,1+j])
    for i in range(axial-2):
        for j in range(radial):
            a, b = 1+i*radial+j, 1+i*radial+(j+1)%radial
            c, d = a+radial, b+radial
            faces.extend([[a,b,c],[b,d,c]] if diagonal else [[a,b,d],[a,d,c]])
    start = 1+(axial-2)*radial
    for j in range(radial):
        faces.append([start+j,start+(j+1)%radial,pole])
    volume = math.fsum(dot(vertices[a],cross(vertices[b],vertices[c])) for a,b,c in faces)
    if volume < 0:
        faces = [f[::-1] for f in faces]
    return vertices, faces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--synthetic', choices=['ellipsoid','neck','ridge','groove'])
    group.add_argument('--cohort', type=Path)
    parser.add_argument('--mesh', default='frog')
    parser.add_argument('--axial', type=int, default=32)
    parser.add_argument('--radial', type=int, default=24)
    parser.add_argument('--diagonal', type=int, choices=[0,1], default=0)
    parser.add_argument('--clustered', action='store_true')
    parser.add_argument('--rho', type=float, default=.5)
    parser.add_argument('--aspect', type=float, default=1.)
    parser.add_argument('--ablation', choices=['none','regional_only','concavity_only'], default='none')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('use a fresh output directory')
    config = validate_config(read_json(args.config))
    initial, evidence, source_hash = None, None, None
    if args.synthetic:
        if args.axial < 8 or args.radial < 8 or not 0 < args.rho <= 1 or args.aspect <= 0 or not math.isfinite(args.aspect):
            parser.error('invalid synthetic geometry parameters')
        vertices, faces = revolution(args.synthetic,args.axial,args.radial,args.diagonal,args.clustered,args.rho,args.aspect)
    else:
        cohort = read_json(args.cohort)
        matches = [r for r in cohort['runs'] if r['mesh'] == args.mesh and r['mode'] == 'local']
        if len(matches) != 1:
            raise ValueError('exactly one source-bound local baseline required')
        record = matches[0]
        _, source_hash, vertices, faces, baseline = bound_run(cohort, record)
        initial = baseline['labels']
        evidence = [(int(a),int(b),int(h),float(c)) for a,b,h,c,_ in (line.split() for line in Path(record['command'][2]+'.edges').read_text().splitlines())]
    graph = prepare(vertices, faces, config, evidence)
    result = run(graph, initial, args.ablation != 'concavity_only', args.ablation != 'regional_only')
    result.update(config=config, config_sha256=digest(args.config), implementation_sha256=digest(__file__),
                  evaluator_sha256=digest(Path(__file__).with_name('evaluate_part_seams.py')), input_sha256=source_hash,
                  invocation=sys.argv, geometry={'positions':graph['vertices'],'triangles':graph['faces']})
    args.output.mkdir(parents=True)
    (args.output/'result.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('regions','area_fractions','initial_energy','final_energy','accepted_splits')}))


if __name__ == '__main__':
    main()
