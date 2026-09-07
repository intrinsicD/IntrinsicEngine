#!/usr/bin/env python3
"""Withheld body/appendage fixtures; frozen thickness pipeline, not a new selector."""
import argparse
import json
import math
from pathlib import Path
import sys
import shape_diameter_parts as sdf
import refine_thickness_parts as parts


def fixture(config, kind, axial=32, radial=24, diagonal=0):
    junction=config['junction_x'];length=config['limb_length'];n=axial//2
    profile=[(-1.,0.)]
    for i in range(1,n+1):
        x=-1+(junction+1)*(1-math.cos(math.pi*i/n))/2
        profile.append((x,math.sqrt(1-x*x)))
    for i in range(1,n):
        t=i/n;radius=math.sqrt(1-junction*junction)*(1-t)
        if kind=='bulge':radius+=config['bulge_amplitude']*math.sin(math.pi*t)*math.exp(-((t-config['bulge_center'])/config['bulge_width'])**2)
        elif kind!='taper':raise ValueError('unknown fixture')
        profile.append((junction+length*t,radius))
    profile.append((junction+length,0.))
    vertices=[[profile[0][0],0.,0.]]
    for x,radius in profile[1:-1]:
        for j in range(radial):
            angle=2*math.pi*j/radial;vertices.append([x,radius*math.cos(angle),radius*math.sin(angle)])
    pole=len(vertices);vertices.append([profile[-1][0],0.,0.]);faces=[]
    for j in range(radial):faces.append([0,1+(j+1)%radial,1+j])
    for i in range(len(profile)-3):
        for j in range(radial):
            a=1+i*radial+j;b=1+i*radial+(j+1)%radial;c=a+radial;d=b+radial
            faces.extend([[a,b,c],[b,d,c]] if diagonal else [[a,b,d],[a,d,c]])
    start=1+(len(profile)-3)*radial
    for j in range(radial):faces.append([start+j,start+(j+1)%radial,pole])
    if math.fsum(sdf.dot(vertices[a],sdf.cross(vertices[b],vertices[c])) for a,b,c in faces)<0:faces=[f[::-1] for f in faces]
    return vertices,faces


def transform(vertices):
    result=[]
    for x,y,z in vertices:
        x,y=x*math.cos(.371)-y*math.sin(.371),x*math.sin(.371)+y*math.cos(.371)
        x,z=x*math.cos(.619)+z*math.sin(.619),-x*math.sin(.619)+z*math.cos(.619)
        result.append([3.7*x+2,3.7*y-3,3.7*z+5])
    return result


def seam_summary(graph,labels,original_x,config):
    ts=[(original_x[v]-config['junction_x'])/config['limb_length']
        for a,b,_,i,j in graph['edges'] if labels[a]!=labels[b] for v in (i,j)]
    return {'seam_t_min':min(ts,default=None),'seam_t_max':max(ts,default=None)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();config=sdf.read_json(args.config)
    if config['schema']!='intrinsic.thickness-withheld-controls.v1':raise ValueError('unknown control config')
    if args.output.exists():parser.error('fresh output directory required')
    args.output.mkdir(parents=True);field_config=sdf.read_json(config['field_config']);part_config=sdf.read_json(config['parts_config'])
    records=[];base={}
    for kind in config['cases']:
        for name in config['samplings']:
            axial,radial=(64,48) if name=='dense' else (32,24)
            vertices,faces=fixture(config,kind,axial,radial,int(name=='flip'))
            original_x=[p[0] for p in vertices];face_map=list(range(len(faces)))
            if name=='reordered':
                vertices=vertices[::-1];original_x=original_x[::-1];face_map=face_map[::-1]
                faces=[[len(vertices)-1-i for i in face] for face in faces[::-1]]
            if name=='rigid_scaled':vertices=transform(vertices)
            graph=sdf.geometry(vertices,faces);print(f'Start {kind}-{name}',flush=True)
            field=sdf.measure(graph,field_config)
            result=parts.select(graph,field['values_over_sqrt_area'],part_config)
            result.update(schema='intrinsic.thickness-withheld-result.v1',field=field,config=config,
                geometry={'positions':vertices,'triangles':faces},original_x=original_x,
                source_sha256={p:sdf.digest(p) for p in (str(Path(__file__).relative_to(Path.cwd())),sdf.__file__,parts.__file__)},
                field_config=field_config,parts_config=part_config)
            summary={'case':kind,'sampling':name,'regions':result['regions'],'area_fractions':result['area_fractions'],
                'supported_faces':field['supported_faces'],'faces':field['faces'],
                'field_completion':result['field_completion'],**seam_summary(graph,result['labels'],original_x,config)}
            if name=='base':base[kind]=result
            elif name in ('reordered','rigid_scaled'):
                reference=base[kind]
                summary['maximum_field_difference']=max(abs(v-reference['field']['values_over_sqrt_area'][i])
                    for v,i in zip(field['values_over_sqrt_area'],face_map) if v is not None and reference['field']['values_over_sqrt_area'][i] is not None)
                summary['boundary_edge_disagreements']=sum((result['labels'][a]!=result['labels'][b]) !=
                    (reference['labels'][face_map[a]]!=reference['labels'][face_map[b]]) for a,b,*_ in graph['edges'])
            path=args.output/f'{kind}-{name}.json';path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
            if name=='base':
                obj=args.output/f'{kind}.obj';obj.write_text(''.join('v '+' '.join(format(x,'.17g') for x in v)+'\n' for v in vertices)+''.join('f '+' '.join(str(i+1) for i in f)+'\n' for f in faces))
            summary.update(path=str(path),sha256=sdf.digest(path));records.append(summary);print(json.dumps(summary),flush=True)
            (args.output/'cohort.json').write_text(json.dumps({'config':config,'config_sha256':sdf.digest(args.config),'invocation':sys.argv,'claim_eligible':False,'records':records},indent=2)+'\n')


if __name__=='__main__':main()
