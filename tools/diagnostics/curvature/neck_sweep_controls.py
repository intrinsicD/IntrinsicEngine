#!/usr/bin/env python3
"""Retain every cell of a deterministic neck-sweep diagnostic control cohort."""
import argparse
import json
import math
from pathlib import Path
import sys

import neck_sweep as neck


def proposal_diagnostic(graph):
    base=[0]*len(graph['faces'])
    start=neck.energy(graph,base)
    total=math.fsum(graph['areas'])
    waist=[int(c[0]>0) for c in graph['centers']]
    best=None
    for profile in neck.profiles(graph):
        for entry in profile['entries']:
            labels=neck.components([int(x<=entry['threshold']) for x in profile['field']],graph['adjacent'])
            areas=[0.]*(max(labels)+1)
            for label,area in zip(labels,graph['areas']):
                areas[label]+=area
            if len(areas)<2 or min(areas)<graph['config']['minimum_child_area_fraction']*total:
                continue
            delta=neck.energy(graph,labels)-start
            if best is None or delta<best['delta_energy']:
                best={'delta_energy':delta,'source_face':profile['source_face'],'fraction':entry['fraction'],
                      'proposal_prominence':entry['proposal_prominence'],'regions':len(areas)}
    return {'explicit_equatorial_partition_delta_energy':neck.energy(graph,waist)-start,
            'best_minimum_area_sweep_without_prominence_gate':best}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sampling', action='store_true')
    parser.add_argument('--ablations', action='store_true')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('fresh output required')
    config = neck.validate_config(neck.read_json(args.config))
    args.output.mkdir(parents=True)
    shapes = [('sphere','ellipsoid',1.,1.),('elongated4','ellipsoid',1.,4.),('elongated8','ellipsoid',1.,8.),
              ('ridge','ridge',1.,1.),('groove','groove',1.,1.)]
    shapes += [(f'neck{int(rho*100)}','neck',rho,2.) for rho in (.3,.5,.7,.9)]
    samplings = [('base',32,24,0,False)]
    if args.sampling:
        samplings += [('flip',32,24,1,False),('dense',64,48,0,False),('dense_flip',64,48,1,False),('clustered',32,24,0,True)]
    records = []
    for name, kind, rho, aspect in shapes:
        for sampling, axial, radial, diagonal, clustered in samplings:
            vertices, faces = neck.revolution(kind,axial,radial,diagonal,clustered,rho,aspect)
            graph = neck.prepare(vertices,faces,config)
            result = neck.run(graph)
            cut = [e for e in graph['edges'] if result['labels'][e['faces'][0]] != result['labels'][e['faces'][1]]]
            if kind == 'neck':
                result['waist_cut_max_abs_x_over_D'] = max((abs(vertices[v][0])/graph['diagonal'] for e in cut for v in e['vertices']),default=None)
                result['proposal_diagnostic']=proposal_diagnostic(graph)
            result.update(case=name,sampling=sampling,config_sha256=neck.digest(args.config),
                          source_sha256=neck.digest(neck.__file__),controller_sha256=neck.digest(__file__),
                          geometry={'positions':graph['vertices'],'triangles':graph['faces']})
            path = args.output/f'{name}-{sampling}.json'
            path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
            row = {k:result[k] for k in ('regions','area_fractions','initial_energy','final_energy','minimum_raw_kappa_times_D')}
            row.update(case=name,sampling=sampling,path=str(path),sha256=neck.digest(path),splits=len(result['accepted_splits']),
                       waist_cut_max_abs_x_over_D=result.get('waist_cut_max_abs_x_over_D'))
            if kind=='neck':
                row['proposal_diagnostic']=result['proposal_diagnostic']
            records.append(row)
            print(json.dumps(row),flush=True)
    if args.ablations:
        for kind in ('neck','groove','ridge'):
            vertices,faces = neck.revolution(kind,rho=.3,aspect=2. if kind=='neck' else 1.)
            graph=neck.prepare(vertices,faces,config)
            for regional,concavity in ((True,False),(False,True)):
                result=neck.run(graph,regional_gate=regional,concavity_gate=concavity)
                name=f'{kind}-'+('regional_only' if regional else 'concavity_only')
                path=args.output/f'{name}.json'
                result.update(geometry={'positions':graph['vertices'],'triangles':graph['faces']})
                path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
                row={'case':name,'regions':result['regions'],'splits':len(result['accepted_splits']),'path':str(path),'sha256':neck.digest(path)}
                records.append(row)
                print(json.dumps(row),flush=True)
    output={'schema':'intrinsic.neck-controls.v1','claim_eligible':False,'config':config,'config_sha256':neck.digest(args.config),
            'implementation_sha256':neck.digest(neck.__file__),'controller_sha256':neck.digest(__file__),'invocation':sys.argv,'records':records}
    (args.output/'cohort.json').write_text(json.dumps(output,indent=2,allow_nan=False)+'\n')


if __name__ == '__main__':
    main()
