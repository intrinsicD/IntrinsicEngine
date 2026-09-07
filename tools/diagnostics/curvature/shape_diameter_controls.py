#!/usr/bin/env python3
"""Fixed field-control cohort for the bounded shape-diameter experiment."""
import argparse
import json
import math
from pathlib import Path
import sys
import shape_diameter_parts as sdf


def weighted_quantile(values,areas,q):
    pairs=sorted((v,a) for v,a in zip(values,areas) if v is not None)
    if not pairs:return None
    target=q*math.fsum(a for _,a in pairs);accum=0.
    for value,area in pairs:
        accum+=area
        if accum>=target:return value
    return pairs[-1][0]


def summarize(graph,result,kind):
    values=result['values_over_sqrt_area'];areas=graph['areas']
    out={k:result[k] for k in ('supported_faces','faces','ray_misses','opposite_normal_rejections')}
    out['area_quantiles']=[weighted_quantile(values,areas,q) for q in (.05,.5,.95)]
    if kind in ('neck','asymmetric','bent'):
        xs=[sum(graph['original_positions'][i][0] for i in f)/3 for f in graph['faces']]
        masks={'waist':[abs(x)<.15 for x in xs], 'left_lobe':[-1.3<x<-.6 for x in xs], 'right_lobe':[.6<x<1.3 for x in xs]}
        out['region_medians']={key:weighted_quantile([v if m else None for v,m in zip(values,mask)],areas,.5) for key,mask in masks.items()}
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error('fresh output directory required')
    config=sdf.validate_config(sdf.read_json(args.config));args.output.mkdir(parents=True)
    cases=[]
    for kind in ('sphere','neck','asymmetric','bent','groove','ridge'):
        for name,axial,radial,diagonal in [('base',32,24,0),('dense',64,48,0),('flip',32,24,1)]:
            print(f'Start {kind}-{name}',flush=True)
            vertices,faces=sdf.synthetic(kind,axial,radial,diagonal);graph=sdf.geometry(vertices,faces)
            result=sdf.measure(graph,config)
            result.update(schema='intrinsic.shape-diameter-field.v1',implementation=config['implementation'],backend='offline_cpu_reference',claim_eligible=False,
                          config=config,config_sha256=sdf.digest(args.config),source_sha256=sdf.digest(sdf.__file__),
                          controller_sha256=sdf.digest(__file__),case=kind,sampling=name,
                          geometry={'positions':vertices,'triangles':faces},baseline_labels=None)
            path=args.output/f'{kind}-{name}.field.json'
            path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
            row={'case':kind,'sampling':name,'path':str(path),'sha256':sdf.digest(path),**summarize(graph,result,kind)}
            cases.append(row);print(json.dumps(row),flush=True)
            (args.output/'cohort.json').write_text(json.dumps({'schema':'intrinsic.sdf-control-cohort.v1','claim_eligible':False,
                'config_sha256':sdf.digest(args.config),'source_sha256':sdf.digest(sdf.__file__),'invocation':sys.argv,'cases':cases},indent=2)+'\n')


if __name__=='__main__':main()
