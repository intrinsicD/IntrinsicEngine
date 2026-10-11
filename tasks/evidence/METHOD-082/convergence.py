#!/usr/bin/env python3
"""METHOD-082 frozen CPU diagnostic. Timings are descriptive, not a performance claim."""
import hashlib, html, json, math, resource, signal, subprocess, sys, time
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'METHOD-081'))
from compare import distances, traces, quality

OUT = HERE / 'runs'
BINS = Path('/tmp/cpd-nystrom-repair-20261011/bin')
N, CAP, CELL_SECONDS = 100000, 5000, 900
TOTAL_SECONDS = 34 * CELL_SECONDS + 600
VARIANTS = {'auto': 'exact', 'points': 'points/fixed/random', 'box': 'box/resampled/random'}


def cells():
    result = []
    for fixture in ('full', 'partial'):
        for seed in (1, 2, 3):
            arms = [('baseline', 'auto')] + [(a, v) for a in ('baseline', 'candidate') for v in ('points', 'box')]
            for arm, variant in arms:
                name = f'{fixture}_{seed}_{arm}_{variant}'
                result.append(dict(name=name, fixture=fixture, seed=seed, arm=arm, variant=variant))
    result += [dict(c, name=c['name'] + '_repeat') for c in reversed(result[1:5])]
    return result


def command(cell, folder):
    return ['taskset', '-c', '0-7', str(BINS / cell['arm']), '--fixture', cell['fixture'],
            '--seed', str(cell['seed']), '--points', str(N), '--threads', '8', '--iterations', str(CAP),
            '--trace', '1', '--variant', VARIANTS[cell['variant']], '--landmarks', '256', '--error-limit', '0.001',
            '--out', str(folder / (cell['name'] + '.json')), '--positions', str(folder / (cell['name'] + '.bin'))]


def manifest_command():
    return ['/usr/bin/python3', '/home/alex/.codex/skills/experiment-preflight/manifest.py',
            *sum((['--exclude', f'tasks/evidence/METHOD-082/{p}'] for p in ('preflight', 'reviews', 'runs', 'verification')), []),
            str(BINS), str(HERE / 'preflight/layer-a.md'), str(HERE / 'preflight/layer-b.md'),
            str(HERE / 'verification/check.log'), str(HERE / 'verification/technical.py'),
            str(HERE / 'verification/technical.log')]


def load_positions(path):
    p = np.fromfile(path, dtype='<f8')
    assert p.size == N * 3 and np.isfinite(p).all(), 'invalid successful endpoint'
    return p.reshape(N, 3)


def load_cell(cell, folder):
    d = json.loads((folder / (cell['name'] + '.json')).read_text())
    assert all(d[k] == v for k, v in dict(fixture=cell['fixture'], seed=cell['seed'], points=N,
               threads=8, variant=VARIANTS[cell['variant']], landmarks=256, error_limit=.001).items()), 'configuration mismatch'
    t, stages = traces(folder / (cell['name'] + '.stderr'))
    assert len(t) == d['iterations'] == d['observed_iterations'] and 0 <= len(t) <= CAP
    assert [r[0] for r in t] == list(range(len(t)))
    assert sum(r[5] == 5 for r in t) == d['nystrom_iterations']
    assert stages and math.isfinite(d['seconds']) and d['seconds'] > 0
    assert all(t[i][8] >= t[i - 1][8] for i in range(1, len(t)))
    p = None
    if d['status'] == 'success':
        assert d['termination'] in ('converged', 'iteration_cap', 'sigma_floor') and len(t) > 0
        assert d['termination'] != 'iteration_cap' or len(t) == CAP
        assert all(math.isfinite(d[k]) for k in ('sigma2', 'scale', 'rms_to_truth', 'max_sampled_error'))
        p = load_positions(folder / (cell['name'] + '.bin'))
    else:
        assert not (folder / (cell['name'] + '.bin')).exists(), 'failed registration published endpoint'
    return d, t, stages, p


