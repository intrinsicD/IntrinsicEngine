#!/usr/bin/env python3
"""Version1 appendix from retained outputs; never runs registration or rewrites frozen analysis."""
import hashlib
import html
import json
import sys
from collections import Counter
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'METHOD-081'))
from compare import traces

# The frozen report requires native JSON, which interrupted runs cannot truthfully supply.
OUT = HERE / 'postrun'
RUNS = HERE / 'runs'
manifest = HERE / 'reviews/raw-sha256.txt'
assert hashlib.sha256(manifest.read_bytes()).hexdigest() == '7d068d3dec21450834cd82ccc906be6a97f0a6dfce0f2232dc0e8fa19b25731d'
for line in manifest.read_text().splitlines():
    digest, path = line.split('  ', 1)
    assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == digest, path
summary = json.loads((RUNS / 'summary.json').read_text())
assert summary['verdict'] == 'INCOMPLETE / UNEVALUATED'
outcomes, rows = summary['outcomes'], summary['rows']
assert len(outcomes) == 34 and len(rows) == 32 and all(summary['repeats'].values())
OUT.mkdir(exist_ok=True)

body = ['''<!doctype html><html lang="en"><meta charset="utf-8">
<title>METHOD-082: final interpretation and interrupted traces</title>
<style>body{font:16px system-ui;max-width:1200px;margin:32px auto;padding:0 16px;line-height:1.5}img{max-width:100%}pre{white-space:pre-wrap;overflow-wrap:anywhere}td,th{padding:8px;text-align:left;border-bottom:1px solid #ccc}table{border-collapse:collapse}aside{border-left:4px solid #b45;padding:12px;background:#fff5f5}</style>
<h1>Nyström repair: complete-termination test</h1>
<p>Post-run interpretation and timeout appendix, version1, 2026-10-12.
Question 1 (verbatim): “Dann bitte einmal testen, vollständige Konvergenz usw”.
Operator-directed test beyond METHOD-081's short prefixes; no engine change.</p>
<p><a href="../runs/report.html">Unchanged frozen report: all32 completed cells, curves and before/after figures</a> ·
<a href="../runs/summary.json">Original summary</a> ·
<a href="../preflight/layer-a.md">Resolved configuration and command</a> ·
<a href="../preflight/layer-b.md">Frozen predictions and controls</a> ·
<a href="../../../../ara/logic/claims.md#c123-method-082s-all-cell-convergence-and-auto-endpoint-prediction-is-refuted">ARA C123/C124</a></p>
<p>CPU Bayesian CPD; 100000 source points; full/partial overlap; seeds1–3;
5000 M-step cap, relative objective tolerance1e-5, 900-second per-cell process limit.
FPS is points/fixed; Box is union-box/resampled. Auto is a policy selector, not an all-dense oracle.
Every arm retains the rank100 deformation approximation.</p>
<h2>Frozen verdicts and observed counterexamples</h2>
<p><strong>All34 planned attempts ended: 32 native success/converged, two timeouts.
P1 and P2: refuted on retained cases.</strong> The original aggregate label remains
<strong>INCOMPLETE / UNEVALUATED</strong> because two native endpoints are missing.
Missing evidence does not erase the direct counterexamples in completed cells.</p>
<p>P1 required regular convergence of all34 cells, RMS/max agreement with same-seed
Auto ≤.001/.01 world units and four identical replays. P2 additionally required ≥10%
truth-RMS improvement from unregistered source and centered-radius/truth in [.5,2]
for every arm. No thresholds or analysis of the original report were changed.</p>
<aside><strong>Execution-wrapper anomaly:</strong> final shell retrieval reported exit143,
unknown cause. The controller retained all34 ordered START/DONE pairs and a matching
final summary; independent raw audit verified every available endpoint and all233 original
file hashes. This supports completed attempts and their individual dispositions, not a
clean exit0. <a href="../reviews/execution-binding.json">Execution binding</a> ·
<a href="../reviews/final-raw-results-audit.txt">Independent raw audit</a></aside>
<h2>Full overlap</h2>
<p>All15 original cells converge regularly and pass endpoint, truth-improvement and
spread checks. Largest RMS/max disagreement with Auto: 6.83233e-6/7.56731e-5 world units.
FPS accepts 6/4/4 Nyström steps before repair versus 16/16/15 after repair across seeds1/2/3;
even the repaired totals are below1% of complete traces. Box accepts one step in each
full-overlap cell, with identical baseline/candidate endpoints. All four full/seed1
replays are identical in positions and non-time trace values; other replay regimes
were not tested. Counts at unequal trace lengths are descriptive, not speed evidence.</p>
<h2>Partial overlap</h2>
<p>All13 available native endpoints converge regularly yet fail P2's truth improvement,
including all three Auto references. Truth RMS is .788114–.795272, 58.03–59.79% worse
than unregistered source; radius ratios .77754–.78391 pass the spread screen.
Four completed endpoints fail both P1 distance bounds:</p>
<table><tr><th>Cell</th><th>RMS to Auto (world)</th><th>Maximum (world)</th></tr>''']
for name, row in rows.items():
    if row['auto_agreement'] is False:
        body.append(f'<tr><td>{html.escape(name)}</td><td>{row["rms_to_auto"]:.9f}</td><td>{row["max_to_auto"]:.9f}</td></tr>')
