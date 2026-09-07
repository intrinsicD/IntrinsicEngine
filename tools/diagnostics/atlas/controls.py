#!/usr/bin/env python3
"""Generated scale/resolution/topology controls; no external mesh required."""
import argparse
import json
from pathlib import Path
import numpy as np
import trimesh
import patch_merge as pm
from compare_atlases import audit


def cylinder(n, cut=False, planar=False):
    count=n+1 if cut else n
    angles=np.linspace(0,2*np.pi,n+1)[:count]
    v=[]
    for j in range(9):
        for a in angles:
            radius=1+j/8 if planar else 1
            v.append((radius*np.cos(a),radius*np.sin(a),0 if planar else j/4))
    f=[]
    for j in range(8):
        for i in range(n):
            a=j*count+i; b=j*count+(i+1)%count
            f.extend(((a,b,b+count),(a,b+count,a+count)))
    return np.array(v),np.array(f)


def fold(n):
    v=[]
    for y in np.linspace(-1,1,n):
        for x in np.linspace(-1,1,n):
            v.append((min(x,0),y,max(x,0)))
    f=[]
    for j in range(n-1):
        for i in range(n-1):
            a=j*n+i; f.extend(((a,a+1,a+n+1),(a,a+n+1,a+n)))
    return np.array(v),np.array(f)


def fixtures():
    for n in (16,32,64):
        yield f'cylinder_annulus_{n}',*cylinder(n)
        yield f'cylinder_cut_{n}',*cylinder(n,cut=True)
        yield f'planar_annulus_{n}',*cylinder(n,planar=True)
    for n in (9,17,33):
        yield f'fold_{n}',*fold(n)
    for n in (1,2,3):
        mesh=trimesh.creation.icosphere(subdivisions=n)
        yield f'sphere_{n}',mesh.vertices,mesh.faces


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('output',type=Path)
    args=parser.parse_args(); records=[]
    for name,v,f in fixtures():
        result=pm.execute(v,f,patches=64)
        measured=audit(v,f,result['labels'],result['corner_uv'])
        passed=measured['all_charts_valid'] and measured['max_stretch']<=1.5+1e-8
        if 'cut' in name or name.startswith('fold'):
            passed=passed and measured['chart_count']==1 and measured['max_stretch']<1.000001
        else:
            passed=passed and measured['chart_count']>=2
        record=dict(fixture=name,faces=len(f),passed=passed,metrics=result['metrics'],audit=measured)
        records.append(record); print(json.dumps(record),flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(records,indent=2,allow_nan=False)+'\n')
    return 0 if all(x['passed'] for x in records) else 1


if __name__=='__main__':
    raise SystemExit(main())
