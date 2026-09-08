#!/usr/bin/env python3
"""Build a self-contained comparison of refinement moves and evidence fields."""
import argparse
import gzip
import json
from pathlib import Path

import numpy as np
import patch_merge as pm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--source', type=Path, default=Path('ara/evidence/diagnostics/method045/stages/stages.json.gz'))
    args = parser.parse_args()
    with gzip.open(args.source, 'rt') as stream:
        data = json.load(stream)
    for mesh in data['meshes']:
        folder = args.results/mesh['name']
        records = json.loads((folder/'results.json').read_text())
        initial = np.load(folder/'reference/unpacked.npz')
        g = pm.geometry(initial['vertices'], initial['faces'])
        mesh['stages'] = [dict(name='reference: Protected atlas', labels=initial['labels'].tolist(), seeds=[])]
        for arm in records['arms']:
            name = arm['arm']
            if name == 'reference':
                continue
            moves = np.load(folder/name/'moves.npz')['labels']
            attempts = json.loads((folder/name/'unpacked.json').read_text())['events']
            accepted = [e for e in attempts if e['status'] == 'accepted']
            for i, (labels, event) in enumerate(zip(moves[1:], accepted)):
                mesh['stages'].append(dict(name=f'{name}: Move {i+1}', labels=labels.tolist(), seeds=[], decision=event))
            metrics = arm['metrics']
            mesh['stages'].append(dict(name=name+': Final', labels=moves[-1].tolist(), seeds=[],
                                      decision={k: metrics[k] for k in ('accepted_moves', 'moved_faces',
                                                'additional_seam_length', 'curve_support_mean',
                                                'lost_baseline_edges', 'max_stretch')}))
        scores = np.load(folder/'scores.npz')
        for key in ('soft', 'curve', 'combined', 'shuffled'):
            field = np.zeros(len(g['f']))
            for value, (a, b, *_) in zip(scores[key], g['edges']):
                field[a] = max(field[a], value); field[b] = max(field[b], value)
            mesh['stages'].append(dict(name='Score: '+key, labels=initial['labels'].tolist(), seeds=[],
                                      scalars=field.tolist(), range=[0, 1], units='max incident edge score'))
    template = Path(__file__).with_name('trace_atlas_viewer.html').read_text()
    replacements = {
        'METHOD-045 / observed reference decisions': 'METHOD-046 / constrained boundary relocation',
        'Where do useful boundaries disappear?': 'Can additional UV borders follow the features?',
        'Curvature curves are diagnostic overlays; the atlas does not use them.':
            'Curve-guided moves use the large-scale principal curves. Other curve families and scales remain overlays.',
        'Colors identify clusters within a stage; matching colors across stages do not imply corresponding parts.':
            'Chart colors and IDs correspond across these stages; original region membership stays fixed.',
        'Local CPU replay, with unchanged algorithm parameters.':
            'Local CPU experiment: beta 0.8, six-hop frozen bands, three rounds, stretch ≤ 1.35 and anisotropy ≤ 2.',
        'Native baseline merge records interleave refinement and cannot reconstruct every native step.':
            'Every accepted boundary move is shown. Rejected attempts are retained in each arm’s unpacked.json.',
        'Curves are overlays, not atlas decision inputs.':
            'Large-scale principal curves supply the curve score; confidence is not ground truth.',
        "r.name==='protected: Final charts before native packing'": "r.name==='curves: Final'",
        'observed stages and scalar views. Exact final partitions match saved results. Native baseline: initial/final only; Python atlases: all accepted splits and merges. Rejected attempts are in the neighboring decisions JSON.':
            'accepted boundary moves, final partitions and score fields. Original regions and chart count are fixed. Compare the length-only control before attributing changes to curvature guidance.',
        '<option value="1" selected>Medium</option><option value="2">Large</option>':
            '<option value="1">Medium</option><option value="2" selected>Large</option>',
    }
    for before, after in replacements.items():
        if before not in template:
            raise ValueError('viewer template changed: '+before)
        template = template.replace(before, after)
    payload = json.dumps(data, separators=(',', ':'), allow_nan=False)
    (args.results/'index.html').write_text(template.replace('__STAGE_DATA__', payload))
    with gzip.open(args.results/'stages.json.gz', 'wt') as stream:
        stream.write(payload)
    print(f'Wrote {sum(len(m["stages"]) for m in data["meshes"])} inspectable stages')


if __name__ == '__main__':
    main()
