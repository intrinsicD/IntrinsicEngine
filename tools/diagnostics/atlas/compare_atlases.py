#!/usr/bin/env python3
"""Reproducible offline comparisons and source-face UV inspection artifacts."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import numpy as np
import patch_merge as pm


def overlapping_triangles(triangles, epsilon=1e-10):
    """Independent positive-area SAT overlap test, including across chart labels.

    Sweep/AABB broad phase; shared boundaries are allowed, positive-area triangle
    intersection is not. This does not require disk topology or a boundary theorem.
    """
    triangles=np.asarray(triangles,float)
    triangles=(triangles-triangles.reshape(-1,2).min(axis=0))/max(float(np.ptp(triangles.reshape(-1,2),axis=0).max()),1e-30)
    lo,hi=triangles.min(axis=1),triangles.max(axis=1)
    order=np.argsort(lo[:,0],kind='stable'); lower=lo[order,0]
    collisions=[]
    for position,i in enumerate(order):
        end=np.searchsorted(lower,hi[i,0]-epsilon,side='left')
        ids=order[position+1:end]
        ids=ids[(lo[ids,1]<hi[i,1]-epsilon)&(hi[ids,1]>lo[i,1]+epsilon)]
        if not len(ids):
            continue
        a,b=triangles[i],triangles[ids]
        ea=np.roll(a,-1,axis=0)-a; eb=np.roll(b,-1,axis=1)-b
        edges=np.concatenate((np.broadcast_to(ea,eb.shape),eb),axis=1)
        axes=np.stack((-edges[:,:,1],edges[:,:,0]),axis=2)
        axes/=np.maximum(np.linalg.norm(axes,axis=2,keepdims=True),1e-30)
        pa=np.einsum('nki,ji->nkj',axes,a); pb=np.einsum('nki,nji->nkj',axes,b)
        gaps=np.minimum(pa.max(axis=2),pb.max(axis=2))-np.maximum(pa.min(axis=2),pb.min(axis=2))
        for j in ids[np.all(gaps>epsilon,axis=1)]:
            collisions.append((int(i),int(j)))
    return collisions


def audit(v, f, labels, corners):
    """Recompute stretch through the 2x2 metric tensor, not the solver's SVD."""
    g = pm.geometry(v,f)
    stretch = np.zeros(len(f)); anisotropy = stretch.copy()
    failures, disk_failures, reflections, scales = [], [], 0, []
    for label in np.unique(labels):
        ids = np.flatnonzero(labels==label)
        # Permit intentional cuts duplicating one source vertex inside a chart.
        keys, local, points, positions = {}, [], [], []
        for face,uvs in zip(f[ids],corners[ids]):
            indices = []
            for vertex,uv in zip(face,uvs):
                key = (int(vertex),float(uv[0]),float(uv[1]))
                if key not in keys:
                    keys[key] = len(points); points.append(uv); positions.append(g['v'][vertex])
                indices.append(keys[key])
            local.append(indices)
        local=np.asarray(local); points=np.asarray(points); positions=np.asarray(positions)
        boundary=pm.disk_boundary(local)
        if boundary is None:
            disk_failures.append(int(label))
        uv=points[local]
        q1,q2=uv[:,1]-uv[:,0],uv[:,2]-uv[:,0]
        signed=pm.cross2(q1,q2)
        if np.all(signed<0):
            points[:,0]*=-1; q1[:,0]*=-1; q2[:,0]*=-1; signed*=-1; reflections+=1
        if not np.isfinite(points).all() or np.any(signed<=0):
            failures.append([int(label),'orientation']); continue
        if boundary is not None and not pm.simple_boundary(points[boundary]):
            failures.append([int(label),'overlap']); continue
        length,x,y=pm.triangle_frames(positions,local)
        factor=np.sqrt(np.sum(length*y)/signed.sum())
        scales.append(float(1/factor))
        a=q1/length[:,None]*factor; b=(q2-q1*(x/length)[:,None])/y[:,None]*factor
        aa=np.einsum('ij,ij->i',a,a); bb=np.einsum('ij,ij->i',b,b); ab=np.einsum('ij,ij->i',a,b)
        delta=np.sqrt((aa-bb)**2+4*ab**2)
        hi=np.sqrt((aa+bb+delta)/2); lo=np.sqrt(np.maximum((aa+bb-delta)/2,0))
        if np.any(lo<=0):
            failures.append([int(label),'singular']); continue
        stretch[ids]=np.maximum(hi,1/lo); anisotropy[ids]=hi/lo
    overlaps=overlapping_triangles(corners)
    valid=not failures and not overlaps
    def percentile(values):
        order=np.argsort(values)
        return float(values[order[np.searchsorted(np.cumsum(g['area'][order]),.95)]])
    metrics=dict(chart_count=len(np.unique(labels)),invalid_charts=failures,
                 chart_texel_density_ratio=max(scales)/min(scales) if not failures else None,
                 non_disk_charts=disk_failures,positive_area_overlap_count=len(overlaps),
                 overlap_pairs_sample=overlaps[:8],
                 global_chart_reflections=reflections,all_charts_valid=valid,
                 max_stretch=float(stretch.max()) if not failures else None,
                 mean_stretch=float(g['area']@stretch) if not failures else None,
                 area_weighted_p95_stretch=percentile(stretch) if not failures else None,
                 max_anisotropy=float(anisotropy.max()) if not failures else None,
                 mean_anisotropy=float(g['area']@anisotropy) if not failures else None,
                 seam_length_over_sqrt_area=float(sum(length for a,b,length,*_ in g['edges'] if labels[a]!=labels[b])),
                 smallest_chart_area_fraction=float(min(g['area'][labels==x].sum() for x in np.unique(labels))))
    return metrics


