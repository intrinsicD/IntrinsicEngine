#!/usr/bin/env python3
"""UI-081: HTML report and frozen decision for IntrinsicGlyphOrderComparison output.

    python3 benchmarks/runners/glyph_order_report.py <run dir> [--budget-ms 2000] [--tie 0.02]

Reads <run dir>/cells.jsonl (raw cells) and writes <run dir>/report.html and decision.json.
Decision rule (frozen in the UI-081 pre-flight packet): candidates are the orders (not stride,
not random). Score = median over fixture x domain x ratio of the seed-mean coverage / d_hex.
A candidate is eligible when its first 65536 (the cap) of every domain of the 1000 x 1000
timing grid take at most --budget-ms. The lowest eligible score wins; candidates within --tie
(relative) of it are tied and the tie goes to the highest median packing. The cap is dropped
(mode "full") only when the winner's full order of every domain takes at most
--full-margin x --budget-ms. Gates: random's score must exceed the winner's by more than 10%
and the winner must beat stride on the graded fixture's coverage at every ratio.
"""
import argparse
import html
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

NON_CANDIDATES = {"stride", "random"}
METRICS = [("coverage", "coverage / d_hex (lower is better)"), ("packing", "packing / d_hex (higher is better)"),
           ("cell_cv", "Voronoi cell area CV (lower is more even)")]
PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf",
           "#ff7f0e", "#000000"]


def load(run):
    cells = [json.loads(line) for line in (run / "cells.jsonl").read_text().splitlines() if line.strip()]
    summary_path = run / "summary.json"
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {"status": "incomplete"}
    return cells, summary


def seed_means(cells):
    """(fixture, domain, method, ratio) -> mean over seeds of each metric, plus seed spread of coverage."""
    groups = defaultdict(list)
    for c in cells:
        if c["kind"] == "quality":
            groups[(c["fixture"], c["domain"], c["method"], c["ratio"])].append(c)
    out = {}
    for key, rows in groups.items():
        out[key] = {m: statistics.fmean(r[m] for r in rows) for m, _ in METRICS}
        out[key]["coverage_sd"] = statistics.pstdev([r["coverage"] for r in rows]) if len(rows) > 1 else 0.0
        out[key]["seeds"] = len(rows)
        out[key]["count"] = min(r["count"] for r in rows)
    return out


def decide(cells, means, budget_ms, tie, full_margin):
    methods = sorted({k[2] for k in means})
    score = {m: statistics.median(v["coverage"] for k, v in means.items() if k[2] == m) for m in methods}
    pack = {m: statistics.median(v["packing"] for k, v in means.items() if k[2] == m) for m in methods}
    timing = defaultdict(dict)
    for c in cells:
        if c["kind"] == "timing":
            timing[c["method"]][(c["domain"], "full" if c["count"] == c["elements"] else "cap")] = c["ms"]
    domains = {"vertices", "edges", "faces"}

    def eligible(m, mode, limit):
        t = timing.get(m, {})
        return all((d, mode) in t and 0.0 <= t[(d, mode)] <= limit for d in domains)

    candidates = [m for m in methods if m not in NON_CANDIDATES]
    pool = [m for m in candidates if eligible(m, "cap", budget_ms)]
    decision = {"budget_ms": budget_ms, "tie": tie, "full_margin": full_margin, "scores": score, "packing": pack,
                "eligible": pool, "timing_ms": {m: {f"{d}/{k}": v for (d, k), v in t.items()} for m, t in timing.items()}}
    if not pool:
        decision.update({"winner": None, "mode": None, "verdict": "no candidate within the time budget"})
        return decision
    best = min(score[m] for m in pool)
    tied = [m for m in pool if score[m] <= best * (1.0 + tie)]
    winner = max(tied, key=lambda m: (pack[m], -score[m]))
    decision["mode"] = "full" if eligible(winner, "full", full_margin * budget_ms) else "cap"
    gates = {}
    if "random" in score:
        gates["random_worse_by_10pct"] = score["random"] > score[winner] * 1.10
    graded = [(k[3], v["coverage"]) for k, v in means.items() if k[0] == "graded_grid_256" and k[2] == winner]
    stride = {k[3]: v["coverage"] for k, v in means.items() if k[0] == "graded_grid_256" and k[2] == "stride"}
    gates["beats_stride_on_graded"] = bool(graded) and all(cov < stride.get(r, math.inf) for r, cov in graded)
    decision.update({"winner": winner, "tied": tied, "gates": gates,
                     "verdict": "decided" if all(gates.values()) else "gate failed: measurement or premise not confirmed"})
    return decision


