#!/usr/bin/env python3
"""Bounded METHOD-078 diagnostic pilot; no production-policy acceptance or speedup claim."""
import argparse
import hashlib
import html
import json
import math
import re
import resource
import signal
import struct
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PACKET = HERE / 'preflight/pilot'
OUT = HERE / 'runs'
BINS = Path('/tmp/cpd-nystrom-usage-20261011/bin')
MANIFEST = ['/usr/bin/python3', '/home/alex/.codex/skills/experiment-preflight/manifest.py',
            '--exclude', 'tasks/evidence/METHOD-078/preflight/pilot',
            '--exclude', 'tasks/evidence/METHOD-078/reviews',
            '--exclude', 'tasks/evidence/METHOD-078/runs',
            str(BINS), 'tasks/evidence/METHOD-078/preflight/pilot/layer-a.md',
            'tasks/evidence/METHOD-078/preflight/pilot/layer-b.md']

def plan():
    controls = [('exact', 'baseline', 'exact', 256, .001),
                ('floor', 'baseline', 'points/fixed/random', 4096, .001),
                ('negative', 'baseline', 'points/fixed/random', 2, 1e300)]
    pairs = [(arm + '_' + name, arm, variant, 256, .001)
             for name, variant in [('points', 'points/fixed/random'), ('box', 'box/resampled/random')]
             for arm in ('baseline', 'candidate')]
    return controls + [(x[0] + '_1', *x[1:]) for x in pairs] + [(x[0] + '_2', *x[1:]) for x in reversed(pairs)]

def command(cell, folder):
    name, arm, variant, landmarks, error = cell
    args = ['taskset', '-c', '0-7', str(BINS / arm), '--fixture', 'full', '--seed', '1',
            '--points', '100000', '--threads', '8', '--iterations', '20', '--trace', '1',
            '--variant', variant, '--landmarks', str(landmarks), '--error-limit', str(error),
            '--out', str(folder / (name + '.json')), '--positions', str(folder / (name + '.bin'))]
    if name != 'exact':
        args += ['--reference', str(folder / 'exact.bin')]
    return args

def traces(path):
    iterations, attempts, begins, costs, stages = [], [], [], [], []
    for line in path.read_text().splitlines(keepends=True):
        if not line.endswith('\n'): break  # Interrupted final write; retain earlier complete events.
        p = line.split()
        if p[:1] == ['CPD_ITER']:
            assert len(p) == 10, line
            values = [float(x) for x in p[1:]]
            assert all(math.isfinite(x) for x in values), line
            iterations.append(values)
        elif p[:2] == ['NYSTROM_DIAG', 'attempt']:
            attempts.append((int(p[2]), int(p[3]), int(p[4]), float(p[5])))
        elif p[:2] == ['NYSTROM_DIAG', 'begin']:
            begins.append((int(p[2]), float(p[3]), float(p[4])))
        elif p[:2] == ['NYSTROM_DIAG', 'cost']:
            costs.append(int(p[2]))
        elif p[:1] == ['CPD_STAGE']:
            assert len(p) == 3 and math.isfinite(float(p[2])), line
            stages.append((p[1], float(p[2])))
    return iterations, attempts, begins, costs, stages

