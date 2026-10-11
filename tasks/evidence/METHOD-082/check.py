#!/usr/bin/env python3
"""Known-answer controls through the complete METHOD-082 read/report/verdict path."""
import hashlib, json, shutil, subprocess, sys, tempfile
from pathlib import Path
from unittest.mock import patch
import numpy as np
import convergence as c


def controls():
    original_here, original_out, original_n, original_cap = c.HERE, c.OUT, c.N, c.CAP
    with tempfile.TemporaryDirectory() as temp:
        c.HERE = Path(temp); c.OUT = c.HERE / 'runs'; c.OUT.mkdir(); (c.HERE / 'fixtures').mkdir()
        c.N, c.CAP = 200, 4
        x = np.linspace(-1, 1, c.N)
        source = np.column_stack([x, np.sin(x * 3), np.cos(x * 2)])
        source[1, 0] = -.1  # One non-clutter point exactly on the rounded visibility boundary.
        truth = source + [.3, -.2, .1]
        for fixture in ('full', 'partial'):
            for seed in (1, 2, 3):
                np.savez(c.HERE / 'fixtures' / f'{fixture}_{seed}.npz', source=source, truth=truth, target=truth)

        def whole(mode):
            outcomes = {}
            for cell in c.cells():
                name = cell['name']; is_candidate = cell['arm'] == 'candidate'
                term = 'iteration_cap' if mode == 'allcap' or mode == 'candidatecap' and is_candidate else 'sigma_floor' if mode == 'floor' and is_candidate else 'converged'
                count = c.CAP if term == 'iteration_cap' else 3
                p = truth.copy()
                if mode == 'displaced' and is_candidate: p += [.02, 0, 0]
                if mode == 'identity': p = source.copy()
                if mode == 'wrongconsistent': p += [2, 0, 0]
                if mode == 'collapse': p[:] = truth.mean(0)
                status = 'numerical_failure' if mode == 'numerical' and is_candidate else 'success'
                if status != 'success': term = 'none'
                d = dict(fixture=cell['fixture'], seed=cell['seed'], points=c.N, targets=c.N, threads=8,
                    variant=c.VARIANTS[cell['variant']], landmarks=256, error_limit=.001,
                    status=status, termination=term, iterations=count if status == 'success' else 0, observed_iterations=count,
                    nystrom_iterations=0 if cell['variant'] == 'auto' else count,
                    seconds=1., sigma2=.01, scale=1., max_sampled_error=.0001,
                    rms_to_truth=c.distances(p, truth)[0] if status == 'success' else None)
                (c.OUT / (name + '.json')).write_text(json.dumps(d))
                pos = c.OUT / (name + '.bin')
                if status == 'success': p.astype('<f8').tofile(pos)
                else: pos.unlink(missing_ok=True)
                policy = 1 if cell['variant'] == 'auto' else 5
                trace = ''.join(f'CPD_ITER {i} .01 {1+i} {1+i} 30 {policy} .0001 100 {i+1}\n' for i in range(count))
                if mode == 'different_replay' and name.endswith('_repeat'): trace = trace.replace(' 100 ', ' 101 ')
                (c.OUT / (name + '.stderr')).write_text('CPD_STAGE expectation_step 0\n' + trace)
                (c.OUT / (name + '.png')).write_bytes(b'test cache; plots not exercised here')
                (c.OUT / (name + '-positions.png')).write_bytes(b'test cache')
                outcomes[name] = dict(kind='native_result')
            if mode == 'missing_trace': (c.OUT / (c.cells()[0]['name'] + '.stderr')).unlink()
            if mode == 'nonfinite':
                p = truth.copy(); p[0, 0] = np.nan; p.tofile(c.OUT / (c.cells()[3]['name'] + '.bin'))
            if mode == 'timeout': outcomes[c.cells()[3]['name']] = dict(kind='unevaluated', reason='native timeout')
            (c.OUT / 'outcomes.json').write_text(json.dumps(outcomes))
            result = c.report()
            assert (c.OUT / 'report.html').exists()
            return result

        positive = whole('positive')
        assert positive['broader_registration_check_supported']
        subsets = positive['rows']['partial_1_baseline_auto']['truth_subsets']
        assert subsets['boundary_ambiguous_nonclutter']['count'] == 1 and sum(d['count'] for d in subsets.values()) == c.N
        for mode in ('allcap', 'candidatecap', 'floor', 'numerical', 'displaced', 'different_replay'):
            r = whole(mode)
            assert r['complete'] and not r['convergence_comparison_supported'], mode
        for mode in ('identity', 'wrongconsistent', 'collapse'):
            r = whole(mode)
            assert r['convergence_comparison_supported'] and not r['broader_registration_check_supported'], mode
        for mode in ('missing_trace', 'nonfinite', 'timeout'):
            r = whole(mode)
            assert not r['complete'] and r['verdict'] == 'INCOMPLETE / UNEVALUATED', mode
        whole('positive')
        name = c.cells()[3]['name']
        (c.OUT / (name + '-positions.png')).unlink()
        c.report()
        for suffix in ('.png', '-positions.png'):
            assert (c.OUT / (name + suffix)).read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        print('PASS: real figure rendering recovers from existing metric PNG and missing before/after PNG')

        cached = c.OUT; c.OUT = c.HERE / 'driver-run'
        (c.HERE / 'preflight').mkdir(); (c.HERE / 'reviews').mkdir()
        manifest = b'known frozen input test'
        (c.HERE / 'preflight/manifest.txt').write_bytes(manifest)
        (c.HERE / 'reviews/approval.json').write_text(json.dumps(dict(digest=hashlib.sha256(manifest).hexdigest(), codex_a='PASS', codex_b='PASS')))
        starts = []
        def native(command, **kwargs):
            name = Path(command[command.index('--out') + 1]).stem; starts.append(name)
            for suffix in ('.json', '.bin', '.stderr', '.png', '-positions.png'):
                shutil.copyfile(cached / (name + suffix), c.OUT / (name + suffix))
            file = c.OUT / (name + '.json'); d = json.loads(file.read_text()); d['rms_to_truth'] = 1.
            file.write_text(json.dumps(d))
            return subprocess.CompletedProcess(command, 0)
        with patch.object(c.subprocess, 'check_output', return_value=manifest), patch.object(c.subprocess, 'run', side_effect=native):
            try: c.run()
            except AssertionError as error: assert str(error) == 'invalid measurement report'
            else: raise AssertionError('truth mismatch did not stop the driver')
        assert len(starts) == 1 and (c.OUT / 'report.html').exists()
        assert json.loads((c.OUT / 'summary.json').read_text())['invalid_cells']
        print('PASS: native/dump truth mismatch preserves invalid report and prevents the next native start')
        print('PASS: complete-report positive, all/candidate cap, floor, numerical failure, displacement, replay, identity, wrong-consistent, collapse, missing trace, nonfinite, timeout controls')
    c.HERE, c.OUT, c.N, c.CAP = original_here, original_out, original_n, original_cap


