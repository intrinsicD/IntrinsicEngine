import json,sys
from pathlib import Path
sys.path.insert(0,'tools/diagnostics/curvature')
import shape_diameter_parts as sdf
import refine_thickness_parts as parts
config=sdf.read_json('tools/diagnostics/curvature/thickness_parts_round3.json')
root=Path('build/method043/r3/controls');root.mkdir(parents=True,exist_ok=False)
records=[]
for row in sdf.read_json('build/method043/r1/controls/cohort.json')['cases']:
    source=Path(row['path']);assert sdf.digest(source)==row['sha256']
    field=sdf.read_json(source);g=sdf.geometry(field['geometry']['positions'],field['geometry']['triangles'])
    result=parts.select(g,field['values_over_sqrt_area'],config)
    result.update(config=config,source_sha256=sdf.digest(parts.__file__),field_sha256=sdf.digest(source),geometry=field['geometry'])
    path=root/f"{row['case']}-{row['sampling']}.parts.json";path.write_text(json.dumps(result,indent=2)+'\n')
    summary={k:result[k] for k in ('regions','area_fractions','clean_regions_before_baseline')}
    summary.update(case=row['case'],sampling=row['sampling'],path=str(path),sha256=sdf.digest(path))
    if row['case'] in ('neck','asymmetric','bent'):
        cuts=[(a,b) for f,h,_,a,b in g['edges'] if result['labels'][f]!=result['labels'][h]]
        summary['seam_max_abs_x_over_sqrt_area']=max((abs(g['original_positions'][i][0])/g['scale'] for edge in cuts for i in edge),default=None)
    records.append(summary);print(json.dumps(summary),flush=True)
(root/'cohort.json').write_text(json.dumps({'config':config,'records':records},indent=2)+'\n')
