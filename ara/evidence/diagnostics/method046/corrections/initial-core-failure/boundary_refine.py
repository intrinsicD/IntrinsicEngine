#!/usr/bin/env python3
"""Offline, feature-guided chart boundary moves with immutable source regions.

Binary cuts propose collective moves inside frozen bands. The existing cut/LSCM
reference validates both charts; acceptance includes internal UV seam cost.
"""
from __future__ import annotations
from collections import Counter, deque
import heapq

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import maximum_flow

import baseline_atlas as ba
import patch_merge as pm
from compare_atlases import audit


def binary_cut(count, edges, fixed):
    """Minimize a positive binary Potts energy, with fixed 0/1 terminals.

Integer capacity sum stays below 2**31 for scipy's flow implementation. The
caller rechecks the floating-point objective, including terms absent here.
"""
    total = sum(w for _, _, w in edges)
    if total <= 0 or not np.isfinite(total):
        raise ValueError('positive finite graph weights required')
    if any(w <= 0 or not np.isfinite(w) for _, _, w in edges):
        raise ValueError('positive finite graph weights required')
    capacity = [max(1, int(round(w / total * 10_000_000))) for _, _, w in edges]
    infinity = sum(capacity) + 1
    if infinity >= 2**30:
        raise ValueError('capacity budget exceeded')
    rows, cols, values = [], [], []
    for (a, b, _), w in zip(edges, capacity):
        rows.extend((a, b)); cols.extend((b, a)); values.extend((w, w))
    for i, label in sorted(fixed.items()):
        if label not in (0, 1):
            raise ValueError('binary terminal required')
        a, b = (count, i) if label == 0 else (i, count+1)
        rows.append(a); cols.append(b); values.append(infinity)
    graph = csr_matrix((np.asarray(values, np.int64), (rows, cols)), shape=(count+2, count+2))
    flow = maximum_flow(graph, count, count+1)
    residual = (graph-flow.flow).tocsr()
    reached = {count}; queue = [count]
    while queue:
        i = queue.pop()
        for k in range(residual.indptr[i], residual.indptr[i+1]):
            j = int(residual.indices[k])
            if residual.data[k] > 0 and j not in reached:
                reached.add(j); queue.append(j)
    labels = np.array([0 if i in reached else 1 for i in range(count)])
    if any(labels[i] != x for i, x in fixed.items()):
        raise ValueError('min cut violated fixed terminal')
    return labels


def curve_support(g, vertices, curves, scale=2):
    """Conservative dual-path proximity and ambient tangent agreement.

Paths start at the segment's source face and cannot jump between disconnected
surface sheets. Outside that face, center-path length is a distance proxy, not
an exact geodesic. A score is evidence strength, not an alignment oracle.
"""
    if (not np.array_equal(np.asarray(curves['triangles']), g['f']) or
            not np.array_equal(np.asarray(curves['positions'], np.float32),
                               np.asarray(vertices, np.float32))):
        raise ValueError('curve/source identity mismatch')
    if scale not in range(len(curves['scales'])):
        raise ValueError('unknown curve support scale')
    radius = curves['scales'][scale]['radius'] / g['scale']
    if not np.isfinite(radius) or radius <= 0:
        raise ValueError('positive curve radius required')
    center = np.asarray(vertices).mean(axis=0)
    points = (np.array([p['position'] for p in curves['points']])-center)/g['scale']
    mid = np.array([(g['v'][v]+g['v'][w])/2 for *_, v, w in g['edges']])
    tangent = np.array([(g['v'][w]-g['v'][v])/length for _, _, length, _, _, v, w in g['edges']])
    incident = [[] for _ in g['f']]
    for e, (a, b, *_) in enumerate(g['edges']):
        incident[a].append(e); incident[b].append(e)
    score = np.zeros(len(g['edges']))
    for segment in curves['segments']:
        if segment['scale'] != scale or segment['kind'] not in ('principal_ridge', 'principal_valley'):
            continue
        confidence = segment['confidence']
        if not np.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError('invalid curve confidence')
        p, q = points[segment['points']]; delta = q-p; length = np.linalg.norm(delta)
        if length <= 0:
            continue
        direction = delta/length; source = int(segment['face'])
        if not 0 <= source < len(g['f']):
            raise ValueError('curve source face out of range')
        def segment_distance(x):
            return float(np.linalg.norm(x-(p+np.clip((x-p)@direction, 0, length)*direction)))
        # Start with the in-face distance to the center. Edges on the source
        # face use their direct segment distance instead of a center detour.
        distance = {source: segment_distance(g['center'][source])}
        queue = [(distance[source], source)]
        while queue:
            d, face = heapq.heappop(queue)
            if d != distance[face]:
                continue
            for e in incident[face]:
                reach = (segment_distance(mid[e]) if face == source else
                         d+float(np.linalg.norm(mid[e]-g['center'][face])))
                value = confidence*abs(float(direction@tangent[e]))*max(0., 1-reach/radius)
                score[e] = max(score[e], value)
            if d > radius:
                continue
            for other, step, _ in g['adjacent'][face]:
                candidate = d+step
                if candidate < radius and candidate < distance.get(other, np.inf):
                    distance[other] = candidate; heapq.heappush(queue, (candidate, other))
    return score


