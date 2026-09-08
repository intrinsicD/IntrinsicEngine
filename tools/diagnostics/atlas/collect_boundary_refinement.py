#!/usr/bin/env python3
"""Reconcile frozen refinement outputs and retain quality-only benchmark evidence."""
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
import baseline_atlas as ba
import patch_merge as pm

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tools/benchmark'))
from seal_benchmark_results import canonicalize


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path); parser.add_argument('output', type=Path)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = ROOT/'benchmarks/geometry/manifests/geometry_uv_boundary_refinement_diagnostic.yaml'
    manifest = yaml.safe_load(manifest_path.read_text()); rows = []
    for mesh in ('frog', 'sculpt'):
        folder = args.campaign/mesh; summary = json.loads((folder/'results.json').read_text())
        reference = np.load(folder/'reference/unpacked.npz')
        g = pm.geometry(reference['vertices'], reference['faces'])
        weights = np.array([e[2] for e in g['edges']])
        initial_regions = reference['region_labels']
        arm_metrics = {a['arm']: a['metrics'] for a in summary['arms']}
        np.testing.assert_allclose(arm_metrics['reference']['initial_energy'],
                                   arm_metrics['curves']['initial_energy'], rtol=0, atol=0)
        for arm in summary['arms']:
            name = arm['arm']; source = folder/name
            output = args.output/mesh/name; output.mkdir(parents=True, exist_ok=True)
            data = np.load(source/'packed.npz'); moves = np.load(source/'moves.npz')
            unpacked = json.loads((source/'unpacked.json').read_text())
            for key in ('vertices', 'faces', 'region_labels'):
                np.testing.assert_array_equal(data[key], reference[key])
            np.testing.assert_array_equal(data['labels'], moves['labels'][-1])
            for labels in moves['labels']:
                np.testing.assert_array_equal(np.unique(labels), np.unique(reference['labels']))
                for label in np.unique(labels):
                    if len(np.unique(initial_regions[labels == label])) != 1:
                        raise ValueError('stage crossed original region')
            border = ba.boundary_metrics(g, initial_regions, data['labels'], data['corner_uv'])
            np.testing.assert_allclose(weights@moves['final_seams'],
                                       border['total_uv_seam_length_over_sqrt_area'], rtol=1e-12)
            keys = [(*e['pair'], *e['revisions']) for e in unpacked['events']]
            if len(set(keys)) != len(keys):
                raise ValueError('repeated pair/revision attempt')
            accepted = [e for e in unpacked['events'] if e['status'] == 'accepted']
            if len(accepted) != len(moves['labels'])-1 or any(e['energy_after'] >= e['energy_before'] for e in accepted):
                raise ValueError('accepted move count/energy mismatch')
            uv = data['corner_uv']; a = uv[:, 1]-uv[:, 0]; b = uv[:, 2]-uv[:, 0]
            square = float(np.abs(a[:, 0]*b[:, 1]-a[:, 1]*b[:, 0]).sum()/2)
            packed = json.loads((source/'packed.json').read_text()); metrics = packed['metrics']
            violation = max(float(not metrics['all_charts_valid']), metrics['max_stretch']-1.35,
                            metrics['max_anisotropy']-2., float(border['lost_baseline_edges']), 0.)
            row = dict(mesh=mesh, arm=name, metrics=arm['metrics'],
                       postpack_boundaries=border, square_texture_occupancy=square,
                       reconciled_stages=len(moves['labels']), quality_error_linf=violation)
            rows.append(row)
            raw = dict(benchmark_id=manifest['benchmark_id'], method=manifest['method'], dataset=manifest['dataset'],
                       backend='cpu_reference', commit='local-dev', status='passed',
                       metrics=dict(quality_error_linf=violation), diagnostics=dict(cell=row,
                       limitation='Frozen local diagnostic; support score is optimized evidence, not anatomical ground truth. No runtime, timing or uniform-quality claim.'))
            sealed = canonicalize(raw, manifest_path=manifest_path, manifest=manifest, manifests_root=ROOT/'benchmarks',
                                  run_id=f'method046-{mesh}-{name}', attempt_id='attempt-003',
                                  source_state='dirty_worktree', source_revision='local-dev', claim_eligible=False,
                                  snapshot_sha256=None, diff_sha256=None)
            benchmarks = args.output/'benchmarks'; benchmarks.mkdir(exist_ok=True)
            (benchmarks/f'{mesh}-{name}.json').write_text(json.dumps(sealed, indent=2, allow_nan=False)+'\n')
            for filename in ('unpacked.npz', 'unpacked.json', 'moves.npz', 'packed.npz', 'packed.json'):
                shutil.copyfile(source/filename, output/filename)
        for filename in ('results.json', 'scores.npz'):
            shutil.copyfile(folder/filename, args.output/mesh/filename)
    for filename in ('stages.json.gz', 'frog-comparison.png', 'sculpt-comparison.png'):
        shutil.copyfile(args.campaign/filename, args.output/filename)
    files = [ROOT/'tools/diagnostics/atlas'/name for name in (
        'boundary_refine.py', 'run_boundary_refinement.py', 'inspect_boundary_refinement.py',
        'collect_boundary_refinement.py', 'baseline_atlas.py', 'patch_merge.py', 'repack.py',
        'compare_atlases.py', 'trace_atlas_viewer.html')]
    files += [ROOT/'tests/regression/tooling/Test.AtlasBoundaryRefine.py', manifest_path,
              ROOT/'benchmarks/runners/UvChartPackDiagnosticRunner.cpp']
    record = dict(schema='intrinsic.uv-boundary-refinement.diagnostic.v1', claim_eligible=False,
                  source_state='dirty_worktree', cells=rows,
                  environment=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__,
                                   native_preset='ci', native_compiler='Clang 23'),
                  current_source={str(p.relative_to(ROOT)): digest(p) for p in files})
    record['artifacts'] = {str(p.relative_to(args.output)): digest(p) for p in sorted(args.output.rglob('*'))
                           if p.is_file() and p.name != 'record.json'}
    (args.output/'record.json').write_text(json.dumps(record, indent=2, allow_nan=False)+'\n')
    print(f'Reconciled {len(rows)} cells; maximum quality violation {max(r["quality_error_linf"] for r in rows)}')


if __name__ == '__main__':
    main()
