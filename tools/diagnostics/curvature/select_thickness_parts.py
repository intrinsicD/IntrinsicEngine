#!/usr/bin/env python3
"""Area-aware persistence selection of shape-diameter peak regions, offline only."""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import sys

import shape_diameter_parts as sdf


def connected_labels(values, adjacent):
    labels=[-1]*len(adjacent);count=0
    for first in range(len(labels)):
        if labels[first]>=0:continue
        labels[first]=count;queue=[first]
        while queue:
            current=queue.pop()
            for other in adjacent[current]:
                if labels[other]<0 and values[other]==values[first]:labels[other]=count;queue.append(other)
        count+=1
    return labels


def peak_tree(values, areas, adjacent):
    """Plateaus activate simultaneously; branch area is measured above its saddle."""
    if len(values)!=len(areas) or len(values)!=len(adjacent) or not values:
        raise ValueError('field/area/adjacency cardinality mismatch')
    if any(type(v) not in (int,float) or not math.isfinite(v) or v<=0 for v in values):
        raise ValueError('finite positive thickness on every face required')
    if any(not math.isfinite(a) or a<=0 for a in areas):raise ValueError('positive finite areas required')
    n=len(values);parent=list(range(n));active=[False]*n;owner=[-1]*n
    root_peak={};root_area={};peaks={}
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    levels=defaultdict(list)
    for i,v in enumerate(values):levels[v].append(i)
    for level in sorted(levels,reverse=True):
        fresh=set(levels[level]);links={i:i for i in fresh}
        def level_root(i):
            while links[i]!=i:links[i]=links[links[i]];i=links[i]
            return i
        for i in levels[level]:
            for other in adjacent[i]:
                if not active[other] and other not in fresh:continue
                other=root(other) if active[other] else other
                links.setdefault(other,other)
                a,b=level_root(i),level_root(other)
                links[max(a,b)]=min(a,b)
        groups=defaultdict(lambda:([],set()))
        for i in links:
            group=groups[level_root(i)]
            if i in fresh:group[0].append(i)
            else:group[1].add(i)
        # Even disjoint plateaus connected through an older component share one
        # simultaneous saddle event; no same-level area enters a dying core.
        for block,touched in groups.values():
            if not touched:
                representative=min(block);peak=representative
                peaks[peak]={'peak_face':peak,'peak':level,'parent_peak':None,'saddle':None,'core_area':None,'relative_persistence':None}
                root_peak[representative]=peak;root_area[representative]=0.
            else:
                representative=max(touched,key=lambda i:(values[root_peak[i]],-root_peak[i]));peak=root_peak[representative]
                for other in sorted(touched):
                    if other==representative:continue
                    young=root_peak[other]
                    peaks[young].update(parent_peak=peak,saddle=level,core_area=root_area[other],relative_persistence=(values[young]-level)/values[young])
                    parent[other]=representative;root_area[representative]+=root_area[other]
            for i in block:
                parent[i]=representative;active[i]=True;owner[i]=peak;root_area[representative]+=areas[i]
    for i in {root(j) for j in range(n)}:
        peaks[root_peak[i]]['core_area']=root_area[i]
    return owner,peaks


def validate_config(config):
    if config.get('schema')!='intrinsic.thickness-parts.v1' or config.get('implementation')!='thickness_persistence_cpu_diagnostic_v1':
        raise ValueError('unknown thickness parts experiment')
    for key in ('minimum_relative_persistence','minimum_peak_core_area_fraction'):
        if type(config.get(key)) not in (int,float) or not math.isfinite(config[key]) or not 0<config[key]<1:
            raise ValueError(f'invalid {key}')
    if config.get('invalid_field_policy')!='reject' or config.get('smoothing')!='none' or config.get('preserve_baseline_boundaries') is not True:
        raise ValueError('unsupported field or preservation policy')
    return config


def select(graph, values, config, baseline=None):
    validate_config(config)
    owner,peaks=peak_tree(values,graph['areas'],graph['adjacent'])
    total=math.fsum(graph['areas'])
    retained={p for p,r in peaks.items() if r['parent_peak'] is None or
              (r['relative_persistence']>=config['minimum_relative_persistence'] and r['core_area']/total>=config['minimum_peak_core_area_fraction'])}
    def survivor(peak):
        while peak not in retained:peak=peaks[peak]['parent_peak']
        return peak
    basin=[survivor(p) for p in owner]
    raw=connected_labels(basin,graph['adjacent'])
    if baseline is not None:
        if len(baseline)!=len(values) or any(type(x) is not int or x<0 for x in baseline):raise ValueError('invalid baseline labels')
        labels=connected_labels(list(zip(raw,baseline)),graph['adjacent'])
    else:labels=raw
    area=[0.]*(max(labels)+1)
    for label,a in zip(labels,graph['areas']):area[label]+=a
    boundary_length=math.fsum(length for a,b,length,_,_ in graph['edges'] if labels[a]!=labels[b])
    return {'implementation':config['implementation'],'backend':'offline_cpu_reference','claim_eligible':False,
            'retained_peaks':sorted(retained),'peak_tree':list(peaks.values()),'unconstrained_regions':max(raw)+1,
            'regions':len(area),'area_fractions':[a/total for a in area],'labels':labels,'raw_labels':raw,
            'boundary_length_over_sqrt_area':boundary_length,'field_policy':config['invalid_field_policy']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--field',type=Path,required=True);parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():parser.error('fresh output directory required')
    field=sdf.read_json(args.field);config=validate_config(sdf.read_json(args.config))
    if field.get('schema')!='intrinsic.shape-diameter-field.v1':raise ValueError('unknown field schema')
    graph=sdf.geometry(field['geometry']['positions'],field['geometry']['triangles'])
    result=select(graph,field['values_over_sqrt_area'],config,field.get('baseline_labels'))
    result.update(schema='intrinsic.thickness-parts-result.v1',config=config,config_sha256=sdf.digest(args.config),
                  source_sha256=sdf.digest(__file__),field_sha256=sdf.digest(args.field),field_path=str(args.field),
                  input_sha256=field.get('input_sha256'),geometry=field['geometry'],invocation=sys.argv)
    args.output.mkdir(parents=True)
    (args.output/'parts.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('regions','unconstrained_regions','area_fractions','retained_peaks')}))


if __name__=='__main__':main()