body.append('''</table><p>These are counterexamples to the all-cell preservation prediction.
Baseline Box already fails at seed3, and Auto already fails truth quality: the common
partial-overlap failure cannot be attributed solely to the repair. Its cause remains
undiagnosed. A decreasing observed objective and a native convergence flag do not
establish accurate correspondence. No new diagnosis, rollback or algorithm change
was made from this test; the earlier bounded prefix result retains its narrow scope.</p>
<h2>Interrupted-trace appendix</h2>
<p>The two partial/seed2 Box runs hit the external900-second process limit. No native
termination result was returned. Endpoints and before/after positions unavailable;
Native convergence status unavailable. Final truth/Auto distances are unavailable.
These are censored outcomes, neither demonstrated nonconvergence nor native cap/floor stops.</p>
<p>Curves below use every retained iteration record. NLL, objective and matched mass
precede the M-step; sigma² follows it. Logged NLL is raw=weighted, weight1 and share100%
of logged NLL; objective equals NLL, not the full variational ELBO. Red dotted lines mark
policy changes. The black dashed line marks the last retained iteration/event before
the timeout, not the exact kill time; native elapsed time and the external process
deadline have different origins. The final regular-convergence-only E-step is unlogged
in completed runs. Repaired-row counts, accepted landmark counts and a full ELBO
decomposition are not exposed. Kernel counters have different accounting across versions.</p>''')
for name, outcome in outcomes.items():
    if outcome['kind'] == 'native_result':
        continue
    assert outcome['kind'] == 'unevaluated' and outcome['reason'] == 'native timeout', outcome
    assert not (RUNS / (name + '.json')).exists() and not (RUNS / (name + '.bin')).exists()
    records, stages = traces(RUNS / (name + '.stderr'))
    a = np.asarray(records)
    assert len(a) and [int(v) for v in a[:, 0]] == list(range(len(a)))
    assert np.isfinite(a).all() and stages and all(np.isfinite(v) for _, v in stages)
    counts = Counter({int(k): v for k, v in Counter(a[:, 5]).items()})
    figure, axes = plt.subplots(4, 2, figsize=(14, 13))
    changes = np.flatnonzero(np.diff(a[:, 5])) + 1
    for ax, col, label in [(axes[0, 0], 2, 'NLL raw=weighted, weight1'), (axes[0, 1], 3, 'Stopping objective'),
        (axes[1, 0], 1, 'Sigma² after M-step (world²)'), (axes[1, 1], 6, 'Accepted sampled relative error'),
        (axes[2, 0], 5, 'Policy: 1 Dense, 2 Truncated, 5 Nyström'), (axes[2, 1], 4, 'Matched mass before M-step'),
        (axes[3, 0], 7, 'Kernel evaluations')]:
        ax.plot(a[:, 0] + 1, a[:, col])
        ax.set(title=label, xlabel='Completed M-steps')
        for j in changes:
            ax.axvline(a[j, 0] + 1, color='red', linestyle=':', alpha=.6)
        ax.axvline(a[-1, 0] + 1, color='black', linestyle='--', label='Last retained iteration')
    axes[0, 1].plot(a[:, 0] + 1, a[:, 3] - a[:, 2], label='Objective minus NLL')
    axes[0, 1].legend(fontsize=8)
    labels = list(dict.fromkeys(s for s, _ in stages))
    axes[3, 1].step([v for _, v in stages], [labels.index(s) for s, _ in stages], where='post')
    axes[3, 1].axvline(stages[-1][1], color='black', linestyle='--', label='Last retained event')
    axes[3, 1].set_yticks(range(len(labels)), labels)
    axes[3, 1].set(title='Stage boundaries; censored at external timeout', xlabel='Native elapsed seconds')
    axes[3, 1].legend(fontsize=8)
    figure.suptitle(f'{name}: {len(a)} M-steps, endpoint unavailable')
    figure.tight_layout()
    figure.savefig(OUT / (name + '.png'), dpi=110)
    plt.close(figure)
    command = json.loads((RUNS / (name + '.command.json')).read_text())
    body.append(f'<section id="{name}"><h3>{name}</h3><p>{len(a)} completed M-steps; '
        f'accepted {counts[5]} Nyström, {counts[1]} Dense, {counts[2]} Truncated. '
        f'Last recorded stage: {html.escape(stages[-1][0])}, native elapsed {stages[-1][1]:.6f}s.</p>'
        f'<p><a href="../runs/{name}.stderr">Original trace</a> · <a href="../runs/{name}.command.json">Original command</a></p>'
        f'<details><summary>Exact recorded argv</summary><pre>{html.escape(json.dumps(command, indent=2))}</pre></details>'
        f'<img src="{name}.png" alt="All retained iteration metrics, policy changes and stage boundaries before timeout"></section>')
body.append('''<h2>Limits and evidence</h2><p>This is a local synthetic CPU Bayesian CPD diagnostic,
not classical CPD, real-data, GPU, global-optimum, universal positivity or performance evidence.
Three seeds are individual cases, not a population estimate. Input text dumps are rounded;
independently recomputed truth RMS agrees with native results within7.88e-9. All distance
metrics use complete endpoint buffers; original before/after figures show first/last100
points only. Unknown endpoints stay unknown. The code and thresholds need no change to
retain these negative results.</p><p>Frozen input commit: 8c3ffaaa7b396a5c875ca32b77c34d3b1abbc60e.<br>
<a href="../preflight/manifest.txt">Packet digest</a>: 209972fb7ccbbaa3fd83549805d04b54f2ce5ee6888b417829f56293bb461c9e.<br>
<a href="../reviews/raw-sha256.txt">Original233-file manifest</a>: 7d068d3dec21450834cd82ccc906be6a97f0a6dfce0f2232dc0e8fa19b25731d.</p></html>''')
(OUT / 'report.html').write_text('\n'.join(body) + '\n')
print('Wrote separate version1 report and two interrupted-trace figures; original analysis unchanged')
