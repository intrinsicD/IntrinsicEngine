#!/usr/bin/env python3
"""Retain quality-only atlas comparisons, including rejected/missing baselines."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import sys

import numpy as np
import scipy
import yaml

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/benchmark'))
from seal_benchmark_results import canonicalize


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    manifest_path=ROOT/'benchmarks/geometry/manifests/geometry_baseline_atlas_diagnostic.yaml'
    manifest=yaml.safe_load(manifest_path.read_text()); cells=[]
    for batch in ('round1','round2','subdivision','dolphin-comparison','final-comparators'):
        source=args.campaign/batch/'comparison.json'
        if not source.exists():
            continue
        target=args.output/'cells'/batch; target.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target/'comparison.json')
        bindings=source.with_name('source-bindings.json')
        if bindings.exists():
            shutil.copyfile(bindings,target/'source-bindings.json')
        for row in json.loads(source.read_text()):
            name=row['mesh']+'-'+row.get('arm','unavailable')
            measured=row.get('metrics',{}); violation=0.
            if measured:
                violation=float(not measured['all_charts_valid'])
                if row['arm']!='xatlas':
                    violation=max(violation,(measured['max_stretch'] or 0)-row['stretch_limit'],
                                  (measured['max_anisotropy'] or 0)-2.,0.)
                if row['arm'].startswith('protected'):
                    violation=max(violation,float(measured['lost_baseline_edges']))
            else:
                violation=1.
            raw=dict(benchmark_id=manifest['benchmark_id'],method=manifest['method'],dataset=manifest['dataset'],
                     backend='cpu_reference',commit='local-dev',status='passed' if measured else 'error',
                     metrics=dict(quality_error_linf=violation),
                     diagnostics=dict(cell=row,batch=batch,
                                      quality_error_definition='invalid map indicator; candidate stretch/anisotropy excess; protected candidate lost baseline edges; missing baseline is an execution error',
                                      limitation='Exploratory manifest consolidated after the first probe; quality gates do not mean baseline dominance or production adoption.'))
            sealed=canonicalize(raw,manifest_path=manifest_path,manifest=manifest,manifests_root=ROOT/'benchmarks',
                                run_id='method045-'+batch+'-'+name,attempt_id='attempt-001',
                                source_state='dirty_worktree',source_revision='local-dev',claim_eligible=False,
                                snapshot_sha256=None,diff_sha256=None)
            destination=args.output/'benchmarks'/(sealed['run_id']+'.json'); destination.parent.mkdir(exist_ok=True)
            destination.write_text(json.dumps(sealed,indent=2,allow_nan=False)+'\n')
            cells.append(dict(batch=batch,record=row,benchmark=str(destination.relative_to(args.output))))
    for mesh in ('sculpt','frog','fandisk','dolphin'):
        source=args.campaign/'round2'/mesh/'protected-merge'
        for filename in ('result.npz','comparison.png','packed.obj','packed.mtl','packed.checker.png'):
            if (source/filename).exists():
                target=args.output/'examples'/mesh/filename; target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(source/filename,target)
    for source in (args.campaign/'pack-search').glob('*.json'):
        if source.name.endswith(('.input.json','.raw.json')):
            continue
        target=args.output/'packing'/source.name; target.parent.mkdir(exist_ok=True)
        shutil.copyfile(source,target)
    for mesh in ('sculpt','frog','fandisk','dolphin','bunny10k'):
        for suffix in ('.labels','.geometry.json','.json'):
            source=args.campaign/'baselines'/(mesh+suffix)
            if not source.exists():
                continue
            # Compact, exact native source-face correspondence is the replay input.
            target=args.output/'baselines'/source.name; target.parent.mkdir(exist_ok=True)
            if suffix=='.geometry.json':
                import gzip
                target=target.with_suffix(target.suffix+'.gz')
                target.write_bytes(gzip.compress(source.read_bytes(),mtime=0))
            else:
                shutil.copyfile(source,target)
    files=list((ROOT/'tools/diagnostics/atlas').glob('*.py'))+[ROOT/'tests/regression/tooling/Test.BaselineAtlas.py',
          ROOT/'benchmarks/runners/UvChartPackDiagnosticRunner.cpp']
    record=dict(schema='intrinsic.baseline-atlas-campaign.v1',claim_eligible=False,
                environment=dict(python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,
                                 native_preset='ci',native_compiler='Clang 23',native_build_type='Debug'),
                cells=cells,current_source={str(p.relative_to(ROOT)):digest(p) for p in files},
                limitations=['offline CPU only; not integrated into the editor',
                             'preserving input labels does not validate anatomical segmentation',
                             'uniform subdivision is not arbitrary remeshing',
                             'packing occupancy and distortion are distinct objectives',
                             'historical exploratory cells are not clean exact-commit evidence'])
    record['artifacts']={str(p.relative_to(args.output)):digest(p) for p in sorted(args.output.rglob('*')) if p.is_file() and p.name!='record.json'}
    (args.output/'record.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    print(f'Retained {len(cells)} cells in {args.output}')


if __name__=='__main__':
    main()