def svg_lines(series, title, ylabel, width=420, height=260):
    """series: {name: [(x, y), ...]} with x a reduction ratio (log2 axis)."""
    pts = [p for s in series.values() for p in s if p[1] is not None and math.isfinite(p[1])]
    if not pts:
        return ""
    xs = sorted({p[0] for p in pts})
    lo, hi = min(p[1] for p in pts), max(p[1] for p in pts)
    if hi == lo:
        hi = lo + 1.0
    L, R, T, B = 48, 150, 24, 36
    def X(x):
        return L + (math.log2(x) - math.log2(xs[0])) / max(1e-9, math.log2(xs[-1]) - math.log2(xs[0])) * (width - L - R)
    def Y(y):
        return T + (1.0 - (y - lo) / (hi - lo)) * (height - T - B)
    out = [f'<svg viewBox="0 0 {width} {height}" class="chart"><text x="{L}" y="14" class="t">{html.escape(title)}</text>']
    for v in (lo, (lo + hi) / 2, hi):
        out.append(f'<text x="4" y="{Y(v) + 4:.1f}" class="a">{v:.3g}</text><line x1="{L}" x2="{width - R}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" class="g"/>')
    for x in xs:
        out.append(f'<text x="{X(x) - 8:.1f}" y="{height - 18}" class="a">N/{x}</text>')
    out.append(f'<text x="{L}" y="{height - 4}" class="a">{html.escape(ylabel)}</text>')
    for i, (name, s) in enumerate(sorted(series.items())):
        color = PALETTE[i % len(PALETTE)]
        s = sorted(p for p in s if p[1] is not None)
        path = " ".join(f"{'M' if j == 0 else 'L'}{X(x):.1f},{Y(y):.1f}" for j, (x, y) in enumerate(s))
        dash = ' stroke-dasharray="4 3"' if name in NON_CANDIDATES else ""
        out.append(f'<path d="{path}" fill="none" stroke="{color}" stroke-width="1.6"{dash}/>')
        out.append(f'<text x="{width - R + 6}" y="{T + 12 * i + 8}" class="a" fill="{color}">{html.escape(name)}</text>')
    out.append("</svg>")
    return "".join(out)


def svg_points(name, points, size=220):
    dots = "".join(f'<circle cx="{8 + x * (size - 16):.1f}" cy="{size - 8 - y * (size - 16):.1f}" r="1.6"/>' for x, y in points)
    return (f'<figure><svg viewBox="0 0 {size} {size}" class="pts"><rect x="0" y="0" width="{size}" height="{size}" class="frame"/>'
            f'{dots}</svg><figcaption>{html.escape(name)} ({len(points)})</figcaption></figure>')