def edge_evidence(g, rows):
    """Bind native hard flags and soft confidence by source vertex pair."""
    lookup = {tuple(sorted((int(r[0]), int(r[1])))): r for r in rows}
    hard = np.zeros(len(g['edges']), bool); soft = np.zeros(len(g['edges']))
    for e, (*_, a, b) in enumerate(g['edges']):
        if (a, b) not in lookup:
            raise ValueError('native edge evidence missing source edge')
        row = lookup[a, b]; hard[e] = bool(row[2]); soft[e] = row[3]
    if not np.isfinite(soft).all() or np.any((soft < 0) | (soft > 1)):
        raise ValueError('invalid native soft confidence')
    return hard, soft


def solve_chart(g, ids, edge_index, stretch_limit, anisotropy_limit):
    ids = np.sort(ids)
    allowed = set(ids); seen = {int(ids[0])}; queue = list(seen)
    while queue:
        for j, *_ in g['adjacent'][queue.pop()]:
            if j in allowed and j not in seen:
                seen.add(j); queue.append(j)
    if len(seen) != len(ids):
        return None, 'disconnected_chart'
    opened, reason, _ = ba.open_region(g, ids)
    if opened is None:
        return None, reason
    chart, reason = pm.parameterize_local(g, ids, *opened, stretch_limit, anisotropy_limit)
    if chart is None:
        return None, reason
    cuts = set(); original = ba.incidence(g['f'][ids])
    for edge, entries in original.items():
        if len(entries) == 2:
            (a, k, _, _), (b, l, _, _) = entries
            if chart['local_faces'][a, k] != chart['local_faces'][b, (l+1)%3]:
                cuts.add(edge_index[edge])
    chart['internal_edges'] = cuts
    return chart, 'accepted'


def seam_mask(g, labels, charts):
    seams = np.array([labels[a] != labels[b] for a, b, *_ in g['edges']])
    for chart in charts.values():
        if chart['internal_edges']:
            seams[list(chart['internal_edges'])] = True
    return seams


def frozen_bands(g, labels, regions, hard, hops):
    pairs = {}
    pinned = set()
    for e, (a, b, length, *_) in enumerate(g['edges']):
        if labels[a] != labels[b]:
            if hard[e]:
                pinned.update((a, b))
            if regions[a] == regions[b]:
                pair = tuple(sorted((int(labels[a]), int(labels[b]))))
                entry = pairs.setdefault(pair, dict(length=0., seeds=set()))
                entry['length'] += length; entry['seeds'].update((a, b))
    result = []
    for pair, entry in sorted(pairs.items(), key=lambda x: (-x[1]['length'], x[0])):
        distance = {i: 0 for i in entry['seeds']}; queue = deque(sorted(distance))
        while queue:
            i = queue.popleft()
            if distance[i] == hops:
                continue
            for j, *_ in g['adjacent'][i]:
                if labels[j] in pair and j not in distance:
                    distance[j] = distance[i]+1; queue.append(j)
        mutable = set(distance)-pinned
        # Tiny charts with no fixed interior do not get an artificial seed/core.
        cores = [set(np.flatnonzero(labels == x))-mutable for x in pair]
        result.append((pair, mutable if all(cores) else set()))
    return result


