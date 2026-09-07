#!/usr/bin/env python3
"""Offline shape-diameter reference and bounded parts experiments; no engine backend."""
from __future__ import annotations
import argparse
import heapq
import json
import math
from pathlib import Path
import statistics
import sys

from evaluate_part_seams import bound_run, digest, mesh_geometry, read_json
from neck_sweep import cross, dot, sub, revolution


def unit(vector):
    length=math.hypot(*vector)
    if not math.isfinite(length) or length<=0:
        raise ValueError('nonzero finite direction required')
    return [x/length for x in vector]


def geometry(vertices, faces):
    vertices,faces,areas,incidence=mesh_geometry(vertices,faces)
    if any(len(v)!=2 for v in incidence.values()):
        raise ValueError('closed edge-manifold triangle surface required')
    directed={}
    for face in faces:
        for a,b in zip(face,[face[1],face[2],face[0]]):
            directed.setdefault(tuple(sorted((a,b))),[]).append((a,b))
    if any(es[0]!=es[1][::-1] for es in directed.values()):
        raise ValueError('consistent orientation required')
    origin=vertices[0]
    volume6=math.fsum(dot(sub(vertices[a],origin),cross(sub(vertices[b],origin),sub(vertices[c],origin))) for a,b,c in faces)
    if volume6<=0:
        raise ValueError('outward orientation required; no automatic repair')
    scale=math.sqrt(math.fsum(areas))
    positions=[[x/scale for x in sub(v,origin)] for v in vertices]
    centers=[[math.fsum(positions[i][k] for i in face)/3 for k in range(3)] for face in faces]
    normals=[unit(cross(sub(positions[b],positions[a]),sub(positions[c],positions[a]))) for a,b,c in faces]
    adjacent=[[] for _ in faces]
    edges=[]
    for (a,b),(f,g) in sorted(incidence.items()):
        length=math.dist(positions[a],positions[b])
        adjacent[f].append(g);adjacent[g].append(f)
        edges.append((f,g,length,a,b))
    visited={0};stack=[0]
    while stack:
        for other in adjacent[stack.pop()]:
            if other not in visited:
                visited.add(other);stack.append(other)
    if len(visited)!=len(faces):
        raise ValueError('one connected surface required')
    return {'positions':positions,'faces':faces,'centers':centers,'normals':normals,'areas':[a/scale**2 for a in areas],
            'adjacent':adjacent,'edges':edges,'scale':scale,'original_positions':vertices}


