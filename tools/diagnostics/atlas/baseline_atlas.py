#!/usr/bin/env python3
"""Offline UV charts preserving source-bound regions and their borders.

Connect boundary loops by shortest primal paths before LSCM. Unsupported or
distorted regions are bisected explicitly; semantic labels never become UV IDs.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import heapq
import json
from pathlib import Path

import numpy as np
import patch_merge as pm
from compare_atlases import audit, render


def incidence(faces):
    edges = {}
    for i, face in enumerate(faces):
        for k in range(3):
            a, b = int(face[k]), int(face[(k+1)%3])
            edges.setdefault(tuple(sorted((a, b))), []).append((i, k, a, b))
    return edges


def cut_corners(faces, edges, cuts):
    """Join corners across uncut edges and retain the source-vertex map."""
    parent = np.arange(faces.size)
    def root(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    def join(a, b):
        a, b = root(a), root(b)
        parent[max(a,b)] = min(a,b)
    for edge, entries in edges.items():
        if len(entries) == 2 and edge not in cuts:
            (i,k,a,b), (j,l,c,d) = entries
            if (a,b) != (d,c):
                raise ValueError('inconsistent orientation')
            join(3*i+k, 3*j+(l+1)%3)
            join(3*i+(k+1)%3, 3*j+l)
    roots = np.array([root(i) for i in range(faces.size)])
    unique, inverse = np.unique(roots, return_inverse=True)
    return faces.ravel()[unique], inverse.reshape(-1,3)


def topology(faces):
    edges = incidence(faces)
    if any(len(x)>2 for x in edges.values()):
        raise ValueError('nonmanifold edge')
    vertices, _ = cut_corners(faces, edges, set())
    if len(vertices) != len(np.unique(faces)):
        raise ValueError('nonmanifold vertex fan')
    outgoing, incoming = {}, {}
    for entries in edges.values():
        if len(entries) == 1:
            _,_,a,b = entries[0]
            if a in outgoing or b in incoming:
                raise ValueError('nonmanifold boundary')
            outgoing[a] = b; incoming[b] = a
    if outgoing.keys() != incoming.keys():
        raise ValueError('open boundary chain')
    loops, remaining = [], set(outgoing)
    while remaining:
        start = min(remaining); loop = []; current = start
        while current in remaining:
            remaining.remove(current); loop.append(current); current = outgoing[current]
        if current != start:
            raise ValueError('non-simple boundary loop')
        loops.append(loop)
    return edges, loops, len(vertices)-len(edges)+len(faces)


def open_region(g, ids):
    """Cut a connected genus-zero region with boundary; report other topology."""
    faces = g['f'][ids]
    try:
        edges, loops, euler = topology(faces)
    except ValueError as error:
        # A valid source can induce a pinched region boundary. Split the region
        # explicitly; never mistake corner duplication for source repair.
        return None, 'region_topology', dict(euler_characteristic=None,
                                            boundary_loops=None, cut_edges=0,
                                            topology_failure=str(error))
    diagnostics = dict(euler_characteristic=euler, boundary_loops=len(loops), cut_edges=0)
    if not loops or euler != 2-len(loops):
        return None, 'unsupported_topology', diagnostics
    adjacent = {int(v): [] for v in np.unique(faces)}
    for a,b in edges:
        length = float(np.linalg.norm(g['v'][a]-g['v'][b]))
        adjacent[a].append((b,length)); adjacent[b].append((a,length))
    connected = set(loops[0]); waiting = [set(x) for x in loops[1:]]; cuts = set()
    while waiting:
        targets = set().union(*waiting)
        distance = {v:0. for v in connected}; previous = {}
        queue = [(0.,v) for v in sorted(connected)]; heapq.heapify(queue)
        hit = None
        while queue:
            d,v = heapq.heappop(queue)
            if d != distance[v]:
                continue
            if v in targets:
                hit = v; break
            for w,length in sorted(adjacent[v]):
                value = d+length
                if value < distance.get(w, np.inf):
                    distance[w] = value; previous[w] = v; heapq.heappush(queue,(value,w))
        if hit is None:
            return None, 'disconnected_region', diagnostics
        current = hit
        while current not in connected:
            parent = previous[current]
            cuts.add(tuple(sorted((current,parent)))); connected.add(current); current = parent
        reached = next(i for i,loop in enumerate(waiting) if hit in loop)
        connected.update(waiting.pop(reached))
    vertices, local = cut_corners(faces, edges, cuts)
    diagnostics['cut_edges'] = len(cuts)
    if pm.disk_boundary_fast(local) is None:
        return None, 'cut_topology', diagnostics
    return (vertices, local), 'accepted', diagnostics


def region_components(g, labels):
    visited = np.zeros(len(labels),bool); result = []
    for seed in range(len(labels)):
        if visited[seed]:
            continue
        stack = [seed]; visited[seed] = True; ids = []
        while stack:
            i = stack.pop(); ids.append(i)
            for j,*_ in g['adjacent'][i]:
                if not visited[j] and labels[j] == labels[i]:
                    visited[j] = True; stack.append(j)
        result.append(np.asarray(sorted(ids),np.int64))
    return result


def boundary_metrics(g, regions, labels, corners):
    retained = lost = extra = internal = 0
    total = baseline_length = internal_length = 0.
    for a,b,length,_,_,v,w in g['edges']:
        cut = labels[a] != labels[b]
        if not cut:
            for vertex in (v,w):
                ia = int(np.flatnonzero(g['f'][a] == vertex)[0])
                ib = int(np.flatnonzero(g['f'][b] == vertex)[0])
                cut = cut or not np.array_equal(corners[a,ia],corners[b,ib])
        protected = regions[a] != regions[b]
        retained += int(protected and cut); lost += int(protected and not cut)
        extra += int(not protected and labels[a] != labels[b])
        internal += int(cut and labels[a] == labels[b])
        total += length if cut else 0.
        baseline_length += length if protected else 0.
        internal_length += length if cut and labels[a] == labels[b] else 0.
    return dict(retained_baseline_edges=retained, lost_baseline_edges=lost,
                extra_chart_boundary_edges=extra, internal_uv_cut_edges=internal,
                total_uv_seam_length_over_sqrt_area=total,
                baseline_seam_length_over_sqrt_area=baseline_length,
                internal_uv_seam_length_over_sqrt_area=internal_length)


def merge_within_regions(g, regions, charts, stretch_limit, anisotropy_limit):
    """Rejoin only subdivisions of the same region, revalidating each union."""
    rejected = set(); attempts = []; next_id = len(charts)
    while True:
        labels = np.empty(len(regions),np.int64)
        for i,c in charts.items():
            labels[c['ids']] = i
        seams = Counter()
        for a,b,length,*_ in g['edges']:
            i,j = sorted((int(labels[a]),int(labels[b])))
            if i!=j and regions[a]==regions[b] and (i,j) not in rejected:
                seams[i,j] += length
        candidates = []
        for (i,j),length in seams.items():
            area = min(g['area'][charts[i]['ids']].sum(),g['area'][charts[j]['ids']].sum())
            candidates.append((-length/np.sqrt(area),i,j))
        accepted = False
        for _,i,j in sorted(candidates):
            ids = np.sort(np.concatenate((charts[i]['ids'],charts[j]['ids'])))
            opened,reason,topo = open_region(g,ids); chart = None
            if opened is not None:
                chart,reason = pm.parameterize_local(g,ids,*opened,stretch_limit,anisotropy_limit)
            attempts.append(dict(region=int(regions[ids[0]]),faces=len(ids),status=reason,**topo))
            if chart is None:
                rejected.add((i,j)); continue
            del charts[i]; del charts[j]; charts[next_id] = chart; next_id += 1
            accepted = True; break
        if not accepted:
            return charts, attempts


def execute(vertices, faces, regions, *, stretch_limit=1.35, anisotropy_limit=2., merge=False):
    regions = np.asarray(regions)
    if regions.shape != (len(faces),) or regions.dtype.kind not in 'iu' or np.any(regions<0):
        raise ValueError('one nonnegative integer baseline label per face required')
    if not np.isfinite([stretch_limit,anisotropy_limit]).all() or min(stretch_limit,anisotropy_limit)<1:
        raise ValueError('invalid distortion limits')
    g = pm.geometry(vertices,faces)
    # Validate the source before duplication could hide a pinched vertex.
    topology(g['f'])
    components = region_components(g,regions)
    pending = list(reversed(components)); charts = {}; records = []; splits = Counter()
    while pending:
        ids = np.asarray(sorted(pending.pop()),np.int64)
        opened, reason, topo = open_region(g,ids)
        chart = None
        if opened is not None:
            chart, reason = pm.parameterize_local(g,ids,*opened,stretch_limit,anisotropy_limit)
        records.append(dict(region=int(regions[ids[0]]),faces=len(ids),status=reason,**topo))
        if chart is None:
            splits[reason] += 1
            pending.extend(reversed(pm.bisect(g,ids)))
        else:
            charts[len(charts)] = chart
    initial_count = len(charts); merges = []
    if merge:
        charts, merges = merge_within_regions(g,regions,charts,stretch_limit,anisotropy_limit)
    labels = np.empty(len(faces),np.int64); chart_uv = np.empty((len(faces),3,2))
    corners = np.empty_like(chart_uv); packed,_ = pm.pack(charts)
    for i,c in charts.items():
        labels[c['ids']] = i
        chart_uv[c['ids']] = c['uv'][c['local_faces']]
        corners[c['ids']] = packed[i][c['local_faces']]
    metrics = audit(vertices,faces,labels,corners)
    metrics.update(boundary_metrics(g,regions,labels,corners))
    metrics.update(region_count=len(np.unique(regions)), region_component_count=len(components),
                   split_reasons=dict(splits), uv_solves=g.get('actual_uv_solves',0),
                   initial_charts=initial_count, accepted_merges=initial_count-len(charts),
                   merge_attempts=merges,
                   outcome='protected_with_chart_splits' if splits else 'protected_without_chart_splits')
    if (not metrics['all_charts_valid'] or metrics['lost_baseline_edges'] or
            metrics['max_stretch'] > stretch_limit or metrics['max_anisotropy'] > anisotropy_limit):
        raise ValueError('final source/UV audit failed: '+str(metrics))
    return dict(labels=labels,region_labels=regions.copy(),chart_uv=chart_uv,corner_uv=corners,
                metrics=metrics,component_attempts=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path,help='native export prefix (.geometry.json and .labels)')
    parser.add_argument('output',type=Path)
    parser.add_argument('--stretch-limit',type=float,default=1.35)
    parser.add_argument('--merge',action='store_true',help='rejoin valid subdivisions inside each region')
    args = parser.parse_args()
    geometry = args.baseline.with_suffix('.geometry.json'); labels = args.baseline.with_suffix('.labels')
    source = json.loads(geometry.read_text()); v = np.asarray(source['positions']); f = np.asarray(source['triangles'])
    regions = np.loadtxt(labels,dtype=np.int64,ndmin=1)
    result = execute(v,f,regions,stretch_limit=args.stretch_limit,merge=args.merge)
    record = dict(schema='intrinsic.baseline-atlas.diagnostic.v1',claim_eligible=False,
                  implementation='python_baseline_boundary_cuts_lscm',stretch_limit=args.stretch_limit,merge=args.merge,
                  source_sha256=hashlib.sha256(geometry.read_bytes()).hexdigest(),
                  baseline_labels_sha256=hashlib.sha256(labels.read_bytes()).hexdigest(),
                  implementation_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  metrics=result['metrics'],component_attempts=result['component_attempts'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.with_suffix('.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    arrays = {k:x for k,x in result.items() if k not in ('metrics','component_attempts')}
    np.savez_compressed(args.output.with_suffix('.npz'),vertices=v,faces=f,**arrays)
    render(v,f,result,args.output.with_suffix('.png'),f'{args.baseline.name} · protected regions · {record["metrics"]["chart_count"]} UV charts')
    print(json.dumps(record,allow_nan=False),flush=True)


if __name__ == '__main__':
    main()
