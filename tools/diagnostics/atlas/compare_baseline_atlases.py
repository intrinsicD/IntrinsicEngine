#!/usr/bin/env python3
"""Compare protected-region charts with frozen seed and native atlas baselines."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import baseline_atlas as ba
import patch_merge as pm
from compare_atlases import audit, native, render


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def subdivide(v,f,regions):
    """Four triangles per source face; midpoint subdivision changes no surface."""
    vertices=v.tolist(); midpoints={}; faces=[]
    def midpoint(a,b):
        key=tuple(sorted((int(a),int(b))))
        if key not in midpoints:
            midpoints[key]=len(vertices); vertices.append(((v[a]+v[b])/2).tolist())
        return midpoints[key]
    for a,b,c in f:
        ab,bc,ca=midpoint(a,b),midpoint(b,c),midpoint(c,a)
        faces.extend(((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)))
    return np.asarray(vertices),np.asarray(faces),np.repeat(regions,4)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baselines',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--meshes',nargs='+',default=['sculpt','frog','fandisk','bunny10k'])
    parser.add_argument('--arms',nargs='+',choices=['protected','protected-merge','feature','growth-only','xatlas'],
                        default=['protected','feature','growth-only','xatlas'])
    parser.add_argument('--stretch-limit',type=float,default=1.35)
    parser.add_argument('--subdivide',action='store_true')
    parser.add_argument('--runner',type=Path,default=Path('build/ci/bin/IntrinsicUvAtlasMeshDiagnostic'))
    parser.add_argument('--packer',type=Path,default=Path('build/ci/bin/IntrinsicUvChartPackDiagnostic'))
    args=parser.parse_args()
    if args.output.exists():
        parser.error('use a fresh output directory to retain earlier observations')
    args.output.mkdir(parents=True)
    records=[]
    for mesh in args.meshes:
        geometry=args.baselines/(mesh+'.geometry.json'); label_path=args.baselines/(mesh+'.labels')
        if not geometry.exists() or not label_path.exists():
            failure=args.baselines/(mesh+'.json')
            record=dict(mesh=mesh,status='baseline_unavailable',claim_eligible=False,
                        diagnostic=json.loads(failure.read_text()) if failure.exists() else 'missing export')
            records.append(record)
            (args.output/'comparison.json').write_text(json.dumps(records,indent=2)+'\n')
            print(json.dumps(record),flush=True)
            continue
        source=json.loads(geometry.read_text()); v=np.asarray(source['positions']); f=np.asarray(source['triangles'])
        regions=np.loadtxt(label_path,dtype=np.int64,ndmin=1)
        if args.subdivide:
            v,f,regions=subdivide(v,f,regions)
        g=pm.geometry(v,f)
        for arm in args.arms:
            folder=args.output/mesh/arm; folder.mkdir(parents=True)
            if arm=='xatlas':
                result,measured=native(v,f,args.runner.resolve(),folder)
                prepack=None; attempts=[]; outcome='native_end_to_end'
                measured['square_texture_occupancy']=float(np.abs(pm.cross2(
                    result['corner_uv'][:,1]-result['corner_uv'][:,0],
                    result['corner_uv'][:,2]-result['corner_uv'][:,0])).sum()/2)
                raw=json.loads((folder/'xatlas-raw.json').read_text())
                width,height=raw['atlas_width'],raw['atlas_height']
                measured['continuous_packing_utilization']=measured['square_texture_occupancy']/(width*height/max(width,height)**2)
            else:
                result=(ba.execute(v,f,regions,stretch_limit=args.stretch_limit,merge=arm=='protected-merge') if arm.startswith('protected') else
                        pm.execute(v,f,patches=64,feature_weight=2.,
                                   merge_feature_weight=0. if arm=='growth-only' else 2.,stretch_limit=args.stretch_limit))
                prepack=result['metrics']; attempts=result.get('component_attempts',[])
                outcome=prepack.get('outcome','seeded_atlas')
                arrays={k:x for k,x in result.items() if k not in ('metrics','component_attempts')}
                np.savez_compressed(folder/'unpacked.npz',vertices=v,faces=f,**arrays)
                (folder/'unpacked.json').write_text(json.dumps({'stretch_limit':args.stretch_limit}))
                call=subprocess.run([sys.executable,str(Path(__file__).with_name('repack.py')),
                                     str(folder/'unpacked.npz'),str(folder/'packed.json'),
                                     '--runner',str(args.packer)],capture_output=True,text=True)
                (folder/'packing.log').write_text(call.stdout+call.stderr)
                if call.returncode and not (folder/'packed.json').exists():
                    raise RuntimeError('packer failed: '+call.stderr)
                packed=json.loads((folder/'packed.json').read_text()); measured=packed['metrics']
                measured['postpack_bound_passed']=packed['postpack_bound_passed']
                data=np.load(folder/'packed.npz'); result={k:data[k] for k in ('labels','corner_uv')}
                measured['square_texture_occupancy']=float(np.abs(pm.cross2(
                    result['corner_uv'][:,1]-result['corner_uv'][:,0],
                    result['corner_uv'][:,2]-result['corner_uv'][:,0])).sum()/2)
            measured.update(ba.boundary_metrics(g,regions,result['labels'],result['corner_uv']))
            measured['region_count']=len(np.unique(regions))
            np.savez_compressed(folder/'result.npz',vertices=v,faces=f,region_labels=regions,**result)
            render(v,f,result,folder/'comparison.png',f'{mesh} · {arm} · {measured["chart_count"]} charts')
            record=dict(mesh=mesh,arm=arm,subdivided=args.subdivide,faces=len(f),
                        source_sha256=sha(geometry),baseline_labels_sha256=sha(label_path),
                        claim_eligible=False,stretch_limit=args.stretch_limit,anisotropy_limit=2.,
                        outcome=outcome,metrics=measured,prepack_metrics=prepack,component_attempts=attempts)
            (folder/'result.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
            records.append(record)
            (args.output/'comparison.json').write_text(json.dumps(records,indent=2,allow_nan=False)+'\n')
            print(json.dumps({k:record[k] for k in ('mesh','arm','metrics')}),flush=True)
    files=[Path(__file__),Path(pm.__file__),Path(ba.__file__),Path(__file__).with_name('compare_atlases.py'),
           Path(__file__).with_name('repack.py'),args.runner,args.packer]
    (args.output/'source-bindings.json').write_text(json.dumps({str(p):sha(p) for p in files},indent=2)+'\n')


if __name__=='__main__':
    main()