def report(run, budget_ms, tie, full_margin):
    cells, summary = load(run)
    means = seed_means(cells)
    decision = decide(cells, means, budget_ms, tie, full_margin)
    (run / "decision.json").write_text(json.dumps(decision, indent=2))
    parts = ["<!doctype html><meta charset='utf-8'><title>Glyph order comparison</title><style>",
             "body{font:14px system-ui,sans-serif;margin:24px;max-width:1400px;color:#111;background:#fff}",
             ".chart{width:420px;height:260px;margin:4px}.t{font-size:12px;font-weight:600}.a{font-size:10px}",
             ".g{stroke:#ddd}.pts{width:220px;height:220px}.frame{fill:none;stroke:#999}circle{fill:#1f77b4}",
             "figure{display:inline-block;margin:4px}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:3px 6px;text-align:right}",
             ".ok{color:#2a7a2a}.bad{color:#b00020}</style>",
             "<h1>UI-081 glyph order comparison</h1>",
             f"<p>Run status: <b>{html.escape(str(summary.get('status')))}</b>, commit {html.escape(str(summary.get('commit')))}, "
             f"{len(cells)} raw cells. Self-test and controls: see the pre-flight packet.</p>",
             "<h2>Frozen decision</h2><table><tr><th>method</th><th>median coverage/d</th><th>median packing/d</th>"
             "<th>eligible</th><th>timing (ms) vertices/edges/faces full | cap</th></tr>"]
    for m in sorted(decision["scores"], key=lambda m: decision["scores"][m]):
        t = decision["timing_ms"].get(m, {})
        tt = " / ".join(f"{t.get(f'{d}/full', float('nan')):.0f}" for d in ("vertices", "edges", "faces")) + " | " + \
             " / ".join(f"{t.get(f'{d}/cap', float('nan')):.0f}" for d in ("vertices", "edges", "faces"))
        mark = "winner" if m == decision.get("winner") else ("yes" if m in decision["eligible"] else "")
        parts.append(f"<tr><td style='text-align:left'>{html.escape(m)}</td><td>{decision['scores'][m]:.4f}</td>"
                     f"<td>{decision['packing'][m]:.4f}</td><td>{mark}</td><td>{tt}</td></tr>")
    parts.append("</table>")
    gates = decision.get("gates", {})
    parts.append(f"<p>Mode: <b>{decision['mode']}</b> (budget {budget_ms:.0f} ms per domain for the capped order, "
                 f"full order dropped the cap at {decision['full_margin']:.0%} of it; tie {tie:.0%}). Winner: "
                 f"<b>{html.escape(str(decision.get('winner')))}</b>; tied: {html.escape(str(decision.get('tied')))}; verdict: "
                 f"<b>{html.escape(decision['verdict'])}</b>.</p><ul>")
    for g, ok in gates.items():
        parts.append(f"<li class='{'ok' if ok else 'bad'}'>{html.escape(g)}: {ok}</li>")
    parts.append("</ul>")
    parts.append("<h2>Before / after: graded plane (density x16 left to right), vertices, N/64</h2>")
    for c in cells:
        if c["kind"] == "samples":
            parts.append(svg_points(c["method"], c["points"]))
    fixtures = sorted({(k[0], k[1]) for k in means})
    parts.append("<h2>Metrics over the prefix size (N / ratio), seed means</h2>")
    for fixture, domain in fixtures:
        parts.append(f"<h3>{html.escape(fixture)} / {html.escape(domain)}</h3><div>")
        for metric, label in METRICS:
            series = defaultdict(list)
            for k, v in means.items():
                if k[0] == fixture and k[1] == domain:
                    series[k[2]].append((k[3], v[metric]))
            parts.append(svg_lines(series, f"{fixture} {domain}: {metric}", label))
        parts.append("</div>")
    spread = [(k, v["coverage_sd"] / v["coverage"]) for k, v in means.items() if v["seeds"] > 1 and v["coverage"] > 0]
    if spread:
        worst = max(spread, key=lambda kv: kv[1])
        parts.append(f"<p>Largest seed spread of coverage (sd/mean): {worst[1]:.3f} at {html.escape(str(worst[0]))}.</p>")
    failures = [c for c in cells if c["kind"] == "failure"]
    parts.append(f"<p>Failed orders: {len(failures)} {html.escape(str(failures[:10]))}</p>")
    (run / "report.html").write_text("".join(parts))
    return decision


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path)
    parser.add_argument("--budget-ms", type=float, default=2000.0)
    parser.add_argument("--tie", type=float, default=0.02)
    parser.add_argument("--full-margin", type=float, default=0.9)
    args = parser.parse_args()
    decision = report(args.run, args.budget_ms, args.tie, args.full_margin)
    print(json.dumps({k: decision.get(k) for k in ("mode", "winner", "tied", "gates", "verdict")}, indent=2))


if __name__ == "__main__":
    main()