MUTATIONS = {
    'convergence': ("d['status'] == 'success' and d['termination'] == 'converged'", "d['status'] == 'success'"),
    'agreement': ("agreement = all_success and all(d['auto_agreement'] is True for d in rows.values())", 'agreement = all_success'),
    'learning': ("learning = all_success and all(d['truth_improvement'] for d in rows.values())", 'learning = all_success'),
    'replay': ('comparison = regular and agreement and replay', 'comparison = regular and agreement'),
    'invalid_report': ("assert not result['invalid_cells'], 'invalid measurement report'", "assert True, 'invalid measurement report'"),
    'failure_history': ("assert len(t) == d['observed_iterations']", "assert len(t) == d['iterations'] == d['observed_iterations']"),
}

if __name__ == '__main__':
    if len(sys.argv) == 2:
        before, after = MUTATIONS[sys.argv[1]]
        source = Path(c.__file__).read_text(); assert source.count(before) == 1
        exec(compile(source.replace(before, after), c.__file__, 'exec'), c.__dict__)
        controls()
    else:
        controls()
        for mutation in MUTATIONS:
            result = subprocess.run([sys.executable, __file__, mutation], capture_output=True, text=True)
            assert result.returncode != 0 and 'AssertionError' in result.stderr, (mutation, result.stdout, result.stderr)
            print('REJECTED mutation:', mutation, result.stderr.splitlines()[-1])
