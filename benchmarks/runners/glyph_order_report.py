#!/usr/bin/env python3
"""UI-081: HTML report and frozen decision for IntrinsicGlyphOrderComparison output.

    python3 benchmarks/runners/glyph_order_report.py <run dir> [--budget-ms 2000] [--tie 0.02] [--full-margin 0.9]

Reads <run dir>/cells.jsonl (raw cells) and writes <run dir>/report.html and decision.json.
Decision rule (frozen in the UI-081 pre-flight packet), applied only to a complete run:
- The planned inventory must be present: every fixture x domain x method x seed x ratio quality
  cell, the stride and random controls, and every timing cell. Anything missing, a failed order,
  a failed measurement or an incomplete summary stops the decision ("incomplete").
- Candidates are the orders (not stride, not random). A candidate with any "short" quality cell
  (fewer samples than the prefix asks for) cannot serve the slider and is ineligible; so is one
  whose order missed the runner's deadline anywhere ("timeout": measured too slow).
- Score = median over fixture x domain x ratio of the seed-mean coverage / d_hex.
- Eligible: every repeat of the capped (65536) order of each timing-grid domain succeeded within
  the runner's deadline and their median is at most --budget-ms.
- The lowest eligible score wins; candidates within --tie (relative) of it are tied and the tie
  goes to the highest median packing.
- The cap is dropped (mode "full") only when the winner's capped order is a prefix of its full
  order and the median full order of every domain takes at most --full-margin x --budget-ms.
- Gates: random's score exceeds the winner's by more than 10%; the winner beats stride's
  coverage on the graded fixture for every (domain, ratio); the reference refinement changes no
  measured coverage by more than half the tie band. A failed gate means the measurement or the
  premise is not confirmed and the operator decides.
"""
import argparse
import html
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

FIXTURES = ["sculpt", "child", "regular_grid_256", "graded_grid_256"]
DOMAINS = ["vertices", "edges", "faces"]
RATIOS = [round(4.0 * 2.0 ** (0.5 * j), 4) for j in range(13)]  # sqrt(2) steps, as the runner
SEEDED = {"random", "flat_greedy_beta110", "lazy_greedy_beta110", "progressive_poisson_hapds",
          "progressive_poisson_fast", "progressive_poisson_balanced"}
METHODS = ["random", "farthest_point", "coupled_sieve_eta095", "flat_greedy_beta110", "lazy_greedy_beta110",
           "progressive_poisson_hapds", "progressive_poisson_fast", "progressive_poisson_balanced",
           "sample_elimination", "tournament"]
NON_CANDIDATES = {"stride", "random"}
METRICS = [("coverage", "coverage / d_hex (lower is better)"), ("packing", "packing / d_hex (higher is better)"),
           ("cell_cv", "Voronoi cell area CV (lower is more even)")]
PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf",
           "#ff7f0e", "#000000"]


def load(run):
    cells, broken = [], 0
    for line in (run / "cells.jsonl").read_text().splitlines():
        if not line.strip():
            continue
        try:
            cells.append(json.loads(line))
        except json.JSONDecodeError:
            broken += 1  # a line cut by the watchdog
    summary_path = run / "summary.json"
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {"status": "incomplete"}
    return cells, summary, broken


def inventory(cells, summary, broken):
    """Missing or failed planned cells; an empty list means the run is complete."""
    problems = []
    if summary.get("status") != "complete":
        problems.append("summary status is not complete")
    if broken:
        problems.append(f"{broken} unparsable line(s)")
    quality = {(c["fixture"], c["domain"], c["method"], c["seed"], round(c["ratio"], 4)): c
               for c in cells if c["kind"] == "quality"}
    elements = {(c["fixture"], c["domain"]): c["elements"] for c in cells if c["kind"] == "quality"}
    timeouts = {(c["fixture"], c["domain"], c["method"], c["seed"]) for c in cells if c["kind"] == "timeout"}
    for fixture in FIXTURES:
        for domain in DOMAINS:
            if (fixture, domain) not in elements:
                problems.append(f"no cells for {fixture}/{domain}")
                continue
            ratios = [r for r in RATIOS if int(elements[(fixture, domain)] / r) >= 8]
            for method in ["stride"] + METHODS:
                for seed in ([1, 2, 3, 4, 5] if method in SEEDED else [0]):
                    if (fixture, domain, method, seed) in timeouts:
                        if method in NON_CANDIDATES:
                            problems.append(f"control timed out {fixture}/{domain}/{method}/seed {seed}")
                        continue
                    for r in ratios:
                        cell = quality.get((fixture, domain, method, seed, r))
                        if cell is None:
                            problems.append(f"missing {fixture}/{domain}/{method}/seed {seed}/N/{r}")
                        elif cell["status"] == "measure_failed":
                            problems.append(f"measurement failed {fixture}/{domain}/{method}/seed {seed}/N/{r}")
    for c in cells:
        if c["kind"] == "failure":
            problems.append(f"order failed {c['fixture']}/{c['domain']}/{c['method']}/seed {c['seed']}")
    timing = {(c["method"], c["domain"], c["count"] == c["elements"]) for c in cells if c["kind"] == "timing"}
    for method in METHODS:
        for domain in DOMAINS:
            for full in (True, False):
                if (method, domain, full) not in timing:
                    problems.append(f"missing timing {method}/{domain}/{'full' if full else 'cap'}")
    stability = {c["method"] for c in cells if c["kind"] == "prefix_stability"}
    problems += [f"missing prefix stability {m}" for m in METHODS if m not in stability]
    return problems