class RayTree:
    """Median-split triangle BVH; nearest hit is chosen before validity filtering."""
    def __init__(self, positions, faces, leaf_size=8):
        self.triangles=[(positions[a],sub(positions[b],positions[a]),sub(positions[c],positions[a])) for a,b,c in faces]
        lower=[[min(positions[i][k] for i in f) for k in range(3)] for f in faces]
        upper=[[max(positions[i][k] for i in f) for k in range(3)] for f in faces]
        centers=[[(a+b)/2 for a,b in zip(lo,hi)] for lo,hi in zip(lower,upper)]
        self.nodes=[]
        def build(ids):
            lo=[min(lower[i][k] for i in ids) for k in range(3)]
            hi=[max(upper[i][k] for i in ids) for k in range(3)]
            index=len(self.nodes);self.nodes.append(None)
            if len(ids)<=leaf_size:
                self.nodes[index]=(lo,hi,-1,-1,ids)
            else:
                axis=max(range(3),key=lambda k:hi[k]-lo[k])
                ids.sort(key=lambda i:(centers[i][axis],i));mid=len(ids)//2
                left,right=build(ids[:mid]),build(ids[mid:])
                self.nodes[index]=(lo,hi,left,right,[])
            return index
        build(list(range(len(faces))))

    @staticmethod
    def triangle(origin, direction, triangle, t_min, t_max):
        a,e1,e2=triangle
        h=cross(direction,e2);det=dot(e1,h)
        if abs(det)<=1e-14*math.sqrt(dot(e1,e1)*dot(e2,e2)):
            return None
        s=sub(origin,a);u=dot(s,h)/det
        if u < -1e-10 or u > 1+1e-10:
            return None
        q=cross(s,e1);v=dot(direction,q)/det
        if v < -1e-10 or u+v > 1+1e-10:
            return None
        t=dot(e2,q)/det
        return t if t_min<t<=t_max else None

    @staticmethod
    def box(origin, inverse, lo, hi, limit):
        near,far=0.,limit
        for k in range(3):
            if inverse[k] is None:
                if not lo[k]<=origin[k]<=hi[k]:return None
            else:
                a,b=(lo[k]-origin[k])*inverse[k],(hi[k]-origin[k])*inverse[k]
                near=max(near,min(a,b));far=min(far,max(a,b))
                if far<near:return None
        return near

    def nearest(self, origin, direction, exclude=-1, epsilon=1e-9):
        inverse=[1/x if x!=0 else None for x in direction]
        stack=[(0.,0)];best=math.inf;hit=-1
        while stack:
            distance,index=stack.pop()
            if distance>best:continue
            lo,hi,left,right,ids=self.nodes[index]
            if self.box(origin,inverse,lo,hi,best) is None:continue
            if left<0:
                for face in ids:
                    if face==exclude:continue
                    t=self.triangle(origin,direction,self.triangles[face],epsilon,best)
                    if t is not None and (t<best or face<hit):best,hit=t,face
            else:
                children=[]
                for child in (left,right):
                    lo,hi,*_=self.nodes[child]
                    distance=self.box(origin,inverse,lo,hi,best)
                    if distance is not None:children.append((distance,child))
                stack.extend(sorted(children,reverse=True))
        return None if hit<0 else (best,hit)


def validate_config(config):
    if config.get('schema')!='intrinsic.shape-diameter-experiment.v1' or config.get('implementation')!='shape_diameter_cpu_diagnostic_v1':
        raise ValueError('unknown shape diameter experiment')
    if config.get('ray_layout')!='equal_solid_angle_fibonacci':raise ValueError('unknown ray layout')
    for key in ('ray_count','minimum_valid_rays'):
        if type(config.get(key)) is not int or not 1<=config[key]<=256:raise ValueError(f'invalid {key}')
    if config['minimum_valid_rays']>config['ray_count']:raise ValueError('minimum valid rays exceeds ray count')
    for key in ('cone_opening_degrees','ray_epsilon_over_sqrt_area'):
        if type(config.get(key)) not in (int,float) or not math.isfinite(config[key]):raise ValueError(f'invalid {key}')
    if not 0<config['cone_opening_degrees']<180 or not 0<config['ray_epsilon_over_sqrt_area']<1e-4:
        raise ValueError('invalid cone or ray epsilon')
    return config


def ray_layout(config):
    n=config['ray_count'];cap=math.cos(math.radians(config['cone_opening_degrees']/2))
    golden=math.pi*(3-math.sqrt(5))
    result=[]
    for i in range(n):
        cosine=1-(i+.5)/n*(1-cap);sine=math.sqrt(1-cosine*cosine);phi=i*golden
        result.append((cosine,sine*math.cos(phi),sine*math.sin(phi),1/math.acos(cosine)))
    return result