def execute(vertices, faces, regions, initial, support, *, hard=None, beta=.8,
            band_hops=6, rounds=3, stretch_limit=1.35, anisotropy_limit=2.):
    if (not np.isfinite([beta, stretch_limit, anisotropy_limit]).all() or
            not 0 <= beta <= .9 or min(stretch_limit, anisotropy_limit) < 1 or
            not isinstance(band_hops, int) or band_hops < 0 or
            not isinstance(rounds, int) or rounds < 0):
        raise ValueError('invalid refinement parameters')
    g = pm.geometry(vertices, faces); ba.topology(g['f'])
    regions = np.asarray(regions); labels = np.asarray(initial).copy()
    for array in (regions, labels):
        if array.shape != (len(faces),) or array.dtype.kind not in 'iu' or np.any(array < 0):
            raise ValueError('nonnegative integer face labels required')
    score = np.asarray(support, float)
    hard = np.zeros(len(score), bool) if hard is None else np.asarray(hard, bool)
    if (score.shape != (len(g['edges']),) or hard.shape != score.shape or
            not np.isfinite(score).all() or np.any((score < 0) | (score > 1))):
        raise ValueError('source-edge scores in [0,1] required')
    edge_index = {(v, w): e for e, (*_, v, w) in enumerate(g['edges'])}
    charts = {}
    for label in np.unique(labels):
        ids = np.flatnonzero(labels == label)
        if len(np.unique(regions[ids])) != 1:
            raise ValueError('initial chart crosses an original region')
        chart, reason = solve_chart(g, ids, edge_index, stretch_limit, anisotropy_limit)
        if chart is None:
            raise ValueError('initial chart invalid: '+reason)
        charts[int(label)] = chart
    weights = np.array([e[2] for e in g['edges']])*(1-beta*score)
    mask = seam_mask(g, labels, charts); energy = float(weights@mask)
    initial_energy = energy; initial_mask = mask.copy()
    stages = [labels.copy()]; events = []; rejected = set()
    revisions = {i: 0 for i in charts}; bands = frozen_bands(g, labels, regions, hard, band_hops)
    for round_id in range(rounds):
        accepted = 0
        for pair, mutable in bands:
            key = (*pair, *(revisions[i] for i in pair))
            if key in rejected:
                continue
            record = dict(round=round_id, pair=list(pair), revisions=[revisions[i] for i in pair])
            def reject(reason):
                record['status'] = reason; events.append(record); rejected.add(key)
            if not mutable:
                reject('no_fixed_core'); continue
            ids = np.flatnonzero(np.isin(labels, pair)); index = {int(v): i for i, v in enumerate(ids)}
            edges = [(index[a], index[b], weights[e]) for e, (a, b, *_) in enumerate(g['edges'])
                     if a in index and b in index]
            fixed = {i: int(labels[v] == pair[1]) for i, v in enumerate(ids) if v not in mutable}
            candidate = labels.copy()
            candidate[ids] = np.asarray(pair)[binary_cut(len(ids), edges, fixed)]
            record['moved_faces'] = int(np.count_nonzero(candidate != labels))
            border_delta = sum(weights[e]*((int(candidate[a] != candidate[b]))-
                                          int(labels[a] != labels[b]))
                               for e, (a, b, *_) in enumerate(g['edges']))
            record['border_delta'] = float(border_delta)
            if not record['moved_faces'] or border_delta >= -1e-10:
                reject('no_border_descent'); continue
            proposed = charts.copy(); reason = 'accepted'
            for label in pair:
                chart, reason = solve_chart(g, np.flatnonzero(candidate == label), edge_index,
                                            stretch_limit, anisotropy_limit)
                if chart is None:
                    break
                proposed[label] = chart
            if reason != 'accepted':
                reject(reason); continue
            new_mask = seam_mask(g, candidate, proposed); new_energy = float(weights@new_mask)
            record.update(energy_before=energy, energy_after=new_energy)
            if new_energy >= energy-1e-10:
                reject('internal_cut_energy'); continue
            charts = proposed; labels = candidate; mask = new_mask; energy = new_energy
            for label in pair:
                revisions[label] += 1
            record['status'] = 'accepted'; events.append(record); accepted += 1; stages.append(labels.copy())
        if not accepted:
            break
    packed, _ = pm.pack(charts); uv = np.empty((len(faces), 3, 2)); corners = np.empty_like(uv)
    for label, chart in charts.items():
        uv[chart['ids']] = chart['uv'][chart['local_faces']]
        corners[chart['ids']] = packed[label][chart['local_faces']]
    metrics = audit(vertices, faces, labels, corners)
    metrics.update(ba.boundary_metrics(g, regions, labels, corners))
    metrics.update(initial_energy=initial_energy, final_energy=energy,
                   accepted_moves=len(stages)-1, moved_faces=int(np.count_nonzero(labels != initial)),
                   statuses=dict(Counter(e['status'] for e in events)), uv_solves=g.get('actual_uv_solves', 0))
    if (not metrics['all_charts_valid'] or metrics['lost_baseline_edges'] or
            metrics['max_stretch'] > stretch_limit or metrics['max_anisotropy'] > anisotropy_limit or
            not np.array_equal(np.unique(labels), np.unique(initial)) or np.any(hard & initial_mask & ~mask)):
        raise ValueError('final refinement audit failed')
    return dict(labels=labels, region_labels=regions.copy(), chart_uv=uv, corner_uv=corners,
                metrics=metrics, events=events, stages=stages, initial_seams=initial_mask, final_seams=mask)
