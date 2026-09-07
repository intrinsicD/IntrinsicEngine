#!/usr/bin/env python3
"""Collect declared atlas campaign cells and seal local, non-claim-grade records."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys
import platform
import numpy as np
import scipy
import yaml

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/benchmark'))
from seal_benchmark_results import canonicalize


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_text(source,target):
    """Normalize display-log whitespace; numeric records retain their own bytes."""
    text='\n'.join(line.rstrip() for line in source.read_text().splitlines()).rstrip()+'\n'
    target.write_text(text)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--rounds',type=Path,nargs='+',required=True)
    parser.add_argument('--reviews',type=Path,nargs='*',default=[])
    parser.add_argument('--logs',type=Path,nargs='*',default=[])
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    cells=[]; artifact_index={}
    manifest_path=ROOT/'benchmarks/geometry/manifests/geometry_uv_atlas_patch_merge_diagnostic.yaml'
    manifest=yaml.safe_load(manifest_path.read_text())
    for directory in args.rounds:
        if not directory.is_dir():
            raise ValueError(f'missing campaign directory {directory}')
        for source in sorted(directory.rglob('*.json')):
            if source.name=='native-input.json' or source.name.endswith(('.input.json','.raw.json','-raw.json')):
                continue
            data=json.loads(source.read_text())
            if not isinstance(data,dict) or 'metrics' not in data:
                continue
            destination=args.output/'cells'/directory.name/source.relative_to(directory)
            destination.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(source,destination)
            artifact_index[str(destination.relative_to(args.output))]=digest(destination)
            row=dict(path=str(destination.relative_to(args.output)),record=data)
            cells.append(row)
            if data.get('arm') in ('feature','blind','growth-only','merge-only','xatlas','fast-staged'):
                metrics=data['metrics']; checked=metrics.get('independent_audit',metrics)
                violation=0. if checked.get('all_charts_valid') else 1.
                if data['arm'] not in ('xatlas','fast-staged') and checked.get('max_stretch') is not None:
                    violation=max(violation,checked['max_stretch']-data.get('stretch_limit',1.5),0.)
                raw=dict(benchmark_id=manifest['benchmark_id'],method=manifest['method'],dataset=manifest['dataset'],
                         backend='cpu_reference',commit='local-dev',status='passed',
                         metrics=dict(runtime_ms=1000*metrics['runtime_seconds'],quality_error_linf=violation),
                         diagnostics=dict(cell=data,validity_error_definition='max of invalid-map indicator and prototype stretch excess; native has no imposed stretch bound',
                                          timed_scope='resident geometry through internal validation and packing; excludes independent audit, IO, and rendering',
                                          implementation_comparability='Python reference versus ci Debug native adapter; no speedup claim'))
                sealed=canonicalize(raw,manifest_path=manifest_path,manifest=manifest,manifests_root=ROOT/'benchmarks',
                                    run_id='method044-'+directory.name+'-'+source.parent.name+'-'+data['arm'],attempt_id='attempt-001',
                                    source_state='dirty_worktree',source_revision='local-dev',claim_eligible=False,
                                    snapshot_sha256=None,diff_sha256=None)
                target=args.output/'benchmarks'/(sealed['run_id']+'.json'); target.parent.mkdir(exist_ok=True)
                target.write_text(json.dumps(sealed,indent=2,allow_nan=False)+'\n')
        # Preserve actual native outputs compactly for independent face/UV auditing.
        for source in sorted(directory.rglob('xatlas-raw.json')):
            target=args.output/'native'/directory.name/source.parent.name/'xatlas-raw.json.gz'
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        controls=directory/'controls.json'
        if controls.exists():
            shutil.copyfile(controls,args.output/(directory.name+'-controls.json'))
    for source in args.reviews:
        target=args.output/'reviews'/source.name; target.parent.mkdir(exist_ok=True)
        copy_text(source,target)
    for source in args.logs:
        target=args.output/'logs'/source.name; target.parent.mkdir(exist_ok=True)
        copy_text(source,target)
    for name in ('frog_stretch135_final','sculpt_stretch135_final'):
        found=[d/name for d in args.rounds if (d/name/'packed.npz').exists()]
        if not found:
            continue
        for source in found[-1].iterdir():
            if source.suffix in ('.obj','.mtl','.npz','.png'):
                target=args.output/'examples'/name/source.name; target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(source,target)
    implementation={str(p.relative_to(ROOT)):digest(p) for p in sorted((ROOT/'tools/diagnostics/atlas').glob('*.py'))}
    record=dict(schema='intrinsic.atlas-campaign.v1',claim_eligible=False,source_state='dirty_worktree',
                environment=dict(python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,
                                 native_preset='ci',native_build_type='Debug',native_compiler='Clang 23.0.0',
                                 xatlas_revision='f700c7790aaa030e794b52ba7791a05c085faf0c'),
                cells=cells,cell_digests=artifact_index,current_implementation=implementation,
                limitations=['not a native performance benchmark','single-sample timings may overlap other local work',
                             'disk-only prototype; native non-disk charts not automatically invalid',
                             'no production defaults changed','uniform subdivision is not an independent remeshing test'])
    (args.output/'record.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    print(f'Collected {len(cells)} cells into {args.output}')


if __name__=='__main__':
    main()