def measure(d, p, data, reference):
    assert len(data['target']) == d['targets'] and len(data['source']) == len(data['truth']) == N
    row = dict(d)
    row['regular_convergence'] = d['status'] == 'success' and d['termination'] == 'converged'
    row['nystrom_fraction'] = d['nystrom_iterations'] / max(1, d['iterations'])
    row['auto_agreement'] = None
    row['truth_improvement'] = False
    if p is None:
        return row
    source, truth = data['source'].astype(float), data['truth'].astype(float)
    truth_rms, truth_max = distances(p, truth)
    assert abs(truth_rms - d['rms_to_truth']) <= 1e-5, 'native/dump truth mismatch'
    identity = distances(source, truth)[0]
    spread = lambda x: float(np.sqrt(np.mean(np.sum((x - x.mean(axis=0)) ** 2, axis=1))))
    ratio = spread(p) / spread(truth)
    row.update(truth_rms_recomputed=truth_rms, truth_max=truth_max, identity_truth_rms=identity,
               truth_rms_reduction=1 - truth_rms / identity, spread_to_truth=ratio,
               truth_improvement=truth_rms <= .9 * identity and .5 <= ratio <= 2.)
    clutter = np.arange(len(p)) % 20 == 0
    ambiguous = np.abs(truth[:, 0] - .2) <= 1e-5 if d['fixture'] == 'partial' else np.zeros(len(p), dtype=bool)
    observed = ~clutter & ~ambiguous
    hidden = np.zeros(len(p), dtype=bool)
    if d['fixture'] == 'partial':
        observed &= truth[:, 0] < .2
        hidden = ~clutter & ~ambiguous & (truth[:, 0] > .2)
    row['truth_subsets'] = {}
    for label, mask in [('observed', observed), ('hidden', hidden), ('clutter_replaced', clutter), ('boundary_ambiguous_nonclutter', ambiguous & ~clutter)]:
        row['truth_subsets'][label] = dict(count=int(mask.sum()), rms=distances(p[mask], truth[mask])[0] if mask.any() else None)
    if reference is not None:
        rms, maximum = distances(p, reference)
        row.update(rms_to_auto=rms, max_to_auto=maximum, auto_agreement=quality(rms, maximum))
    return row


def summarize(rows, repeats, outcomes):
    complete = len(outcomes) == len(cells()) and len(rows) == len(cells())
    all_success = len(rows) == len(cells()) and all(d['status'] == 'success' for d in rows.values())
    regular = all_success and all(d['regular_convergence'] for d in rows.values())
    agreement = all_success and all(d['auto_agreement'] is True for d in rows.values())
    replay = len(repeats) == 4 and all(repeats.values())
    learning = all_success and all(d['truth_improvement'] for d in rows.values())
    comparison = regular and agreement and replay
    return dict(complete=complete, regular_convergence=regular, auto_agreement=agreement,
                repeat_identity=replay, truth_improvement=learning,
                convergence_comparison_supported=comparison,
                broader_registration_check_supported=comparison and learning,
                verdict=('SUPPORTED' if comparison and learning else 'NOT SUPPORTED') if complete else 'INCOMPLETE / UNEVALUATED',
                rows=rows, repeats=repeats, outcomes=outcomes)


