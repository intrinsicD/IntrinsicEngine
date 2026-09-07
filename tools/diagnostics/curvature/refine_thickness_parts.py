#!/usr/bin/env python3
"""Explicit small-hole field completion and connected-area cleanup for diagnostics."""
import argparse
import heapq
import json
import math
from pathlib import Path
import sys
import shape_diameter_parts as sdf
import select_thickness_parts as parts


def validate_config(config):
    if config.get('schema')!='intrinsic.thickness-cleanup.v1' or config.get('implementation')!='thickness_persistence_cleanup_cpu_diagnostic_v1':
        raise ValueError('unknown thickness cleanup')
    for key in ('minimum_connected_area_fraction','maximum_filled_area_fraction','maximum_fill_distance_over_sqrt_area'):
        if type(config.get(key)) not in (float,int) or not math.isfinite(config[key]) or not 0<config[key]<.5:
            raise ValueError(f'invalid {key}')
    if config.get('merge_rule')!='smallest_area_into_longest_shared_boundary':raise ValueError('unknown merge rule')
    parts.validate_config(config['selector'])
    return config


def complete_field(graph, values, config):
    if len(values)!=len(graph['areas']):raise ValueError('field cardinality mismatch')
    missing=[i for i,v in enumerate(values) if v is None]
    if any(v is not None and (type(v) not in (int,float) or not math.isfinite(v) or v<=0) for v in values):raise ValueError('invalid thickness')
    area=math.fsum(graph['areas'][i] for i in missing)/math.fsum(graph['areas'])
    if area>config['maximum_filled_area_fraction']:raise ValueError('unsupported area exceeds explicit fill limit')
    result=list(values);distance={};source={};queue=[]
    for i in missing:
        for neighbor in graph['adjacent'][i]:
            if values[neighbor] is not None:
                d=math.dist(graph['centers'][i],graph['centers'][neighbor]);candidate=(d,neighbor)
                if candidate<(distance.get(i,math.inf),source.get(i,math.inf)):
                    distance[i],source[i]=candidate;heapq.heappush(queue,(d,neighbor,i))
    while queue:
        d,seed,i=heapq.heappop(queue)
        if (d,seed)!=(distance[i],source[i]):continue
        for other in graph['adjacent'][i]:
            if values[other] is not None:continue
            candidate=(d+math.dist(graph['centers'][i],graph['centers'][other]),seed)
            if candidate<(distance.get(other,math.inf),source.get(other,math.inf)):
                distance[other],source[other]=candidate;heapq.heappush(queue,(*candidate,other))
    for i in missing:
        if distance.get(i,math.inf)>config['maximum_fill_distance_over_sqrt_area']:
            raise ValueError('unsupported patch exceeds explicit fill distance')
        result[i]=values[source[i]]
    return result,{'filled_faces':missing,'filled_area_fraction':area,
                   'maximum_fill_distance_over_sqrt_area':max(distance.values(),default=0.),
                   'source_face_for_fill':[source[i] for i in missing]}


def cleanup(graph, initial, minimum):
    labels=parts.connected_labels(initial,graph['adjacent']);merges=[]
    total=math.fsum(graph['areas'])
    for _ in range(len(labels)):
        areas={}
        for label,a in zip(labels,graph['areas']):areas[label]=areas.get(label,0.)+a
        small=[label for label,a in areas.items() if a/total<minimum]
        if not small or len(areas)==1:break
        source=min(small,key=lambda i:(areas[i],i));shared={}
        for a,b,length,_,_ in graph['edges']:
            la,lb=labels[a],labels[b]
            if la==lb:continue
            other=lb if la==source else la if lb==source else None
            if other is not None:shared[other]=shared.get(other,0.)+length
        if not shared:raise ValueError('isolated region cannot be cleaned')
        target=max(shared,key=lambda i:(shared[i],-i))
        merges.append({'source':source,'target':target,'source_area_fraction':areas[source]/total,'shared_length_over_sqrt_area':shared[target]})
        labels=[target if label==source else label for label in labels]
    return parts.connected_labels(labels,graph['adjacent']),merges


def select(graph, values, config, baseline=None):
    validate_config(config)
    completed,fill=complete_field(graph,values,config)
    initial=parts.select(graph,completed,config['selector'])
    clean,merges=cleanup(graph,initial['labels'],config['minimum_connected_area_fraction'])
    if baseline is not None:
        if len(baseline)!=len(clean) or any(type(v) is not int or v<0 for v in baseline):raise ValueError('invalid baseline labels')
        labels=parts.connected_labels(list(zip(clean,baseline)),graph['adjacent'])
    else:labels=clean
    areas=[0.]*(max(labels)+1)
    for label,a in zip(labels,graph['areas']):areas[label]+=a
    total=math.fsum(areas)
    return {'implementation':config['implementation'],'backend':'offline_cpu_reference','claim_eligible':False,'labels':labels,
            'regions':len(areas),'area_fractions':[a/total for a in areas],'raw_regions':initial['regions'],
            'clean_regions_before_baseline':max(clean)+1,'merges':merges,'field_completion':fill,
            'retained_peaks':initial['retained_peaks'],'completed_values_over_sqrt_area':completed}


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
    print(json.dumps({k:result[k] for k in ('regions','clean_regions_before_baseline','area_fractions','field_completion')}))


if __name__=='__main__':main()