def native(v,f,runner,directory,method='xatlas'):
    input_path=directory/'native-input.json'; output_path=directory/(method+'-raw.json')
    input_path.write_text(json.dumps(dict(vertices=v.tolist(),faces=f.tolist())))
    call=subprocess.run([str(runner),str(input_path),str(output_path),method],capture_output=True,text=True,timeout=180)
    if call.returncode:
        raise RuntimeError(call.stdout+call.stderr)
    raw=json.loads(output_path.read_text())
    if raw['used_fallback']:
        raise ValueError('baseline unexpectedly fell back')
    sf=np.asarray(raw['source_faces']); sv=np.asarray(raw['source_vertices'])
    outfaces=np.asarray(raw['faces']); outuv=np.asarray(raw['uvs'])
    if len(sf)!=len(f) or not np.array_equal(np.sort(sf),np.arange(len(f))):
        raise ValueError('source face coverage mismatch')
    labels=np.empty(len(f),np.int64); corners=np.empty((len(f),3,2))
    for i,source in enumerate(sf):
        source_vertices=sv[outfaces[i]]
        if sorted(source_vertices)!=sorted(f[source]):
            raise ValueError('source corner mapping mismatch')
        labels[source]=raw['face_charts'][i]
        for j,vertex in enumerate(f[source]):
            corners[source,j]=outuv[outfaces[i,np.flatnonzero(source_vertices==vertex)[0]]]
    # Native API normalizes each atlas axis independently; measure in pixel aspect.
    corners*=np.array([raw['atlas_width'],raw['atlas_height']])/max(raw['atlas_width'],raw['atlas_height'])
    metrics=audit(v,f,labels,corners)
    metrics.update(runtime_seconds=raw['runtime_seconds'],implementation='native_engine_'+method)
    result=dict(labels=labels,corner_uv=corners)
    return result,metrics