def measure(graph, config):
    validate_config(config)
    tree=RayTree(graph['positions'],graph['faces']);layout=ray_layout(config)
    values=[];valid_counts=[];inlier_counts=[];misses=0;normal_rejections=0
    eps=config['ray_epsilon_over_sqrt_area']
    for face,(center,normal) in enumerate(zip(graph['centers'],graph['normals'])):
        ids=graph['faces'][face]
        candidates=[sub(graph['positions'][b],graph['positions'][a]) for a,b in zip(ids,[ids[1],ids[2],ids[0]])]
        tangent=unit(max(candidates,key=lambda v:dot(v,v)));bitangent=unit(cross(normal,tangent))
        origin=[x-eps*n for x,n in zip(center,normal)]
        samples=[]
        for cosine,x,y,weight in layout:
            direction=[-cosine*normal[k]+x*tangent[k]+y*bitangent[k] for k in range(3)]
            hit=tree.nearest(origin,direction,exclude=face,epsilon=eps)
            if hit is None:misses+=1;continue
            distance,other=hit
            # Shapira's antipodal-normal rejection, after the first surface hit.
            if dot(normal,graph['normals'][other])>=0:normal_rejections+=1;continue
            samples.append((distance,weight))
        valid_counts.append(len(samples))
        if len(samples)<config['minimum_valid_rays']:
            values.append(None);inlier_counts.append(0);continue
        lengths=[s[0] for s in samples];median=statistics.median(lengths);sigma=statistics.pstdev(lengths)
        kept=[(d,w) for d,w in samples if abs(d-median)<=sigma+1e-14]
        if not kept:values.append(None);inlier_counts.append(0);continue
        values.append(math.fsum(d*w for d,w in kept)/math.fsum(w for _,w in kept));inlier_counts.append(len(kept))
    return {'values_over_sqrt_area':values,'valid_ray_counts':valid_counts,'inlier_ray_counts':inlier_counts,
            'ray_layout_cos_tangent_bitangent_weight':layout,'ray_misses':misses,'opposite_normal_rejections':normal_rejections,
            'supported_faces':sum(v is not None for v in values),'faces':len(values),
            'normal_policy':'first positive hit, then source-normal dot hit-normal < 0',
            'scale':'sqrt(total surface area)','physical_scale':graph['scale']}


def synthetic(kind, axial=32, radial=24, diagonal=0):
    if kind in ('sphere','groove','ridge'):
        return revolution('ellipsoid' if kind=='sphere' else kind,axial,radial,diagonal)
    vertices,faces=revolution('neck',axial,radial,diagonal,rho=.3,aspect=2.)
    if kind not in ('neck','asymmetric','bent'):raise ValueError('unknown synthetic shape')
    if kind in ('asymmetric','bent'):
        vertices=[[x,y*(1+.25*x/2)+( .25*(x/2)**2 if kind=='bent' else 0),z*(1+.25*x/2)] for x,y,z in vertices]
    return vertices,faces


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    group=parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--synthetic',choices=['sphere','groove','ridge','neck','asymmetric','bent'])
    group.add_argument('--cohort',type=Path)
    parser.add_argument('--mesh',default='frog');parser.add_argument('--axial',type=int,default=32)
    parser.add_argument('--radial',type=int,default=24);parser.add_argument('--diagonal',type=int,choices=[0,1],default=0)
    args=parser.parse_args()
    if args.output.exists():parser.error('fresh output directory required')
    config=validate_config(read_json(args.config));baseline=None;source_hash=None
    if args.synthetic:
        if args.axial<8 or args.radial<8:parser.error('at least eight axial/radial samples required')
        vertices,faces=synthetic(args.synthetic,args.axial,args.radial,args.diagonal)
    else:
        cohort=read_json(args.cohort)
        runs=[r for r in cohort['runs'] if r['mesh']==args.mesh and r['mode']=='local']
        if len(runs)!=1:raise ValueError('one native local baseline required')
        _,source_hash,vertices,faces,data=bound_run(cohort,runs[0]);baseline=data['labels']
    graph=geometry(vertices,faces);result=measure(graph,config)
    result.update(schema='intrinsic.shape-diameter-field.v1',implementation=config['implementation'],backend='offline_cpu_reference',
                  claim_eligible=False,config=config,config_sha256=digest(args.config),source_sha256=digest(__file__),
                  input_sha256=source_hash,geometry={'positions':vertices,'triangles':faces},baseline_labels=baseline,invocation=sys.argv)
    args.output.mkdir(parents=True)
    (args.output/'field.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    finite=[v for v in result['values_over_sqrt_area'] if v is not None]
    print(json.dumps({'supported_faces':result['supported_faces'],'faces':len(faces),'min':min(finite,default=None),
                      'median':statistics.median(finite) if finite else None,'max':max(finite,default=None),
                      'ray_misses':result['ray_misses'],'normal_rejections':result['opposite_normal_rejections']}))


if __name__=='__main__':main()
