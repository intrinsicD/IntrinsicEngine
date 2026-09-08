#!/usr/bin/env python3
"""Replay a frozen, five-arm boundary-refinement diagnostic on source-bound inputs."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import boundary_refine as br
import patch_merge as pm


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def run(mesh, source, output, *, band_hops=6, rounds=3, beta=.8, curve_scale=2):
    source = source.resolve(); output = output.resolve(); output.mkdir(parents=True, exist_ok=True)
    atlas_path = source/'examples'/mesh/'result.npz'
    curve_path = source/'stages'/f'{mesh}.extrema.json.gz'
    edge_path = source/'stages'/f'{mesh}.edges.gz'
    data = np.load(atlas_path)
    v, f, regions, initial = (data[k] for k in ('vertices', 'faces', 'region_labels', 'labels'))
    g = pm.geometry(v, f)
    with gzip.open(curve_path, 'rt') as stream:
        curves = json.load(stream)
    with gzip.open(edge_path, 'rt') as stream:
        rows = np.loadtxt(stream)
    hard, soft = br.edge_evidence(g, rows)
    curve = br.curve_support(g, v, curves, curve_scale); combined = np.maximum(soft, curve)
    shuffled = np.random.default_rng(20260908).permutation(combined)
    np.savez_compressed(output/'scores.npz', hard=hard, soft=soft, curve=curve, combined=combined, shuffled=shuffled)
    length = np.array([e[2] for e in g['edges']])
    protected = np.array([regions[a] != regions[b] for a, b, *_ in g['edges']])
    original = np.array([initial[a] != initial[b] for a, b, *_ in g['edges']])
    parameters = dict(band_hops=band_hops, rounds=rounds, beta=beta, curve_scale=curve_scale,
                      stretch_limit=1.35, anisotropy_limit=2., shuffle_seed=20260908)
    records = []
    for arm, score, weight, iterations in (
            ('reference', combined, beta, 0), ('length', combined, 0., rounds),
            ('native', soft, beta, rounds), ('curves', combined, beta, rounds),
            ('shuffled', shuffled, beta, rounds)):
        print(f'{mesh}: {arm} refinement', flush=True)
        folder = output/arm; folder.mkdir(exist_ok=True)
        result = br.execute(v, f, regions, initial, score, hard=hard, beta=weight,
                            band_hops=band_hops, rounds=iterations)
        np.savez_compressed(folder/'unpacked.npz', vertices=v, faces=f,
                            **{k: result[k] for k in ('labels', 'region_labels', 'chart_uv', 'corner_uv')})
        np.savez_compressed(folder/'moves.npz', labels=np.stack(result['stages']),
                            initial_seams=result['initial_seams'], final_seams=result['final_seams'])
        write_json(folder/'unpacked.json', dict(parameters=dict(parameters, beta=weight, rounds=iterations), stretch_limit=1.35,
                                               metrics=result['metrics'], events=result['events']))
        call = subprocess.run([sys.executable, str(Path(__file__).with_name('repack.py')),
                               str(folder/'unpacked.npz'), str(folder/'packed.json')], capture_output=True, text=True)
        (folder/'pack.log').write_text(call.stdout+call.stderr)
        if call.returncode:
            raise RuntimeError(f'{mesh}/{arm} postpack failed; see {folder}/pack.log')
        packed = json.loads((folder/'packed.json').read_text())
        seams = result['final_seams']; additional = seams & ~protected
        charts = np.array([result['labels'][a] != result['labels'][b] for a, b, *_ in g['edges']])
        additional_length = float(length@additional)
        metrics = dict(result['metrics'], postpack=packed['metrics'],
                       postpack_bound_passed=packed['postpack_bound_passed'],
                       additional_seam_length=additional_length,
                       common_guided_energy=float((length*(1-beta*combined))@seams),
                       curve_support_mean=float((length*curve)@additional/additional_length) if additional_length else 0.,
                       native_support_mean=float((length*soft)@additional/additional_length) if additional_length else 0.,
                       soft_chart_edges_gained=int(np.count_nonzero(charts & ~original & (soft > 0))),
                       soft_chart_edges_lost=int(np.count_nonzero(original & ~charts & (soft > 0))),
                       chart_area_min_ratio=min(float(g['area'][result['labels'] == i].sum()/g['area'][initial == i].sum()) for i in np.unique(initial)))
        record = dict(arm=arm, beta=weight, rounds=iterations, metrics=metrics)
        records.append(record)
        write_json(output/'results.json', dict(mesh=mesh, claim_eligible=False, parameters=parameters, arms=records,
                   inputs={str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (atlas_path, curve_path, edge_path)}))
        print(f'{mesh}: {arm}: {metrics["accepted_moves"]} accepted, '
              f'{metrics["moved_faces"]} moved faces, support {metrics["curve_support_mean"]:.5f}, '
              f'additional length {additional_length:.5f}, statuses {metrics["statuses"]}', flush=True)

    np.testing.assert_allclose(records[0]['metrics']['initial_energy'],
                               next(r for r in records if r['arm'] == 'curves')['metrics']['initial_energy'],
                               rtol=0, atol=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mesh'); parser.add_argument('output', type=Path)
    parser.add_argument('--source', type=Path, default=Path('ara/evidence/diagnostics/method045'))
    parser.add_argument('--band-hops', type=int, default=6); parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--beta', type=float, default=.8); parser.add_argument('--curve-scale', type=int, default=2)
    args = parser.parse_args()
    run(args.mesh, args.source, args.output, band_hops=args.band_hops, rounds=args.rounds,
        beta=args.beta, curve_scale=args.curve_scale)


if __name__ == '__main__':
    main()