def assessed(rows, failure=False):
    if failure: return 'UNEVALUATED: execution failed or timed out'
    expected = {c[0] for c in plan()}
    if set(rows) != expected:
        return 'INCOMPLETE: no comparison'
    for name, row in rows.items():
        keys = ('nystrom_iterations', 'iterations', 'sigma2', 'seconds')
        if name != 'exact': keys += ('rms_to_exact', 'max_to_exact')
        if any(not isinstance(row.get(key), (int, float)) or not math.isfinite(row[key])
               for key in keys):
            return 'INVALID: missing or non-finite result'
    if rows['exact']['nystrom_iterations'] or rows['floor']['nystrom_iterations']:
        return 'INVALID: exact/floor control used the approximation'
    if rows['floor']['rms_to_exact'] > 2e-4 or rows['negative']['rms_to_exact'] <= .001:
        return 'INVALID: parity/control sensitivity'
    for name, row in rows.items():
        if name not in ('exact', 'negative') and (row['rms_to_exact'] > .001 or row['max_to_exact'] > .01):
            return 'INVALID: equivalence failed'
        if name != 'negative' and (row['status'] != rows['exact']['status'] or row['termination'] != rows['exact']['termination']
                                   or row['iterations'] != rows['exact']['iterations']):
            return 'INVALID: outcome changed'
    for variant in ('points', 'box'):
        baseline = [rows[f'baseline_{variant}_{r}'] for r in (1, 2)]
        candidate = [rows[f'candidate_{variant}_{r}'] for r in (1, 2)]
        for repeats in (baseline, candidate):
            for key in ('nystrom_iterations', 'iterations', 'sigma2', 'rms_to_exact', 'max_to_exact'):
                if repeats[0][key] != repeats[1][key]:
                    return 'INVALID: non-deterministic repeat'
        if max(x['seconds'] for x in candidate) >= .9 * min(x['seconds'] for x in baseline):
            return 'DO NOT ADOPT: retries did not clear the predeclared timing margin in both variants'
    if not any(rows[f'candidate_{v}_1']['nystrom_iterations'] > rows[f'baseline_{v}_1']['nystrom_iterations'] for v in ('points', 'box')):
        return 'DO NOT ADOPT: no additional accepted iterations'
    return 'FOLLOW-UP REQUIRED: pilot passed; no production adoption or general speedup claim'