def render(v,f,result,path,title):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    from matplotlib.collections import PolyCollection
    labels=result['labels']; uvs=result['corner_uv']
    # Stable high-contrast palette; no semantic or anatomical meaning is asserted.
    colors=plt.cm.tab20((labels*7)%20/19)
    normal=np.cross(v[f[:,1]]-v[f[:,0]],v[f[:,2]]-v[f[:,0]])
    normal/=np.linalg.norm(normal,axis=1)[:,None]
    light=np.array([.4,-.5,.75]); light/=np.linalg.norm(light)
    surface_colors=colors.copy(); surface_colors[:,:3]*=(.45+.55*np.abs(normal@light))[:,None]
    fig=plt.figure(figsize=(16,5.2),layout='constrained')
    for panel,azimuth in enumerate((-60,120),1):
        ax=fig.add_subplot(1,3,panel,projection='3d')
        collection=Poly3DCollection(v[f],facecolors=surface_colors,linewidth=0,antialiased=False,rasterized=True)
        ax.add_collection3d(collection)
        lo,hi=v.min(axis=0),v.max(axis=0); center=(lo+hi)/2; extent=(hi-lo).max()/2
        ax.set_xlim(center[0]-extent,center[0]+extent); ax.set_ylim(center[1]-extent,center[1]+extent)
        ax.set_zlim(center[2]-extent,center[2]+extent)
        ax.set_box_aspect((1,1,1)); ax.view_init(elev=25,azim=azimuth); ax.set_axis_off()
    ax=fig.add_subplot(1,3,3)
    ax.add_collection(PolyCollection(uvs,facecolors=colors,edgecolors='none',antialiased=False,rasterized=True))
    ax.autoscale_view(); ax.set_aspect('equal'); ax.set_axis_off(); ax.set_title('UV charts (actual mapping)')
    fig.suptitle(title); fig.savefig(path,dpi=150); plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mesh',type=Path); parser.add_argument('output',type=Path)
    parser.add_argument('--runner',type=Path,default=Path('build/ci/bin/IntrinsicUvAtlasMeshDiagnostic'))
    parser.add_argument('--patches',type=int,default=64)
    parser.add_argument('--stretch-limit',type=float,default=1.5)
    parser.add_argument('--arms',nargs='+',choices=['feature','blind','growth-only','merge-only','xatlas','fast-staged'],default=['feature','blind','xatlas'])
    parser.add_argument('--subdivide',type=int,default=0)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    v,f=pm.read_obj(args.mesh)
    if args.subdivide:
        import trimesh
        for _ in range(args.subdivide):
            v,f=trimesh.remesh.subdivide(v,f)
    records=[]
    for arm in args.arms:
        if arm in ('xatlas','fast-staged'):
            result,metrics=native(v,f,args.runner.resolve(),args.output,arm)
        else:
            growth_weight=2. if arm in ('feature','growth-only') else 0.
            rank_weight=2. if arm in ('feature','merge-only') else 0.
            result=pm.execute(v,f,patches=args.patches,feature_weight=growth_weight,
                              merge_feature_weight=rank_weight,stretch_limit=args.stretch_limit)
            metrics=result.pop('metrics')
            independent=audit(v,f,result['labels'],result['corner_uv'])
            if not independent['all_charts_valid'] or abs(independent['max_stretch']-metrics['max_stretch'])>1e-6:
                raise ValueError('independent final UV audit disagrees: '+str(independent))
            metrics['independent_audit']=independent
        record=dict(arm=arm,patches=args.patches,stretch_limit=args.stretch_limit,subdivide=args.subdivide,faces=len(f),
                    source=str(args.mesh),source_sha256=hashlib.sha256(args.mesh.read_bytes()).hexdigest(),
                    implementation_sha256=hashlib.sha256(Path(pm.__file__).read_bytes()).hexdigest(),
                    claim_eligible=False,metrics=metrics)
        (args.output/(arm+'.json')).write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
        np.savez_compressed(args.output/(arm+'.npz'),vertices=v,faces=f,**result)
        value=metrics['max_stretch']; quality=f'{value:.3f}' if value is not None else 'invalid'
        render(v,f,result,args.output/(arm+'.png'),f'{args.mesh.stem} · {arm} · {metrics["chart_count"]} charts · max stretch {quality}')
        records.append(record)
        print(json.dumps(record,allow_nan=False),flush=True)
    (args.output/'comparison.json').write_text(json.dumps(records,indent=2,allow_nan=False)+'\n')


if __name__=='__main__':
    main()
