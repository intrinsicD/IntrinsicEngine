#!/usr/bin/env python3
"""Offline classical atlas reference: geodesic atoms and UV-validated unions.

No engine backend or production dependency. NumPy/SciPy provide double-precision
reference numerics; timings are not native-engine performance claims.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import heapq
import json
from pathlib import Path
import time
import warnings

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import MatrixRankWarning, spsolve
from scipy.sparse.csgraph import connected_components


def read_obj(path):
    """Read positions and source-index triangles; reject implicit mesh repair."""
    vertices, faces = [], []
    for line in Path(path).read_text().splitlines():
        words = line.split('#', 1)[0].split()
        if not words:
            continue
        if words[0] == 'v':
            vertices.append([float(x) for x in words[1:4]])
        elif words[0] == 'f':
            ids = [int(x.split('/')[0]) for x in words[1:]]
            ids = [x - 1 if x > 0 else len(vertices) + x for x in ids]
            if len(ids) != 3:
                raise ValueError('triangle OBJ required; triangulation must be explicit')
            faces.append(ids)
    return np.asarray(vertices, float), np.asarray(faces, np.int64)


def geometry(vertices, faces):
    v, f = np.asarray(vertices, float), np.asarray(faces, np.int64)
    if v.ndim != 2 or v.shape[1] != 3 or f.ndim != 2 or f.shape[1] != 3 or not len(f):
        raise ValueError('nonempty indexed triangles required')
    if not np.isfinite(v).all() or f.min() < 0 or f.max() >= len(v):
        raise ValueError('invalid positions or indices')
    v = v - v.mean(axis=0)
    cross = np.cross(v[f[:, 1]]-v[f[:, 0]], v[f[:, 2]]-v[f[:, 0]])
    twice = np.linalg.norm(cross, axis=1)
    if np.any(twice <= np.max(twice)*1e-14):
        raise ValueError('degenerate triangle')
    scale = np.sqrt(twice.sum()/2)
    v = v/scale
    area = twice/(2*scale**2)
    normal = cross/twice[:, None]
    center = v[f].mean(axis=1)
    incidence = {}
    for i, face in enumerate(f):
        for a, b in zip(face, np.roll(face, -1)):
            incidence.setdefault(tuple(sorted((int(a), int(b)))), []).append((i, int(a), int(b)))
    adjacent = [[] for _ in f]
    edges = []
    for (a, b), entries in sorted(incidence.items()):
        if len(entries) > 2:
            raise ValueError('nonmanifold edge')
        if len(entries) == 2:
            (i, x, y), (j, z, w) = entries
            if (x, y) != (w, z):
                raise ValueError('inconsistent orientation')
            length = float(np.linalg.norm(v[a]-v[b]))
            distance = float(np.linalg.norm(center[i]-center[j]))
            angle = float(np.arccos(np.clip(normal[i]@normal[j], -1, 1)))
            adjacent[i].append((j, distance, angle))
            adjacent[j].append((i, distance, angle))
            edges.append((i, j, length, angle, distance, a, b))
    return dict(v=v, f=f, area=area, normal=normal, center=center,
                adjacent=adjacent, edges=edges, scale=float(scale))


def propagate(g, seeds, weight=0., radius=1., allowed=None, distances=None):
    """Deterministic Dijkstra; labels are connected by their shortest-path tree."""
    n = len(g['f'])
    d = np.full(n, np.inf) if distances is None else distances.copy()
    labels = np.full(n, -1, np.int64)
    queue = []
    for label, seed in enumerate(seeds):
        d[seed] = 0.
        labels[seed] = label
        heapq.heappush(queue, (0., label, int(seed)))
    while queue:
        dist, label, i = heapq.heappop(queue)
        if dist != d[i] or label != labels[i]:
            continue
        for j, length, angle in g['adjacent'][i]:
            if allowed is not None and j not in allowed:
                continue
            candidate = dist + length + weight*radius*angle
            if candidate < d[j]:
                d[j], labels[j] = candidate, label
                heapq.heappush(queue, (candidate, label, j))
    return d, labels


def seeds_for(g, count):
    # Incremental farthest-point updates visit only nodes whose distance improves.
    seeds = [int(np.argmax(g['area']))]
    d, _ = propagate(g, seeds)
    for _ in range(min(count, len(d))-1):
        seed = int(np.argmax(d))
        if d[seed] == 0:
            break
        seeds.append(seed)
        d, _ = propagate(g, [seed], distances=d)
    # Every disconnected component must have a seed even beyond the target count.
    while not np.isfinite(d).all():
        seed = int(np.flatnonzero(~np.isfinite(d))[0])
        seeds.append(seed)
        d, _ = propagate(g, [seed], distances=d)
    return seeds


def disk_boundary(faces):
    """Require a connected oriented manifold disk, including vertex links."""
    incidence, vertex_faces = {}, {}
    for i, face in enumerate(faces):
        for v in face:
            vertex_faces.setdefault(int(v), []).append(i)
        for a, b in zip(face, np.roll(face, -1)):
            incidence.setdefault(tuple(sorted((int(a), int(b)))), []).append((i, int(a), int(b)))
    face_adj = [[] for _ in faces]
    next_vertex = {}
    vertex_adj = {v: {} for v in vertex_faces}
    for (a, b), entries in incidence.items():
        if len(entries) == 1:
            _, x, y = entries[0]
            if x in next_vertex:
                return None
            next_vertex[x] = y
        elif len(entries) == 2:
            (i, x, y), (j, z, w) = entries
            if (x, y) != (w, z):
                return None
            face_adj[i].append(j); face_adj[j].append(i)
            for v in (a, b):
                vertex_adj[v].setdefault(i, []).append(j)
                vertex_adj[v].setdefault(j, []).append(i)
        else:
            return None
    if len(vertex_faces)-len(incidence)+len(faces) != 1 or not next_vertex:
        return None
    def connected(ids, adj):
        visited = {ids[0]}; stack = [ids[0]]
        while stack:
            for j in adj[stack.pop()]:
                if j not in visited:
                    visited.add(j); stack.append(j)
        return len(visited) == len(ids)
    if not connected(list(range(len(faces))), face_adj):
        return None
    for v, ids in vertex_faces.items():
        local = vertex_adj[v]
        if not connected(ids, {i: local.get(i, []) for i in ids}):
            return None
    boundary = [min(next_vertex)]
    while True:
        j = next_vertex.get(boundary[-1])
        if j == boundary[0]:
            break
        if j is None or j in boundary:
            return None
        boundary.append(j)
    if len(boundary) != len(next_vertex):
        return None
    return np.asarray(boundary, np.int64)


def cross2(a, b):
    return a[..., 0]*b[..., 1] - a[..., 1]*b[..., 0]


def disk_boundary_fast(faces):
    """Same disk predicate as the scalar reference, using corner-link components."""
    nfaces=len(faces)
    if not nfaces:
        return None
    vertices,flat=np.unique(faces,return_inverse=True)
    f=flat.reshape(-1,3); nvertices=len(vertices)
    directed=f[:,[0,1,1,2,2,0]].reshape(-1,2)
    edges=np.sort(directed,axis=1)
    unique,inverse,counts=np.unique(edges,axis=0,return_inverse=True,return_counts=True)
    if np.any(counts>2) or nvertices-len(unique)+nfaces!=1:
        return None
    order=np.argsort(inverse,kind='stable')
    starts=np.concatenate(([0],np.cumsum(counts)[:-1]))
    first=order[starts[counts==2]]; second=order[starts[counts==2]+1]
    if np.any(directed[first]!=directed[second,::-1]):
        return None
    def component_count(a,b,n):
        matrix=sparse.coo_matrix((np.ones(len(a),np.int8),(a,b)),shape=(n,n)).tocsr()
        return connected_components(matrix,directed=False,return_labels=False)
    if component_count(first//3,second//3,nfaces)!=1:
        return None
    first_end=first//3*3+(first%3+1)%3
    second_end=second//3*3+(second%3+1)%3
    # Each node is a face corner. Links cross a shared edge at the same vertex;
    # exactly V components means every vertex has one connected incident fan.
    if component_count(np.concatenate((first,first_end)),np.concatenate((second_end,second)),3*nfaces)!=nvertices:
        return None
    boundary=directed[order[starts[counts==1]]]
    if not len(boundary) or len(np.unique(boundary[:,0]))!=len(boundary):
        return None
    next_vertex=dict(boundary.tolist()); ring=[min(next_vertex)]; visited=set(ring)
    while True:
        j=next_vertex.get(ring[-1])
        if j==ring[0]:
            break
        if j is None or j in visited:
            return None
        ring.append(j); visited.add(j)
    if len(ring)!=len(boundary):
        return None
    return vertices[np.asarray(ring)]


def simple_boundary(points, epsilon=1e-10):
    """Reject nonadjacent crossings or contacts, including collinear overlap."""
    scale = max(float(np.ptp(points, axis=0).max()), np.finfo(float).tiny)
    p = (points-points[0])/scale
    q = np.roll(p, -1, axis=0)
    if np.any(np.linalg.norm(q-p, axis=1) <= epsilon):
        return False
    for i in range(len(p)-2):
        ids = np.arange(i+2, len(p)-(i == 0))
        a, b, c, d = p[i], q[i], p[ids], q[ids]
        boxes = np.all(np.maximum(np.minimum(a,b), np.minimum(c,d)) <=
                       np.minimum(np.maximum(a,b), np.maximum(c,d))+epsilon, axis=1)
        c, d = c[boxes], d[boxes]
        ab_c, ab_d = cross2(b-a,c-a), cross2(b-a,d-a)
        cd_a, cd_b = cross2(d-c,a-c), cross2(d-c,b-c)
        if np.any((np.minimum(ab_c,ab_d) <= epsilon) & (np.maximum(ab_c,ab_d) >= -epsilon) &
                  (np.minimum(cd_a,cd_b) <= epsilon) & (np.maximum(cd_a,cd_b) >= -epsilon)):
            return False
    return True


def triangle_frames(v, f):
    e1, e2 = v[f[:, 1]]-v[f[:, 0]], v[f[:, 2]]-v[f[:, 0]]
    length = np.linalg.norm(e1, axis=1)
    x = np.einsum('ij,ij->i', e1, e2)/length
    y = np.linalg.norm(np.cross(e1,e2), axis=1)/length
    return length, x, y


def measure_uv(v, f, uv):
    """Per-chart area normalization removes packing/global-scale choices."""
    length, x, y = triangle_frames(v,f)
    q1, q2 = uv[f[:,1]]-uv[f[:,0]], uv[f[:,2]]-uv[f[:,0]]
    signed = cross2(q1,q2)
    if not np.isfinite(uv).all() or np.any(signed <= 1e-13*length*y):
        return None
    factor = np.sqrt(np.sum(length*y)/np.sum(signed))
    uv = uv*factor
    a = q1/length[:,None]*factor
    b = (q2-q1*(x/length)[:,None])/y[:,None]*factor
    jacobian = np.stack((a,b),axis=2)
    singular = np.linalg.svd(jacobian,compute_uv=False)
    if np.any(singular[:,1] <= 0) or not np.isfinite(singular).all():
        return None
    stretch = np.maximum(singular[:,0],1/singular[:,1])
    anisotropy = singular[:,0]/singular[:,1]
    return dict(uv=uv, stretch=stretch, anisotropy=anisotropy,
                max_stretch=float(stretch.max()), max_anisotropy=float(anisotropy.max()))


def parameterize(g, ids, stretch_limit=1.5, anisotropy_limit=2.):
    ids = np.asarray(sorted(ids),np.int64)
    vertices, inverse = np.unique(g['f'][ids],return_inverse=True)
    return parameterize_local(g, ids, vertices, inverse.reshape(-1,3),
                              stretch_limit, anisotropy_limit)


def parameterize_local(g, ids, vertices, f, stretch_limit=1.5, anisotropy_limit=2.):
    """Solve an explicit chart, allowing duplicated source vertices at UV cuts."""
    v = g['v'][vertices]
    boundary = disk_boundary_fast(f)
    if boundary is None:
        return None, 'topology'
    length, x, y = triangle_frames(v,f)
    # Weighted Cauchy-Riemann least squares (LSCM), with two separated boundary pins.
    gx = np.column_stack((-y,y,np.zeros(len(f))))/(length*y)[:,None]
    gy = np.column_stack((x-length,-x,length))/(length*y)[:,None]
    w = np.sqrt(length*y/2)[:,None]
    m, n = len(f), len(v)
    rows = np.repeat(np.arange(2*m),6)
    cols = np.concatenate((np.column_stack((f,f+n)), np.column_stack((f,f+n)))).ravel()
    values = np.concatenate((np.column_stack((gx*w,-gy*w)),np.column_stack((gy*w,gx*w)))).ravel()
    matrix = sparse.coo_matrix((values,(rows,cols)),shape=(2*m,2*n)).tocsr()
    p = int(boundary[np.argmax(np.linalg.norm(v[boundary]-v[boundary[0]],axis=1))])
    q = int(boundary[np.argmax(np.linalg.norm(v[boundary]-v[p],axis=1))])
    pins = np.array([p,q,p+n,q+n]); fixed = np.array([0.,np.linalg.norm(v[q]-v[p]),0.,0.])
    free = np.setdiff1d(np.arange(2*n),pins)
    a = matrix[:,free]; rhs = -matrix[:,pins]@fixed
    g['actual_uv_solves']=g.get('actual_uv_solves',0)+1
    g['factorized_degrees_of_freedom']=g.get('factorized_degrees_of_freedom',0)+len(free)
    with warnings.catch_warnings():
        warnings.simplefilter('error',MatrixRankWarning)
        try:
            solution = spsolve((a.T@a).tocsc(), a.T@rhs)
        except (MatrixRankWarning, RuntimeError):
            return None, 'solver'
    flat = np.zeros(2*n); flat[pins] = fixed; flat[free] = solution
    measured = measure_uv(v,f,flat.reshape(2,n).T)
    if measured is None:
        return None, 'orientation'
    if measured['max_stretch'] > stretch_limit or measured['max_anisotropy'] > anisotropy_limit:
        return None, 'distortion'
    if not simple_boundary(measured['uv'][boundary]):
        return None, 'boundary_overlap'
    # A continuous positive map of a manifold disk with a simple boundary is injective.
    measured.update(ids=ids, vertices=vertices, local_faces=f, boundary=boundary)
    return measured, 'accepted'


def bisect(g, ids):
    allowed = set(ids)
    if len(ids) < 2:
        raise ValueError('invalid single triangle cannot be repaired')
    d, _ = propagate(g,[min(ids)],allowed=allowed)
    a = max(ids,key=lambda i:(d[i],-i))
    d, _ = propagate(g,[a],allowed=allowed)
    b = max(ids,key=lambda i:(d[i],-i))
    _, labels = propagate(g,[a,b],allowed=allowed)
    # Separate any disconnected components explicitly, rather than dropping faces.
    groups = {}
    for i in ids:
        groups.setdefault(int(labels[i]),[]).append(i)
    if len(groups) < 2:
        raise ValueError('bisection made no progress')
    return list(groups.values())


def pack(charts):
    """Deterministic common shelf packer; no raster-padding guarantee is claimed."""
    boxes, local_uv = {}, {}
    for i,c in charts.items():
        uv = c['uv'].copy()
        _, axes = np.linalg.eigh(np.cov(uv.T))
        uv = uv@axes
        # Orthogonal reorientation must preserve winding.
        if np.linalg.det(axes) < 0:
            uv[:,0] *= -1
        uv -= uv.min(axis=0)
        extent = uv.max(axis=0)
        if extent[0] > extent[1]:
            uv = np.column_stack((uv[:,1], extent[0]-uv[:,0]))
        local_uv[i] = uv
        boxes[i] = uv.max(axis=0)
    total = sum(float(np.prod(x)) for x in boxes.values())
    gap = np.sqrt(total)*0.002
    best = None
    for factor in (.7,1.,1.3,1.7,2.2):
        width = max(max(x[0] for x in boxes.values())+2*gap,np.sqrt(total)*factor)
        x, y, height = gap, gap, 0.; placements = {}
        for i in sorted(boxes,key=lambda i:(-boxes[i][1],i)):
            w,h = boxes[i]
            if x+w+gap > width:
                x,y,height = gap,y+height+gap,0.
            placements[i] = np.array([x,y]); x += w+gap; height = max(height,h)
        atlas_height = y+height+gap
        if best is None or width*atlas_height < best[0]:
            best = (width*atlas_height,width,atlas_height,placements)
    _,width,height,placements = best
    scale = max(width,height)
    packed = {i:(local_uv[i]+placements[i])/scale for i in charts}
    occupied = sum(np.sum(cross2(c['uv'][c['local_faces'][:,1]]-c['uv'][c['local_faces'][:,0]],
                                c['uv'][c['local_faces'][:,2]]-c['uv'][c['local_faces'][:,0]]))/2 for c in charts.values())
    return packed, float(occupied/(width*height))


def execute(vertices, faces, *, patches=64, feature_weight=2., stretch_limit=1.5,
            anisotropy_limit=2., merge=True, max_merges=None, merge_feature_weight=None):
    if type(patches) is not int or patches<2 or not np.isfinite([feature_weight,stretch_limit,anisotropy_limit]).all() or feature_weight<0 or stretch_limit<1 or anisotropy_limit<1:
        raise ValueError('invalid experiment parameters')
    rank_weight=feature_weight if merge_feature_weight is None else merge_feature_weight
    if not np.isfinite(rank_weight) or rank_weight<0:
        raise ValueError('invalid merge feature weight')
    started = time.perf_counter(); g = geometry(vertices,faces)
    prepared = time.perf_counter()
    seeds = seeds_for(g,patches)
    _, labels = propagate(g,seeds,feature_weight,1/np.sqrt(patches))
    clustered = time.perf_counter()
    charts, rejects = {}, Counter(); solves = 0; next_id = 0
    stack = [np.flatnonzero(labels==i).tolist() for i in range(len(seeds))]
    while stack:
        ids = stack.pop()
        if not ids:
            continue
        chart,reason = parameterize(g,ids,stretch_limit,anisotropy_limit); solves += 1
        if chart is None:
            rejects['initial_'+reason] += 1
            stack.extend(bisect(g,ids))
        else:
            charts[next_id] = chart; next_id += 1
    validated = time.perf_counter(); initial_count = len(charts)
    labels[:] = -1
    for i,c in charts.items():
        labels[c['ids']] = i
    initial_labels = labels.copy()
    adjacent = {i:{} for i in charts}
    for f,h,length,angle,distance,_,_ in g['edges']:
        a,b = int(labels[f]),int(labels[h])
        if a != b:
            for x,y in ((a,b),(b,a)):
                old = adjacent[x].get(y,(0.,0.))
                adjacent[x][y] = (old[0]+length,old[1]+length*angle)
    queue = []
    def enqueue(a,b):
        if a > b:
            a,b = b,a
        length,turn = adjacent[a][b]
        small_area = min(g['area'][charts[a]['ids']].sum(),g['area'][charts[b]['ids']].sum())
        # Feature influence orders candidates, never forbids a low-distortion union.
        score = length/np.sqrt(small_area)/(1+rank_weight*turn/max(length,1e-15))
        heapq.heappush(queue,(-float(score),a,b))
    for a,neighbors in adjacent.items():
        for b in neighbors:
            if a < b:
                enqueue(a,b)
    attempts,accepted = 0,0
    while merge and queue and (max_merges is None or accepted < max_merges):
        _,a,b = heapq.heappop(queue)
        if a not in charts or b not in charts:
            continue
        attempts += 1
        chart,reason = parameterize(g,np.concatenate((charts[a]['ids'],charts[b]['ids'])),
                                    stretch_limit,anisotropy_limit); solves += 1
        if chart is None:
            rejects['merge_'+reason] += 1
            continue
        c = next_id; next_id += 1; accepted += 1
        charts[c] = chart; adjacent[c] = {}
        for old in (a,b):
            for neighbor,pair in adjacent[old].items():
                if neighbor in (a,b):
                    continue
                before = adjacent[c].get(neighbor,(0.,0.))
                adjacent[c][neighbor] = (before[0]+pair[0],before[1]+pair[1])
                adjacent[neighbor].pop(old,None)
            del charts[old]; del adjacent[old]
        for neighbor,pair in adjacent[c].items():
            adjacent[neighbor][c] = pair
            enqueue(c,neighbor)
    merged = time.perf_counter()
    packed,utilization = pack(charts)
    corner_uv = np.zeros((len(faces),3,2)); chart_uv = corner_uv.copy()
    stretches,anisotropies = np.zeros(len(faces)),np.zeros(len(faces))
    for label,(i,c) in enumerate(sorted(charts.items())):
        labels[c['ids']] = label
        corner_uv[c['ids']] = packed[i][c['local_faces']]
        chart_uv[c['ids']] = c['uv'][c['local_faces']]
        stretches[c['ids']] = c['stretch']; anisotropies[c['ids']] = c['anisotropy']
    seam = sum(length for a,b,length,*_ in g['edges'] if labels[a] != labels[b])
    ended = time.perf_counter()
    metrics = dict(input_vertices=len(vertices),input_faces=len(faces),initial_charts=initial_count,
                   chart_count=len(charts),merge_attempts=attempts,accepted_merges=accepted,
                   parameterization_attempts=solves,uv_solves=g.get('actual_uv_solves',0),
                   factorized_degrees_of_freedom=g.get('factorized_degrees_of_freedom',0),
                   rejections=dict(rejects),max_stretch=float(stretches.max()),
                   mean_stretch=float(g['area']@stretches),max_anisotropy=float(anisotropies.max()),
                   mean_anisotropy=float(g['area']@anisotropies),seam_length_over_sqrt_area=float(seam),
                   continuous_packing_utilization=utilization,all_charts_valid=True,
                   runtime_seconds=ended-started,
                   phases_seconds=dict(geometry=prepared-started,clustering=clustered-prepared,
                                       initial_validation=validated-clustered,merging=merged-validated,packing=ended-merged))
    return dict(metrics=metrics,labels=labels,initial_labels=initial_labels,corner_uv=corner_uv,
                chart_uv=chart_uv,stretch=stretches,anisotropy=anisotropies)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mesh',type=Path); parser.add_argument('output',type=Path)
    parser.add_argument('--patches',type=int,default=64)
    parser.add_argument('--feature-weight',type=float,default=2.)
    parser.add_argument('--stretch-limit',type=float,default=1.5)
    parser.add_argument('--anisotropy-limit',type=float,default=2.)
    parser.add_argument('--no-merge',action='store_true')
    args = parser.parse_args()
    if args.patches < 2 or not np.isfinite([args.feature_weight,args.stretch_limit,args.anisotropy_limit]).all() or args.feature_weight < 0 or args.stretch_limit < 1 or args.anisotropy_limit < 1:
        parser.error('invalid experiment parameters')
    v,f = read_obj(args.mesh)
    result = execute(v,f,patches=args.patches,feature_weight=args.feature_weight,
                     stretch_limit=args.stretch_limit,anisotropy_limit=args.anisotropy_limit,merge=not args.no_merge)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    np.savez_compressed(args.output.with_suffix('.npz'),vertices=v,faces=f,
                        **{k:x for k,x in result.items() if k!='metrics'})
    record = dict(schema='intrinsic.atlas-patch-merge.diagnostic.v1',source=str(args.mesh),
                  source_sha256=hashlib.sha256(args.mesh.read_bytes()).hexdigest(),
                  implementation='python_scipy_lscm_patch_merge',claim_eligible=False,
                  parameters={k:str(x) if isinstance(x,Path) else x for k,x in vars(args).items()},
                  metrics=result['metrics'])
    args.output.write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    print(json.dumps(result['metrics'],allow_nan=False),flush=True)


if __name__ == '__main__':
    main()