def report():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    outcomes = json.loads((OUT / 'outcomes.json').read_text()) if (OUT / 'outcomes.json').exists() else {}
    loaded, rows, repeats, invalid = {}, {}, {}, {}
    for cell in cells():
        if outcomes.get(cell['name'], {}).get('kind') == 'native_result':
            try:
                loaded[cell['name']] = load_cell(cell, OUT)
            except (AssertionError, ValueError, KeyError, OSError) as error:
                invalid[cell['name']] = type(error).__name__ + ': ' + str(error)
    body = []
    for cell in cells():
        name = cell['name']
        if name not in loaded:
            continue
        d, t, stages, p = loaded[name]
        refname = f"{cell['fixture']}_{cell['seed']}_baseline_auto"
        ref = loaded.get(refname)
        with np.load(HERE / 'fixtures' / f"{cell['fixture']}_{cell['seed']}.npz") as data:
            try:
                row = measure(d, p, data, ref[3] if ref else None)
            except (AssertionError, ValueError, KeyError) as error:
                invalid[name] = type(error).__name__ + ': ' + str(error)
                continue
            rows[name] = row
            if name.endswith('_repeat'):
                first = loaded.get(name.removesuffix('_repeat'))
                repeats[name] = bool(first and p is not None and first[3] is not None and
                    np.array_equal(p, first[3]) and [r[:8] for r in t] == [r[:8] for r in first[1]] and
                    all(d[k] == first[0][k] for k in ('status', 'termination', 'iterations')))
            image_path = OUT / (name + '.png')
            # Cache finished cell figures; the summary is refreshed after each native run.
            positions_image = OUT / (name + '-positions.png')
            if not image_path.exists() or p is not None and not positions_image.exists():
                fig, axes = plt.subplots(4, 2, figsize=(12, 12))
                if t:
                    a = np.asarray(t)
                    for ax, col, label in [(axes[0, 0], 2, 'NLL raw=weighted (weight1)'), (axes[0, 1], 3, 'Stopping objective'),
                        (axes[1, 0], 1, 'Sigma² after M-step (world²)'), (axes[1, 1], 6, 'Accepted sampled relative error'),
                        (axes[2, 0], 5, 'E-step policy: 1 Dense, 2 Truncated, 4 FastGauss, 5 Nyström'),
                        (axes[2, 1], 4, 'Matched mass before M-step'), (axes[3, 0], 7, 'Kernel evaluations')]:
                        ax.plot(a[:, 0], a[:, col]); ax.set(title=label, xlabel='Completed M-steps')
                        for j in np.flatnonzero(np.diff(a[:, 5])) + 1:
                            ax.axvline(a[j, 0], color='red', linestyle=':')
                    axes[0, 1].plot(a[:, 0], a[:, 3] - a[:, 2], label='Objective minus NLL'); axes[0, 1].legend()
                labels = list(dict.fromkeys(s for s, _ in stages))
                axes[3, 1].step([v for _, v in stages], [labels.index(s) for s, _ in stages], where='post')
                axes[3, 1].set_yticks(range(len(labels)), labels); axes[3, 1].set(xlabel='Elapsed seconds', title='Stage boundaries')
                fig.tight_layout(); fig.savefig(OUT / 'render.tmp.png', dpi=100); plt.close(fig)
                (OUT / 'render.tmp.png').replace(image_path)
                if p is not None:
                    fig = plt.figure(figsize=(12, 5)); ids = np.r_[0:100, N - 100:N]
                    src, truth = data['source'][ids], data['truth'][ids]
                    target = data['target'][np.r_[0:100, len(data['target']) - 100:len(data['target'])]]
                    both = np.vstack([src, truth, target, p[ids]]); low, high = both.min(0), both.max(0)
                    for j, (cloud, label) in enumerate([(src, 'Before'), (p[ids], 'After')], 1):
                        ax = fig.add_subplot(1, 2, j, projection='3d')
                        for x, lab in [(target, 'target'), (truth, 'truth'), (cloud, 'source')]: ax.scatter(*x.T, s=3, label=lab)
                        ax.set(title=label, xlabel='x world', ylabel='y world', zlabel='z world', xlim=(low[0], high[0]), ylim=(low[1], high[1]), zlim=(low[2], high[2])); ax.legend()
                    fig.tight_layout(); fig.savefig(OUT / 'render.tmp.png', dpi=100); plt.close(fig)
                    (OUT / 'render.tmp.png').replace(positions_image)
            body += [f'<h2>{name}</h2><pre>{html.escape(json.dumps(row, indent=2))}</pre>', f'<img src="{image_path.name}" alt="Iteration metrics and events">']
            if p is not None: body += [f'<img src="{name}-positions.png" alt="Before and after, first and last 100 points">']
    summary = summarize(rows, repeats, outcomes)
    summary['invalid_cells'] = invalid
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    intro = '''<!doctype html><meta charset="utf-8"><title>METHOD-082 convergence</title>
<style>body{font:15px system-ui;max-width:1200px;margin:30px auto}img{max-width:100%}pre{white-space:pre-wrap}</style>
<h1>Nyström exact-row repair: full solver termination</h1>
<p>CPU Bayesian CPD, 100,000 source points; full/partial overlap; seeds 1–3. Baseline Auto, baseline and repaired fixed FPS / resampled box. Cap 5,000; tolerance 1e-5. Success alone is not convergence. Sigma floor, cap, numerical failure and unevaluated cells remain distinct.</p>
<p>Frozen convergence comparison: all arms regularly converge, RMS to Auto ≤ .001 world units, maximum ≤ .01, four replay cells identical. Broader registration check additionally needs ≥10% RMS improvement over unregistered source and centered spread 0.5–2 times truth for every arm. These descriptive checks do not gate reporting. Auto agreement does not prove correct correspondence or a global optimum.</p>
<p>Trace: NLL/objective/matched mass precede M-step; sigma² follows it. Final convergence-only E-step is not observed. NLL raw=weighted, weight1, share100% of logged NLL; no full variational ELBO decomposition is available. Nyström totals/fractions at unequal lengths are descriptive. Kernel counters changed accounting across versions. Timings include traced registration and are not a performance claim.</p>
<p>Ground-truth input dumps have six significant digits, checked within 1e-5 RMS of native truth. Partial-subset boundary ambiguity within 1e-5 is counted separately. Figures use first/last100 only; metrics use all points. Three seeds are cases, not population confidence; repeat cells test replay only.</p>'''
    (OUT / 'report.html').write_text(intro + '<pre>' + html.escape(json.dumps({k: v for k, v in summary.items() if k not in ('rows',)}, indent=2)) + '</pre>' + ''.join(body))
    return summary


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (3 * 1024**3,) * 2)