def report():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    rows, problems = {}, []
    for cell in plan():
        path = OUT / (cell[0] + '.json')
        if path.exists():
            try:
                rows[cell[0]] = json.loads(path.read_text())
            except ValueError as error:
                problems.append(f'{cell[0]}: invalid result JSON: {error}')
    failure_path = OUT / 'failure.json'
    failure = json.loads(failure_path.read_text()) if failure_path.exists() else None
    preview = {'source': [], 'target': [], 'truth': []}
    for line in (HERE / 'fixture-preview.txt').read_text().splitlines():
        match = re.match(r'(source|target|truth)\[(\d+)\] (.*)', line)
        if match:
            preview[match[1]].append((int(match[2]), tuple(map(float, match[3].split()))))
    body = ['<h1>METHOD-078 Nyström retry diagnostic pilot</h1>', '<p>' + html.escape(assessed(rows, bool(failure))) + '</p>',
            '<p>One synthetic full-overlap fixture, seed 1, 100,000 points; 20 iterations; eight physical cores. '
            'This is not a completed-registration ranking. Truth error is reported, not gated. '
            'Attempt timings include identical diagnostic logging in both binaries. No performance claim is eligible.</p>']
    for cell in plan():
        name = cell[0]
        log = OUT / (name + '.stderr')
        if name not in rows and not log.exists():
            continue
        row = rows.get(name)
        try:
            values, attempts, begins, costs, stages = traces(log)
            if row is not None:
                assert len(values) == row['observed_iterations'] == row['iterations'], 'incomplete iteration trace'
                assert sum(x[5] == 5 for x in values) == row['nystrom_iterations'], 'policy/count mismatch'
        except (OSError, ValueError, AssertionError, IndexError) as error:
            problems.append(f'{name}: invalid/incomplete trace: {error}')
            values, attempts, begins, costs, stages = [], [], [], [], []
        attempted = {x[0] for x in attempts}
        suppressed = [i for i, sigma, rejected in begins if sigma <= rejected and i not in attempted and i not in costs]
        body += [f'<h2>{html.escape(name)}</h2>', '<pre>' + html.escape(json.dumps(row, indent=2) if row else 'No final result; partial execution only') + '</pre>',
                 f'<p>Attempts: {len(attempts)}; suppressed iterations: {suppressed}; cost-cap stops: {costs}.</p>',
                 '<pre>' + html.escape('\n'.join(line for line in (log.read_text() if log.exists() else '').splitlines()
                                                 if line.startswith('NYSTROM_DIAG') and not line.startswith('NYSTROM_DIAG begin'))) + '</pre>']
        fig, axes = plt.subplots(3, 2, figsize=(12, 9))
        x = [v[0] for v in values]
        for ax, col, label in [(axes[0, 0], 2, 'NLL (raw and weighted; weight 1)'),
                               (axes[0, 1], 3, 'Stopping objective'), (axes[1, 0], 1, 'Post-M-step sigma² (world units²)'),
                               (axes[1, 1], 6, 'Accepted sampled error'), (axes[2, 0], 5, 'Actual E-step policy')]:
            ax.plot(x, [v[col] for v in values]); ax.set_title(label); ax.set_xlabel('Iteration')
            for boundary in suppressed[:1]: ax.axvline(boundary, color='red', linestyle=':')
        axes[0, 1].plot(x, [v[3] - v[2] for v in values], label='prior contribution (BCPD: 0)'); axes[0, 1].legend()
        labels = list(dict.fromkeys(stage for stage, seconds in stages))
        axes[2, 1].step([seconds for stage, seconds in stages], [labels.index(stage) for stage, seconds in stages], where='post')
        for stage, seconds in stages: axes[2, 1].axvline(seconds, color='grey', alpha=.15)
        axes[2, 1].set_yticks(range(len(labels)), labels)
        axes[2, 1].set(xlabel='Elapsed seconds', title='Recorded stage boundaries')
        fig.tight_layout(); fig.savefig(OUT / (name + '.png')); plt.close(fig)
        body.append(f'<img width="1000" src="{name}.png" alt="Iteration diagnostics for {name}">')
        position_path = OUT / (name + '.bin')
        if not position_path.exists():
            body.append('<p>No final position buffer; before/after result unavailable.</p>')
            if row is not None: problems.append(f'{name}: missing final positions')
            continue
        positions = position_path.read_bytes()
        stride = len(positions) // 100000
        if stride not in (24, 32) or len(positions) != 100000 * stride:
            problems.append(f'{name}: incomplete position buffer')
            continue
        before = [p for i, p in preview['source']]
        after = [struct.unpack_from('ddd', positions, i * stride) for i, p in preview['source']]
        target = [p for i, p in preview['target']]
        truth = [p for i, p in preview['truth']]
        limits = [(min(p[k] for p in before + after + target + truth), max(p[k] for p in before + after + target + truth)) for k in range(3)]
        view = plt.figure(figsize=(12, 5))
        for index, (points, title) in enumerate([(before, 'Before'), (after, 'After')], 1):
            ax = view.add_subplot(1, 2, index, projection='3d')
            for cloud, label in [(target, 'target'), (truth, 'truth'), (points, 'source')]:
                ax.scatter(*zip(*cloud), s=3, label=label)
            ax.set(xlim=limits[0], ylim=limits[1], zlim=limits[2], title=title); ax.legend()
        view.tight_layout(); view.savefig(OUT / (name + '-positions.png')); plt.close(view)
        body.append(f'<img width="1000" src="{name}-positions.png" alt="Before and after, same axes, seeded point subset">')
        body.append(f'<p><a href="{name}.stderr">Raw attempts and timed stage boundaries</a></p>')
    if failure or problems:
        body.insert(1, '<h2>INVALID / INCOMPLETE EXECUTION</h2><pre>' + html.escape(json.dumps({'failure': failure, 'problems': problems}, indent=2)) + '</pre>')
    (OUT / 'report.html').write_text('<!doctype html><meta charset="utf-8"><title>METHOD-078</title>' + '\n'.join(body))
    return not failure and not problems