def seed_means(cells):
    """(fixture, domain, method, ratio) -> seed means of each metric over ok cells, plus seed spread."""
    groups = defaultdict(list)
    for c in cells:
        if c["kind"] == "quality" and c["status"] == "ok":
            groups[(c["fixture"], c["domain"], c["method"], round(c["ratio"], 4))].append(c)
    out = {}
    for key, rows in groups.items():
        out[key] = {m: statistics.fmean(r[m] for r in rows) for m, _ in METRICS}
        out[key]["coverage_sd"] = statistics.pstdev([r["coverage"] for r in rows]) if len(rows) > 1 else 0.0
        out[key]["seeds"] = len(rows)
    return out


def decide(cells, summary, broken, means, budget_ms, tie, full_margin):
    problems = inventory(cells, summary, broken)
    methods = sorted({k[2] for k in means})
    score = {m: statistics.median(v["coverage"] for k, v in means.items() if k[2] == m) for m in methods}
    pack = {m: statistics.median(v["packing"] for k, v in means.items() if k[2] == m) for m in methods}
    short = sorted({c["method"] for c in cells if c["kind"] == "quality" and c["status"] == "short"} |
                   {c["method"] for c in cells if c["kind"] == "timeout"})
    stable = {c["method"]: c["stable"] and c.get("status", "ok") == "ok" for c in cells if c["kind"] == "prefix_stability"}
    timing = defaultdict(dict)
    for c in cells:
        if c["kind"] == "timing":
            ms = statistics.median(c["ms"]) if c["status"] == "ok" and c["ms"] else None
            timing[c["method"]][(c["domain"], "full" if c["count"] == c["elements"] else "cap")] = ms

    def within(m, mode, limit):
        t = timing.get(m, {})
        return all(t.get((d, mode)) is not None and t[(d, mode)] <= limit for d in DOMAINS)

    refinement = [abs(c["coverage_refined"] - c["coverage"]) / c["coverage"]
                  for c in cells if c["kind"] == "refinement" and c["coverage"] > 0 and c["coverage_refined"] > 0]
    decision = {"budget_ms": budget_ms, "tie": tie, "full_margin": full_margin, "scores": score, "packing": pack,
                "short_orders": short, "prefix_stable": stable, "problems": problems[:50],
                "problem_count": len(problems), "max_refinement_change": max(refinement) if refinement else None,
                "timing_ms": {m: {f"{d}/{k}": v for (d, k), v in t.items()} for m, t in timing.items()}}
    if problems:
        decision.update({"winner": None, "mode": None, "eligible": [],
                         "verdict": "incomplete: the planned cells are not all present (unevaluated, no decision)"})
        return decision
    pool = [m for m in methods if m not in NON_CANDIDATES and m not in short and within(m, "cap", budget_ms)]
    decision["eligible"] = pool
    if not pool:
        decision.update({"winner": None, "mode": None, "verdict": "no eligible candidate"})
        return decision
    best = min(score[m] for m in pool)
    tied = [m for m in pool if score[m] <= best * (1.0 + tie)]
    winner = max(tied, key=lambda m: (pack[m], -score[m]))
    full = stable.get(winner, False) and within(winner, "full", full_margin * budget_ms)
    gates = {"random_worse_by_10pct": score["random"] > score[winner] * 1.10}
    stride = {(k[1], k[3]): v["coverage"] for k, v in means.items() if k[0] == "graded_grid_256" and k[2] == "stride"}
    graded = {(k[1], k[3]): v["coverage"] for k, v in means.items() if k[0] == "graded_grid_256" and k[2] == winner}
    gates["beats_stride_on_graded"] = bool(graded) and set(graded) == set(stride) and \
        all(cov < stride[key] for key, cov in graded.items())
    gates["reference_resolves_tie_band"] = bool(refinement) and max(refinement) <= tie / 2
    decision.update({"winner": winner, "tied": tied, "mode": "full" if full else "cap", "gates": gates,
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
        out.append(f'<text x="4" y="{Y(v) + 4:.1f}" class="a">{v:.3g}</text>'
                   f'<line x1="{L}" x2="{width - R}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" class="g"/>')
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
    dots = "".join(f'<circle cx="{8 + x * (size - 16):.1f}" cy="{size - 8 - y * (size - 16):.1f}" r="1.6"/>'
                   for x, y in points)
    return (f'<figure><svg viewBox="0 0 {size} {size}" class="pts"><rect x="0" y="0" width="{size}" height="{size}" '
            f'class="frame"/>{dots}</svg><figcaption>{html.escape(name)} ({len(points)})</figcaption></figure>')


def report(run, budget_ms, tie, full_margin):
    cells, summary, broken = load(run)
    means = seed_means(cells)
    decision = decide(cells, summary, broken, means, budget_ms, tie, full_margin)
    (run / "decision.json").write_text(json.dumps(decision, indent=2))
    parts = ["<!doctype html><meta charset='utf-8'><title>Glyph order comparison</title><style>",
             "body{font:14px system-ui,sans-serif;margin:24px;max-width:1400px;color:#111;background:#fff}",
             ".chart{width:420px;height:260px;margin:4px}.t{font-size:12px;font-weight:600}.a{font-size:10px}",
             ".g{stroke:#ddd}.pts{width:220px;height:220px}.frame{fill:none;stroke:#999}circle{fill:#1f77b4}",
             "figure{display:inline-block;margin:4px}table{border-collapse:collapse}",
             "td,th{border:1px solid #ccc;padding:3px 6px;text-align:right}.ok{color:#2a7a2a}.bad{color:#b00020}</style>",
             "<h1>UI-081 glyph order comparison</h1>",
             f"<p>Run status: <b>{html.escape(str(summary.get('status')))}</b>, commit "
             f"{html.escape(str(summary.get('commit')))}, {len(cells)} raw cells, peak RSS "
             f"{html.escape(str(summary.get('peak_rss_kib')))} KiB. Planned-cell problems: {decision['problem_count']}.</p>"]
    if decision["problem_count"]:
        parts.append("<ul>" + "".join(f"<li class='bad'>{html.escape(p)}</li>" for p in decision["problems"]) + "</ul>")
    parts.append("<h2>Frozen decision</h2><table><tr><th>method</th><th>median coverage/d</th><th>median packing/d</th>"
                 "<th>eligible</th><th>prefix-stable</th><th>timing median ms vertices/edges/faces: full | cap</th></tr>")
    for m in sorted(decision["scores"], key=lambda m: decision["scores"][m]):
        t = decision["timing_ms"].get(m, {})

        def fmt(v):
            return "fail" if v is None else f"{v:.0f}"
        tt = " / ".join(fmt(t.get(f"{d}/full")) for d in DOMAINS) + " | " + " / ".join(fmt(t.get(f"{d}/cap")) for d in DOMAINS)
        mark = "winner" if m == decision.get("winner") else ("yes" if m in decision["eligible"] else
                                                              ("short order" if m in decision["short_orders"] else ""))
        parts.append(f"<tr><td style='text-align:left'>{html.escape(m)}</td><td>{decision['scores'][m]:.4f}</td>"
                     f"<td>{decision['packing'][m]:.4f}</td><td>{mark}</td>"
                     f"<td>{html.escape(str(decision['prefix_stable'].get(m, '')))}</td><td>{tt}</td></tr>")
    parts.append("</table>")
    parts.append(f"<p>Mode: <b>{decision.get('mode')}</b> (capped order within {budget_ms:.0f} ms per domain; the cap is "
                 f"dropped at {full_margin:.0%} of it for a prefix-stable winner; tie {tie:.0%}). Winner: "
                 f"<b>{html.escape(str(decision.get('winner')))}</b>; tied: {html.escape(str(decision.get('tied')))}; "
                 f"verdict: <b>{html.escape(decision['verdict'])}</b>. Largest coverage change with the refined reference: "
                 f"{decision['max_refinement_change']}.</p><ul>")
    for g, ok in decision.get("gates", {}).items():
        parts.append(f"<li class='{'ok' if ok else 'bad'}'>{html.escape(g)}: {ok}</li>")
    parts.append("</ul><h2>Before / after: graded plane (density x16 left to right), vertices, N/64</h2>")
    for c in cells:
        if c["kind"] == "samples":
            parts.append(svg_points(c["method"], c["points"]))
    parts.append("<h2>Metrics over the prefix size (N / ratio), seed means</h2>")
    for fixture, domain in sorted({(k[0], k[1]) for k in means}):
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
    (run / "report.html").write_text("".join(parts))
    return decision


def self_test():
    """Asserts decide() on synthetic complete and incomplete runs; returns 0 on success."""
    def cells_for(coverage, timing_ms, drop=None, stride_cov=None, timeout=None):
        out = []
        for f in FIXTURES:
            for d in DOMAINS:
                for m in ["stride"] + METHODS:
                    for seed in ([1, 2, 3, 4, 5] if m in SEEDED else [0]):
                        if timeout == (m, f, d):
                            out.append({"kind": "timeout", "fixture": f, "domain": d, "method": m, "seed": seed})
                            continue
                        for r in RATIOS:
                            if (m, f, d, r) == drop:
                                continue
                            cov = (stride_cov or {}).get((d, r), 3.0) if m == "stride" else coverage[m]
                            out.append({"kind": "quality", "fixture": f, "domain": d, "elements": 4096, "method": m,
                                        "seed": seed, "ratio": r, "k": int(4096 / r), "status": "ok", "coverage": cov,
                                        "packing": 0.9, "cell_cv": 0.2})
        for m in METHODS:
            out.append({"kind": "prefix_stability", "method": m, "stable": True, "status": "ok"})
            for d in DOMAINS:
                for full in (True, False):
                    out.append({"kind": "timing", "domain": d, "elements": 1000000, "method": m,
                                "count": 1000000 if full else 65536, "status": "ok", "ms": [timing_ms.get(m, 100.0)] * 3})
            if m in ("farthest_point", "sample_elimination"):
                out.append({"kind": "refinement", "coverage": 1.0, "coverage_refined": 1.005})
        return out
    coverage = {m: 1.0 for m in METHODS}
    coverage.update({"random": 2.0, "farthest_point": 0.70, "coupled_sieve_eta095": 0.705, "sample_elimination": 0.8})
    summary = {"status": "complete"}
    def run(cells, s=summary):
        return decide(cells, s, 0, seed_means(cells), 2000.0, 0.02, 0.9)
    failures = 0
    def check(ok, what):
        nonlocal failures
        print(("ok   " if ok else "FAIL ") + what)
        failures += 0 if ok else 1
    d = run(cells_for(coverage, {}))
    check(d["verdict"] == "decided" and d["winner"] in ("farthest_point", "coupled_sieve_eta095"), "complete run decides")
    check(set(d["tied"]) == {"farthest_point", "coupled_sieve_eta095"}, "2% tie band")
    d = run(cells_for(coverage, {"farthest_point": 2500.0, "coupled_sieve_eta095": 2500.0}))
    check(d["winner"] not in ("farthest_point", "coupled_sieve_eta095"), "over-budget candidates are ineligible")
    d = run(cells_for(coverage, {}, drop=("random", "child", "faces", RATIOS[3])))
    check(d["winner"] is None and d["verdict"].startswith("incomplete"), "a missing control cell blocks the decision")
    d = run(cells_for(coverage, {}), {"status": "incomplete"})
    check(d["winner"] is None, "an incomplete summary blocks the decision")
    d = run(cells_for(coverage, {}, timeout=("farthest_point", "child", "edges")))
    check(d["verdict"] == "decided" and d["winner"] == "coupled_sieve_eta095", "a timed-out candidate is ineligible")
    # Stride beats the winner only on graded edges: the per-domain gate must see it.
    stride = {("edges", r): 0.5 for r in RATIOS}
    d = run(cells_for(coverage, {}, stride_cov=stride))
    check(d["gates"]["beats_stride_on_graded"] is False, "stride gate compares the same domain")
    d = run(cells_for(coverage, {}, stride_cov={("vertices", r): 0.5 for r in RATIOS}))
    check(d["gates"]["beats_stride_on_graded"] is False, "stride gate sees vertex cells too")
    return 0 if failures == 0 else 1


def main():
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        raise SystemExit(self_test())
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path)
    parser.add_argument("--budget-ms", type=float, default=2000.0)
    parser.add_argument("--tie", type=float, default=0.02)
    parser.add_argument("--full-margin", type=float, default=0.9)
    args = parser.parse_args()
    decision = report(args.run, args.budget_ms, args.tie, args.full_margin)
    print(json.dumps({k: decision.get(k) for k in ("problem_count", "mode", "winner", "tied", "gates", "verdict")},
                     indent=2))


if __name__ == "__main__":
    main()
