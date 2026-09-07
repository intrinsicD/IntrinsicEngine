#!/usr/bin/env python3
"""Replay a frozen thickness selector over a source-bound field-control cohort."""
import argparse
import json
from pathlib import Path
import sys
import shape_diameter_parts as sdf
import select_thickness_parts as persistent
import refine_thickness_parts as cleanup


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fields',type=Path,required=True);parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    config=sdf.read_json(args.config)
    module=cleanup if config.get('schema')=='intrinsic.thickness-cleanup.v1' else persistent
    module.validate_config(config)
    if args.output.exists():parser.error('fresh output directory required')
    args.output.mkdir(parents=True);records=[]
    for row in sdf.read_json(args.fields)['cases']:
        source=Path(row['path'])
        if sdf.digest(source)!=row['sha256']:raise ValueError(f'field hash mismatch: {source}')
        field=sdf.read_json(source);graph=sdf.geometry(field['geometry']['positions'],field['geometry']['triangles'])
        result=module.select(graph,field['values_over_sqrt_area'],config)
        result.update(config=config,config_sha256=sdf.digest(args.config),source_sha256=sdf.digest(module.__file__),
                      field_sha256=sdf.digest(source),geometry=field['geometry'])
        path=args.output/f"{row['case']}-{row['sampling']}.parts.json"
        path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        summary={k:result[k] for k in ('regions','area_fractions','retained_peaks')}
        summary.update(case=row['case'],sampling=row['sampling'],path=str(path),sha256=sdf.digest(path))
        cuts=[(a,b) for f,g,_,a,b in graph['edges'] if result['labels'][f]!=result['labels'][g]]
        if row['case'] in ('neck','asymmetric','bent'):
            summary['seam_max_abs_x_over_sqrt_area']=max((abs(graph['original_positions'][i][0])/graph['scale'] for edge in cuts for i in edge),default=None)
        records.append(summary);print(json.dumps(summary),flush=True)
    (args.output/'cohort.json').write_text(json.dumps({'config':config,'config_sha256':sdf.digest(args.config),'invocation':sys.argv,'records':records},indent=2)+'\n')


if __name__=='__main__':main()