def self_test():
    global OUT
    assert len(plan()) == 11 and len({c[0] for c in plan()}) == 11
    assert assessed({}).startswith('INCOMPLETE')
    base = dict(nystrom_iterations=1, rms_to_exact=0.0, max_to_exact=0.0, status='success', termination='iteration_cap',
                iterations=20, sigma2=.01, seconds=10.0)
    rows = {c[0]: dict(base) for c in plan()}
    rows['exact']['nystrom_iterations'] = rows['floor']['nystrom_iterations'] = 0
    del rows['exact']['rms_to_exact'], rows['exact']['max_to_exact']
    rows['negative']['rms_to_exact'] = .1
    for name in rows:
        if name.startswith('candidate'):
            rows[name]['nystrom_iterations'] = 2
    assert assessed(rows).startswith('DO NOT ADOPT')
    for name in rows:
        if name.startswith('candidate'):
            rows[name].update(seconds=8.0, nystrom_iterations=2)
    assert assessed(rows).startswith('FOLLOW-UP REQUIRED')
    assert assessed(rows, failure=True).startswith('UNEVALUATED')
    rows['candidate_box_1']['rms_to_exact'] = .1
    assert assessed(rows).startswith('INVALID')
    rows['candidate_box_1']['rms_to_exact'] = None
    assert assessed(rows).startswith('INVALID')
    original = OUT
    try:
        with tempfile.TemporaryDirectory() as directory:
            OUT = Path(directory)
            (OUT / 'exact.stderr').write_text('CPD_STAGE preparing 0\nCPD_STAGE expectation_step 0.1\nCPD_ITER 0 0.1 -2 -2 10 1 0 100 0.2\nCPD_ITER 1 0.')
            (OUT / 'failure.json').write_text(json.dumps({'cell': 'exact', 'error': 'TimeoutExpired'}))
            assert len(traces(OUT / 'exact.stderr')[0]) == 1
            assert traces(OUT / 'exact.stderr')[4] == [('preparing', 0.0), ('expectation_step', .1)]
            assert not report()
            page = (OUT / 'report.html').read_text()
            assert 'TimeoutExpired' in page and 'partial execution only' in page
            assert (OUT / 'exact.png').stat().st_size > 0
    finally:
        OUT = original

def limit_memory():
    resource.setrlimit(resource.RLIMIT_AS, (8 << 30, 8 << 30))

def run():
    approved = json.loads((HERE / 'reviews/approvals.json').read_text())
    frozen = (PACKET / 'manifest.txt').read_bytes()
    digest = hashlib.sha256(frozen).hexdigest()
    assert approved['digest'] == digest and approved['claude'] == approved['codex'] == 'PASS'
    OUT.mkdir(exist_ok=False)
    def expired(signum, frame):
        raise TimeoutError('total 3600-second budget exhausted')
    signal.signal(signal.SIGALRM, expired)
    signal.signal(signal.SIGTERM, expired)
    signal.alarm(3600)
    start = time.monotonic()
    name = 'before-first-cell'
    try:
        for cell in plan():
            name = cell[0]
            live = subprocess.check_output(MANIFEST, cwd=ROOT)
            assert live == frozen, 'manifest drift'
            remaining = 3600 - (time.monotonic() - start)
            if remaining <= 0: raise TimeoutError('total 3600-second budget exhausted')
            args = command(cell, OUT)
            (OUT / (name + '.command.json')).write_text(json.dumps(args))
            print('Starting', name, flush=True)
            with (OUT / (name + '.stdout')).open('w') as stdout, (OUT / (name + '.stderr')).open('w') as stderr:
                result = subprocess.run(args, cwd=ROOT, stdout=stdout, stderr=stderr, timeout=min(300, remaining), check=False, preexec_fn=limit_memory)
            (OUT / (name + '.resources.json')).write_text(json.dumps({'max_completed_child_rss_kib_so_far': resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}))
            if result.returncode: raise RuntimeError(f'{name}: runner exit {result.returncode}; preserved incomplete output')
            if not report(): raise RuntimeError(f'{name}: invalid diagnostic output')
        print(assessed({c[0]: json.loads((OUT / (c[0]+'.json')).read_text()) for c in plan()}), flush=True)
    except Exception as error:
        signal.alarm(0)
        (OUT / 'failure.json').write_text(json.dumps({'cell': name, 'error': f'{type(error).__name__}: {error}'}))
        report()
        raise
    finally:
        signal.alarm(0)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('mode', choices=['plan', 'self-test', 'run', 'report'])
    mode = parser.parse_args().mode
    if mode == 'plan': print(json.dumps([command(c, OUT) for c in plan()], indent=2))
    elif mode == 'self-test': self_test(); print('PASS')
    elif mode == 'report': report()
    else: run()