def run():
    expected = hashlib.sha256((HERE / 'preflight/manifest.txt').read_bytes()).hexdigest()
    approval = json.loads((HERE / 'reviews/approval.json').read_text())
    assert approval == dict(digest=expected, codex_a='PASS', codex_b='PASS')
    OUT.mkdir(exist_ok=False)
    start, outcomes = time.monotonic(), {}
    def expired(signum, frame):
        raise TimeoutError('total driver budget exhausted')
    signal.signal(signal.SIGALRM, expired)
    signal.alarm(TOTAL_SECONDS)
    try:
        for cell in cells():
            assert hashlib.sha256(subprocess.check_output(manifest_command(), cwd=ROOT)).hexdigest() == expected, 'frozen inputs changed'
            remaining = TOTAL_SECONDS - (time.monotonic() - start)
            if remaining <= 0:
                outcomes[cell['name']] = dict(kind='unevaluated', reason='total budget exhausted')
                continue
            name, cmd = cell['name'], command(cell, OUT)
            (OUT / (name + '.command.json')).write_text(json.dumps(cmd, indent=2) + '\n')
            print('START', name, flush=True)
            with (OUT / (name + '.stdout')).open('w') as so, (OUT / (name + '.stderr')).open('w') as se:
                try:
                    result = subprocess.run(cmd, cwd=ROOT, stdout=so, stderr=se, timeout=min(CELL_SECONDS, remaining), preexec_fn=limits)
                except subprocess.TimeoutExpired:
                    outcomes[name] = dict(kind='unevaluated', reason='native timeout')
                else:
                    assert result.returncode in (0, 1), f'native technical failure: exit {result.returncode}'
                    d, _, _, _ = load_cell(cell, OUT)
                    assert (result.returncode == 0) == (d['status'] == 'success')
                    outcomes[name] = dict(kind='native_result', status=d['status'], termination=d['termination'])
            outcomes[name]['max_completed_child_rss_kib_so_far'] = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
            (OUT / 'outcomes.json').write_text(json.dumps(outcomes, indent=2) + '\n')
            result = report()
            assert not result['invalid_cells'], 'invalid measurement report'
            print('DONE', name, flush=True)
    except Exception as error:
        signal.alarm(0)
        for c in cells():
            outcomes.setdefault(c['name'], dict(kind='unevaluated', reason='technical stop: ' + str(error)))
        (OUT / 'outcomes.json').write_text(json.dumps(outcomes, indent=2) + '\n')
        report()
        raise
    signal.alarm(0)
    (OUT / 'outcomes.json').write_text(json.dumps(outcomes, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report().items() if k not in ('rows', 'outcomes')}), flush=True)


if __name__ == '__main__':
    if not __debug__: raise RuntimeError('Assertions are required')
    {'run': run, 'report': report}[sys.argv[1]]()
