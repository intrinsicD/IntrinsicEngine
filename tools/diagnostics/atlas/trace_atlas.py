#!/usr/bin/env python3
"""Observe atlas decisions without changing them; export source-bound stage views.

The profiler records actual reference calls, including rejected UV candidates.
It does not reimplement growth, splitting, merge ordering, or acceptance.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import patch_merge as pm
import baseline_atlas as ba


class Trace:
    def __init__(self, arm, g, regions, features):
        self.arm, self.g, self.regions, self.features = arm, g, regions, features
        self.labels = regions.copy()
        self.stages, self.events = [], []
        self.merge_started = False
        self.distance = None

    def snapshot(self, name, **extra):
        self.stages.append(dict(name=name, labels=self.labels.tolist(), **extra))

    def chart_labels(self, charts):
        for i, chart in charts.items():
            self.labels[chart['ids']] = i

    def begin_merge(self, charts):
        if not self.merge_started:
            self.chart_labels(charts)
            self.snapshot('Validated charts before merging')
            self.merge_started = True

    def profile(self, frame, event, value):
        if event not in ('call', 'return'):
            return
        code, local, parent = frame.f_code, frame.f_locals, frame.f_back
        if code == ba.merge_within_regions.__code__ and event == 'call':
            self.begin_merge(local['charts'])
        if event != 'return':
            return
        if code == pm.propagate.__code__ and parent.f_code == pm.execute.__code__:
            self.distance, labels = value
            self.labels = labels.copy()
            self.snapshot('Initial 64 seeded clusters', seeds=list(map(int, local['seeds'])))
        elif code == ba.region_components.__code__ and parent.f_code == ba.execute.__code__:
            for i, ids in enumerate(value):
                self.labels[ids] = i
            self.snapshot('Original baseline components')
        elif code == pm.bisect.__code__:
            fresh = int(self.labels.max()) + 1
            for i, ids in enumerate(value):
                self.labels[ids] = fresh + i
            self.events.append(dict(kind='split', faces=len(local['ids']), stage=len(self.stages)-1))
            self.snapshot(f'Split {sum(x["kind"] == "split" for x in self.events)}')
        elif code == ba.open_region.__code__ and value[0] is None:
            context = 'merge' if parent.f_code == ba.merge_within_regions.__code__ else 'initial'
            self.events.append(dict(kind=context, status=value[1], faces=len(local['ids']),
                                    stage=len(self.stages)-1, topology=value[2]))
        elif code == pm.parameterize_local.__code__:
            caller = parent.f_back if parent.f_code == pm.parameterize.__code__ else parent
            outer = caller.f_locals
            merging = caller.f_code == ba.merge_within_regions.__code__ or (
                caller.f_code == pm.execute.__code__ and 'attempts' in outer)
            if merging:
                self.begin_merge(outer['charts'])
            chart, reason = value
            measured = local.get('measured')
            record = dict(kind='merge' if merging else 'initial', status=reason,
                          faces=len(local['ids']), stage=len(self.stages)-1)
            if measured is not None:
                record.update(max_stretch=measured['max_stretch'],
                              max_anisotropy=measured['max_anisotropy'])
            if merging:
                a, b = (outer['a'], outer['b']) if self.arm == 'feature' else (outer['i'], outer['j'])
                seam = [e for e in self.g['edges'] if {int(self.labels[e[0]]), int(self.labels[e[1]])} == {a, b}]
                length = sum(e[2] for e in seam)
                angle = sum(e[2]*e[3] for e in seam)/length
                area = min(self.g['area'][outer['charts'][i]['ids']].sum() for i in (a, b))
                score = length/np.sqrt(area)/(1+2*angle if self.arm == 'feature' else 1)
                record.update(a=int(a), b=int(b), score=float(score), shared_length=length,
                              mean_dihedral_degrees=float(np.degrees(angle)),
                              removed_baseline_edges=sum(self.regions[e[0]] != self.regions[e[1]] for e in seam),
                              removed_hard_edges=sum(self.features.get(tuple(sorted(e[5:7])), (0, 0))[0] > 0 for e in seam),
                              removed_soft_edges=sum(self.features.get(tuple(sorted(e[5:7])), (0, 0))[1] > 0 for e in seam))
                if chart is not None:
                    self.labels[chart['ids']] = outer['next_id']
                    self.snapshot(f'Merge {sum(x["kind"] == "merge" and x.get("status") == "accepted" for x in self.events)+1}', decision=record.copy())
            self.events.append(record)
        elif code == pm.pack.__code__:
            self.chart_labels(local['charts'])
            self.snapshot('Final charts before native packing')


def scalar(name, labels, values, units, **extra):
    values = np.asarray(values, float)
    finite = values[np.isfinite(values)]
    lo, hi = np.percentile(finite, [2, 98])
    if hi <= lo:
        hi = lo+1
    result = dict(name=name, labels=labels.tolist(), scalars=[float(x) if np.isfinite(x) else None for x in values],
                  range=[float(lo), float(hi)], units=units)
    result.update(extra)
    return result


def inspect(mesh, native, curves, output, references):
    source_path = native/f'{mesh}.geometry.json'
    source = json.loads(source_path.read_text())
    v, f = np.asarray(source['positions']), np.asarray(source['triangles'], np.int64)
    regions = np.loadtxt(native/f'{mesh}.labels', dtype=np.int64)
    stage_data = json.loads((native/f'{mesh}.stages.json').read_text())
    edges = np.loadtxt(native/f'{mesh}.edges')
    features = {tuple(sorted(map(int, row[:2]))): tuple(row[2:4]) for row in edges}
    g = pm.geometry(v, f)
    extrema = json.loads(curves.read_text())
    assert np.array_equal(np.asarray(extrema['positions'], np.float32), np.asarray(v, np.float32))
    assert np.array_equal(extrema['triangles'], f)
    retained = Path('ara/evidence/diagnostics/method045')
    original = json.loads(gzip.decompress((retained/'baselines'/f'{mesh}.geometry.json.gz').read_bytes()))
    assert np.array_equal(v, original['positions']) and np.array_equal(f, original['triangles'])
    assert np.array_equal(regions, np.loadtxt(retained/'baselines'/f'{mesh}.labels', dtype=np.int64))
    stages = [dict(name='Native baseline: initial growth', labels=stage_data['initial_labels'], seeds=stage_data['seed_faces']),
              dict(name='Native baseline: final regions', labels=regions.tolist())]
    summary, traces = {}, {}
    for arm in ('feature', 'protected'):
        trace = Trace(arm, g, regions, features)
        sys.setprofile(trace.profile)
        try:
            result = pm.execute(v, f, patches=64, feature_weight=2., stretch_limit=1.35) if arm == 'feature' else ba.execute(v, f, regions, merge=True)
        finally:
            sys.setprofile(None)
        expected_path = references/f'{mesh}-{arm}-labels.npy'
        expected = np.load(expected_path)
        assert np.array_equal(result['labels'], expected), (mesh, arm, 'changed partition')
        metrics = result['metrics']
        merge_events = [x for x in trace.events if x['kind'] == 'merge']
        accepted = [x for x in merge_events if x['status'] == 'accepted']
        assert len(accepted) == metrics['accepted_merges']
        assert len(merge_events) == (metrics['merge_attempts'] if arm == 'feature' else len(metrics['merge_attempts']))
        summary[arm] = dict(initial_charts=metrics['initial_charts'], final_charts=metrics['chart_count'],
                            merge_attempts=len(merge_events), accepted_merges=len(accepted),
                            rejection_counts=dict(Counter(x['status'] for x in merge_events if x['status'] != 'accepted')),
                            removed_baseline_edges=sum(int(x.get('removed_baseline_edges', 0)) for x in accepted),
                            removed_hard_edges=sum(int(x.get('removed_hard_edges', 0)) for x in accepted),
                            removed_soft_edges=sum(int(x.get('removed_soft_edges', 0)) for x in accepted),
                            exact_retained_labels=True)
        for stage in trace.stages:
            stage['name'] = arm+': '+stage['name']
        if trace.distance is not None:
            trace.stages.append(scalar('feature: growth distance', np.array(trace.stages[0]['labels']), trace.distance, 'normalized graph cost'))
        stretches = np.zeros(len(f))
        for label in np.unique(result['labels']):
            ids = np.flatnonzero(result['labels'] == label)
            # Expand source corners to measure the same cut chart without welding.
            uv = result['chart_uv'][ids].reshape(-1, 2)
            positions = g['v'][f[ids]].reshape(-1, 3)
            measured = pm.measure_uv(positions, np.arange(3*len(ids)).reshape(-1, 3), uv)
            assert measured is not None
            stretches[ids] = measured['stretch']
        trace.stages.append(scalar(arm+': final local UV stretch', result['labels'], stretches, 'per-chart area normalized; limit 1.35', range=[1., 1.35]))
        stages.extend(trace.stages)
        traces[arm] = trace.events
        np.savez_compressed(output/f'{mesh}-{arm}-stages.npz', labels=np.asarray([s['labels'] for s in trace.stages]))
    dihedral = np.zeros(len(f)); soft = dihedral.copy()
    for a, b, _, angle, _, i, j in g['edges']:
        dihedral[[a,b]] = np.maximum(dihedral[[a,b]], np.degrees(angle))
        soft[[a,b]] = np.maximum(soft[[a,b]], features.get(tuple(sorted((i,j))), (0,0))[1])
    stages += [scalar('Scalar: max incident dihedral', regions, dihedral, 'degrees'),
               scalar('Scalar: native soft feature confidence', regions, soft, 'max incident edge score', range=[0.,1.]),
               scalar('Scalar: native baseline growth cost', regions, stage_data['growth_cost'], 'normalized native graph cost')]
    for field in ('k1','k2'):
        stages.append(scalar('Scalar: signed '+field, regions, np.asarray(stage_data[field])[f].mean(axis=1), 'inverse source length; vertex mean per face'))
    curve_fields = {}
    for scale in range(3):
        for field in ('confidence','strength','fit_residual','sharpness'):
            values = np.full(len(f), np.nan)
            for s in extrema['segments']:
                if s['scale'] == scale and s['kind'].startswith('principal'):
                    values[s['face']] = max(s[field], values[s['face']]) if np.isfinite(values[s['face']]) else s[field]
            if np.isfinite(values).any():
                curve_fields[f'{field}_{scale}'] = [float(x) if np.isfinite(x) else None for x in values]
    for field in ('confidence','strength','fit_residual','sharpness'):
        key = f'{field}_{1 if mesh == "frog" else 2}'
        stages.append(scalar('Unused curve scalar: '+field, regions, curve_fields[key], 'max principal segment per face; gray = no segment'))
    record = dict(schema='intrinsic.atlas-stage-audit.v1', claim_eligible=False, mesh=mesh,
                  parameters=dict(patches=64, feature_weight=2., stretch_limit=1.35, anisotropy_limit=2.),
                  summary=summary, events=traces, native_merge_history=stage_data['merges'],
                  native_refinement_move_count=stage_data['refinement_move_count'],
                  source_sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(),
                  curvature_sha256=hashlib.sha256(curves.read_bytes()).hexdigest(),
                  source_files={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),Path(pm.__file__),Path(ba.__file__)]},
                  limitations=['Native merge history interleaves refinement; native initial/final views are not every native step.',
                               'Curve overlays are inspection evidence, not a quantified curve-alignment oracle.',
                               'Scalar percentiles set display ranges only; no algorithm threshold is changed.'])
    (output/f'{mesh}-decisions.json').write_text(json.dumps(record, indent=2, default=lambda x: x.item(), allow_nan=False)+'\n')
    print(json.dumps(dict(mesh=mesh, summary=summary), allow_nan=False), flush=True)
    return dict(name=mesh, positions=v.ravel().tolist(), triangles=f.ravel().tolist(),
                sharedEdges=[[int(e[5]),int(e[6]),int(e[0]),int(e[1]),*features.get(tuple(sorted(e[5:7])),(0,0))] for e in g['edges']],
                stages=stages, curvePoints=[p['position'] for p in extrema['points']], curveSegments=extrema['segments'],
                sha256=record['source_sha256'], curveParameters=extrema['parameters'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--curves', type=Path, default=Path('build/method041-inspection-barriers'))
    parser.add_argument('--references',type=Path,default=Path('ara/evidence/diagnostics/method045/stages/references'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    data = dict(meshes=[inspect(mesh,args.native,args.curves/f'{i}-{mesh}.extrema.json',args.output,args.references) for i,mesh in enumerate(('frog','sculpt'))])
    (args.output/'stages.json.gz').write_bytes(gzip.compress(json.dumps(data, separators=(',',':'), default=lambda x: x.item(), allow_nan=False).encode(), mtime=0))
    template = Path(__file__).with_name('trace_atlas_viewer.html').read_text()
    (args.output/'index.html').write_text(template.replace('__STAGE_DATA__',json.dumps(data,separators=(',',':'),default=lambda x: x.item(),allow_nan=False).replace('<','\\u003c')))


if __name__ == '__main__':
    main()
