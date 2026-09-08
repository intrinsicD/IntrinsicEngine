#!/usr/bin/env python3
"""Use xatlas only as a packer for frozen source-face labels and LSCM UVs."""
import argparse
import json
import hashlib
from pathlib import Path
import subprocess
import numpy as np
from compare_atlases import audit, render
import patch_merge as pm


def export_obj(path,v,f,labels,corners):
    """Derived, importable mesh with source geometry and independent corner UVs."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    material=path.with_suffix('.mtl'); texture=path.with_suffix('.checker.png')
    lines=['# Derived atlas; source triangle geometry is unchanged.',f'mtllib {material.name}']
    lines.extend('v '+' '.join(format(float(x),'.17g') for x in p) for p in v)
    lines.extend('vt '+' '.join(format(float(x),'.17g') for x in uv) for uv in corners.reshape(-1,2))
    lines.append('usemtl atlas_checker')
    for label in np.unique(labels):
        lines.append(f'g chart_{label}')
        for i in np.flatnonzero(labels==label):
            lines.append('f '+' '.join(f'{int(vertex)+1}/{3*i+j+1}' for j,vertex in enumerate(f[i])))
    path.write_text('\n'.join(lines)+'\n')
    material.write_text(f'newmtl atlas_checker\nKd 1 1 1\nmap_Kd {texture.name}\n')
    x,y=np.meshgrid(np.arange(512),np.arange(512))
    checker=np.where(((x//16+y//16)%2)[...,None],np.array([.16,.25,.34]),np.array([.88,.9,.92]))
    plt.imsave(texture,checker)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path); parser.add_argument('output',type=Path)
    parser.add_argument('--runner',type=Path,default=Path('build/ci/bin/IntrinsicUvChartPackDiagnostic'))
    parser.add_argument('--stretch-limit',type=float)
    parser.add_argument('--brute-force',action='store_true',help='exhaustive native chart placement')
    args=parser.parse_args(); data=np.load(args.input)
    settings=json.loads(args.input.with_suffix('.json').read_text())
    limit=args.stretch_limit or settings.get('stretch_limit',settings.get('parameters',{}).get('stretch_limit',1.5))
    v,f,labels,uv=data['vertices'],data['faces'],data['labels'],data['chart_uv']
    points,indices,keys=[],[],{}
    for face,label,corners in zip(f,labels,uv):
        for vertex,point in zip(face,corners):
            # Preserve both sides of an internal UV cut within the same chart.
            key=(int(vertex),int(label),float(point[0]),float(point[1]))
            if key not in keys:
                keys[key]=len(points); points.append(point.tolist())
            indices.append(keys[key])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    source=args.output.with_suffix('.input.json'); destination=args.output.with_suffix('.raw.json')
    source.write_text(json.dumps(dict(uvs=points,indices=indices,materials=labels.tolist(),brute_force=args.brute_force)))
    subprocess.run([str(args.runner.resolve()),str(source),str(destination)],check=True,timeout=180)
    raw=json.loads(destination.read_text()); outindices=np.asarray(raw['indices']).reshape(-1,3)
    xref=np.asarray(raw['source_vertices'])
    if not np.array_equal(xref[outindices].ravel(),indices) or raw['chart_count']!=len(np.unique(labels)):
        raise ValueError('packer changed chart/corner correspondence')
    corners=np.asarray(raw['uvs'])[outindices]/max(raw['width'],raw['height'])
    measured=audit(v,f,labels,corners)
    packed_area=np.abs(pm.cross2(corners[:,1]-corners[:,0],corners[:,2]-corners[:,0])).sum()/2
    measured['continuous_packing_utilization']=float(packed_area/(raw['width']*raw['height']/max(raw['width'],raw['height'])**2))
    # Report texel-density variation that per-chart normalization deliberately removes.
    areas=pm.geometry(v,f)['area']; scales=[]
    for label in np.unique(labels):
        ids=labels==label
        scales.append(float(np.sqrt(np.abs(pm.cross2(corners[ids,1]-corners[ids,0],corners[ids,2]-corners[ids,0])).sum()/2/areas[ids].sum())))
    measured['chart_texel_density_ratio']=max(scales)/min(scales)
    before=audit(v,f,labels,data['corner_uv'])
    passed=measured['all_charts_valid'] and measured['max_stretch']<=limit and measured['max_anisotropy']<=2.
    record=dict(input=str(args.input),input_sha256=hashlib.sha256(args.input.read_bytes()).hexdigest(),
                runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                packer={k:x for k,x in raw.items() if k not in ('uvs','indices','source_vertices')},
                stretch_limit=limit,postpack_bound_passed=passed,
                metrics=measured,prepack_metrics=before,claim_eligible=False)
    args.output.write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    result=dict(labels=labels,corner_uv=corners)
    if 'region_labels' in data:
        result['region_labels']=data['region_labels'].copy()
    np.savez_compressed(args.output.with_suffix('.npz'),vertices=v,faces=f,**result)
    render(v,f,result,args.output.with_suffix('.png'),f'{args.input.parent.name} · {len(scales)} charts · packed UV atlas')
    export_obj(args.output.with_suffix('.obj'),v,f,labels,corners)
    print(json.dumps(record,allow_nan=False),flush=True)
    return 0 if passed else 1


if __name__=='__main__':
    raise SystemExit(main())
