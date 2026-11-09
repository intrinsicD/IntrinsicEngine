#!/usr/bin/env python3
"""Render ARCH-024 raw observations and seal its three fixed workload summaries."""
from __future__ import annotations

import argparse
import html
import json
import math
from pathlib import Path
import statistics
import uuid

from method_execution_campaign import ROOT, digest, file_digest, read, write
from seal_benchmark_results import canonicalize
from validate_benchmark_results import load_manifests, validate_result_data


def measured(payload):
    return [run for run in payload.get("runs", []) if not run.get("warmup", False)]


def canonical_identity(outcomes):
    execution = outcomes.get("execution_id", "")
    if not isinstance(execution, str) or uuid.UUID(hex=execution).hex != execution:
        raise ValueError("Missing or invalid execution identity")
    return "arch024-" + execution, "execution-" + execution


def validated_runs(payload):
    mode = payload["config"]["mode"]
    expected = [0] if mode == "controls" else [0, 1, 2, 3]
    runs = payload.get("runs", [])
    identities = [run.get("repeat") for run in runs]
    if any(type(i) is not int for i in identities) or sorted(identities) != expected:
        raise ValueError(f"{mode}: missing, duplicated or unexpected logical run identity")
    for run in runs:
        if run.get("warmup") is not (mode != "controls" and run["repeat"] == 0):
            raise ValueError(f"{mode}: warmup identity differs from the frozen repeat plan")
        if run.get("quality_passed") is not True:
            raise ValueError(f"{mode}: unchecked logical run")
        if mode == "queries":
            pages = run.get("page_samples", [])
            starts = [page.get("first") for page in pages]
            if (run.get("completed_queries") != 256 or any(type(i) is not int for i in starts) or
                    sorted(starts) != list(range(0, 256, 16)) or
                    any(page.get("completed_queries") != page["first"] + 16 for page in pages)):
                raise ValueError("queries: incomplete sweep or missing/duplicated page coverage")
    return measured(payload)


def paired_errors(payloads):
    """A queue/preview comparison is invalid when it did different completed work."""
    groups = {}
    for identifier, payload in payloads.items():
        config = payload["config"]
        if config["mode"] != "kmeans":
            continue
        for run in validated_runs(payload):
            key = (config["points"], config["seed"], run["repeat"])
            identity = (payload["cloud_hash"], run["labels_hash"], run["iterations"], run["converged"])
            groups.setdefault(key, []).append((identifier, identity))
    return [f"paired completed work differs: {key}: {[name for name, _ in values]}"
            for key, values in groups.items() if len({identity for _, identity in values}) != 1]


def paired_distribution(payloads):
    """Descriptive seed-paired ratios; no significance claim or pooled repeat count."""
    by_seed = {}
    for payload in payloads.values():
        config = payload["config"]
        mode = config["mode"]
        if mode not in {"kmeans", "queries"}:
            continue
        arm = (config["queue"], config["preview_every"], config["preview_buffers"]) if mode == "kmeans" else config["strategy"]
        times = [r["ready_computed_ms" if mode == "kmeans" else "runtime_ms"] for r in validated_runs(payload)]
        if times:
            by_seed.setdefault((mode, config["points"], config["seed"]), {})[arm] = times
    groups = {}
    for (mode, points, seed), arms in by_seed.items():
        comparisons = [("strategy", "brute_force", "lbvh")] if mode == "queries" else (
            [("queue", ("graphics", preview, buffers), ("separate", preview, buffers))
             for preview, buffers in ((0, 3), (1, 2), (1, 3))] +
            [("preview", (queue, 0, 3), (queue, 1, buffers))
             for queue in ("graphics", "separate") for buffers in (2, 3)])
        for axis, baseline_arm, arm in comparisons:
            baseline, times = arms.get(baseline_arm), arms.get(arm)
            if not baseline or not times:
                continue
            key = f"{mode}, N={points}, {axis}: {arm} / {baseline_arm}"
            groups.setdefault(key, []).append({"seed": seed,
                "median_latency_ratio": statistics.median(times) / statistics.median(baseline),
                "baseline_spread_ms": max(baseline) - min(baseline),
                "arm_spread_ms": max(times) - min(times)})
    return {key: {"paired_seeds": values,
                  "median_ratio": statistics.median(v["median_latency_ratio"] for v in values),
                  "range_ratio": [min(v["median_latency_ratio"] for v in values), max(v["median_latency_ratio"] for v in values)]}
            for key, values in groups.items()}


def summary(payloads, mode):
    selected = {name: p for name, p in payloads.items() if p["config"]["mode"] == mode}
    if not selected:
        return None  # Missing observations never become synthetic zero timings.
    runtime, throughput, errors = {}, {}, []
    frame_mean, frame_p99 = {}, {}
    memory_peak, frame_count = 0, 0
    for name, payload in selected.items():
        runs = validated_runs(payload)
        durations = [float(r["runtime_ms"] if mode == "queries" else r["ready_computed_ms"]) for r in runs]
        if any(not math.isfinite(t) or t <= 0 for t in durations):
            raise ValueError(f"Non-positive completed-work duration: {name}")
        runtime[name] = statistics.mean(durations)
        work = [r["completed_queries"] if mode == "queries" else payload["config"]["points"] * r["iterations"] for r in runs]
        if any(n <= 0 for n in work) or any(r.get("quality_passed") is not True for r in runs):
            raise ValueError(f"Empty/unchecked work cannot contribute throughput: {name}")
        throughput[name] = statistics.mean(n * 1000 / t for n, t in zip(work, durations))
        errors.extend(float(r["quality_error_linf"]) for r in runs)
        # Includes instrumentation/control frames: phase tags expose, rather than silently trim, their cost.
        frames = payload.get("frames", [])
        times = sorted(float(f["total_us"]) / 1000 for f in frames)
        if not times:
            raise ValueError(f"Passed GPU cell has no frame observations: {name}")
        frame_mean[name] = statistics.mean(times)
        frame_p99[name] = times[max(0, math.ceil(.99 * len(times)) - 1)]
        frame_count += len(times)
        memory_peak = max(memory_peak, *(f["readback_ring_high_water_bytes"] for f in frames))
    return {"runtime_ms": runtime, "throughput_items_per_sec": throughput,
            "memory_peak_bytes": memory_peak, "quality_error_linf": max(errors),
            "sample_count": len(selected), "avgFrameTimeMs": frame_mean,
            "p99FrameTimeMs": frame_p99, "totalFrames": frame_count}


def chart(title, samples, xkey, ykey, events=()):
    points = [(float(s[xkey]), float(s[ykey])) for s in samples
              if isinstance(s.get(xkey), (int, float)) and isinstance(s.get(ykey), (int, float))]
    if not points:
        return ""
    xmin, xmax = min(x for x, _ in points), max(x for x, _ in points)
    ymin, ymax = min(0., min(y for _, y in points)), max(y for _, y in points)
    def xy(x, y):
        return 45 + 680 * (x - xmin) / max(xmax - xmin, 1e-12), 150 - 125 * (y - ymin) / max(ymax - ymin, 1e-12)
    line = " ".join(f"{x:.2f},{y:.2f}" for x, y in (xy(*point) for point in points))
    marks = []
    for event in events:
        x = float(event["time_ms"])
        if xmin <= x <= xmax:
            px, _ = xy(x, 0)
            marks.append(f'<line x1="{px:.2f}" x2="{px:.2f}" y1="20" y2="150" stroke="#aaa"><title>{html.escape(event["name"])}</title></line>')
    return (f'<figure><figcaption>{html.escape(title)}</figcaption><svg viewBox="0 0 750 185" role="img" aria-label="{html.escape(title)}">'
            f'<text x="2" y="16">{ymax:.6g}</text><text x="2" y="165">{ymin:.6g}</text>'
            + "".join(marks) + f'<polyline points="{line}" fill="none" stroke="#176cc4" stroke-width="1.4"/>'
            f'<text x="45" y="180">{xmin:.6g}</text><text x="640" y="180">{xmax:.6g} {html.escape(xkey)}</text></svg></figure>')


def cloud_view(payload):
    points = payload.get("points_preview", [])
    runs = measured(payload)
    labels = runs[-1].get("labels_preview", runs[-1].get("labels", [])[:len(points)]) if runs else []
    if not points:
        return ""
    colors = ("#0072b2", "#e69f00", "#009e73", "#cc79a7", "#d55e00", "#56b4e9", "#766400")
    xmin, xmax = min(p[0] for p in points), max(p[0] for p in points)
    ymin, ymax = min(p[1] for p in points), max(p[1] for p in points)
    views = []
    for title, after in (("Input: first 256 XY points", False), ("Accepted labels: same points", True)):
        if after and len(labels) < len(points):
            continue
        circles = []
        for i, p in enumerate(points):
            x = 15 + 420 * (p[0] - xmin) / max(xmax - xmin, 1e-12)
            y = 170 - 150 * (p[1] - ymin) / max(ymax - ymin, 1e-12)
            color = colors[int(labels[i]) % len(colors)] if after else "#777"
            circles.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="2.5" fill="{color}"/>')
        views.append(f'<figure><figcaption>{title}</figcaption><svg viewBox="0 0 450 190">' + "".join(circles) + '</svg></figure>')
    return '<div class="views">' + "".join(views) + '</div>'


def load_observations(directory, packet, outcomes):
    if outcomes.get("packet_sha256") != digest(packet):
        raise ValueError("Outcome packet binding differs")
    planned = {cell["id"]: cell for cell in packet["cells"]}
    payloads, failed, seen = {}, {}, set()
    for outcome in outcomes["cells"]:
        identifier = outcome["id"]
        if identifier not in planned or identifier in seen:
            raise ValueError("Unknown or duplicate outcome cell")
        seen.add(identifier)
        passed = outcome["status"] == "passed"
        if not passed and "output_sha256" not in outcome:
            continue
        path = (directory / outcome["output"]).resolve()
        if not path.is_relative_to(directory.resolve()) or file_digest(path) != outcome["output_sha256"]:
            raise ValueError("Raw output path/digest differs")
        try:
            payload = read(path)
        except (ValueError, OSError) as error:
            if passed:
                raise
            failed[identifier] = {"config": planned[identifier], "failure": "Unparseable raw output: " + str(error)}
            continue
        config = read(directory / "configs" / (identifier + ".json"))
        if file_digest(directory / "configs" / (identifier + ".json")) != outcome["config_sha256"]:
            raise ValueError("Resolved cell config changed")
        if any(config.get(k) != v for k, v in planned[identifier].items()):
            raise ValueError("Resolved config differs from frozen cell")
        if passed and payload.get("technical_passed") is not True:
            raise ValueError("Passed outcome contains failed raw assertions")
        if planned[identifier]["mode"] != "reference":
            if passed and (payload.get("config") != config or not payload.get("complete")):
                raise ValueError("Raw config/completion differs")
            if passed:
                validated_runs(payload)
                payloads[identifier] = payload
            else:
                failed[identifier] = payload
    return payloads, failed


def render(directory):
    packet, outcomes = read(directory / "packet.json"), read(directory / "outcomes.json")
    run_id, attempt_id = canonical_identity(outcomes)
    payloads, failed_payloads = load_observations(directory, packet, outcomes)
    errors = paired_errors(payloads)
    paired = paired_distribution(payloads) if not errors else {"invalid": errors}
    manifests_root = ROOT / "benchmarks"
    manifests = load_manifests(manifests_root)
    results, verdicts = {}, {}
    for mode in ("kmeans", "queries", "controls"):
        metrics = summary(payloads, mode)
        expected = sum(c["mode"] == mode for c in packet["cells"])
        if metrics is None:
            verdicts[mode] = "MISSING: no completed observations; no result synthesized"
            continue
        identifier = "runtime.method_execution." + mode
        path, manifest = manifests[identifier]
        if file_digest(path) != packet["source"]["files"][path.relative_to(ROOT).as_posix()]:
            raise ValueError("Manifest changed after packet freeze")
        complete = metrics["sample_count"] == expected and not (mode == "kmeans" and errors)
        raw = {"benchmark_id": identifier, "method": manifest["method"], "dataset": manifest["dataset"],
               "backend": "gpu_vulkan_compute", "execution_status": "passed" if complete else "error",
               "metrics": metrics, "diagnostics": {
                   "packet_sha256": digest(packet), "execution_id": outcomes["execution_id"],
                   "paired_work_errors": errors if mode == "kmeans" else [],
                   "planned_cells": expected, "observed_cells": metrics["sample_count"],
                   "memory_scope": "sampled readback-ring staging high-water only; other overlapping components remain separate raw series",
                   "runtime_scope": "query completed page sums; K-Means Ready to computed (includes control pauses for controls)",
                   "throughput_scope": "logical N times completed iterations per second; queries per second; not GPU instruction rate",
                   "frame_scope": "all observed frames including explicitly tagged instrumentation and owner-delay control",
                   "independent_units": "five seeds; three repeats per seed are noise estimates, not independent samples",
                   "paired_latency_ratios": {k: v for k, v in paired.items() if k.startswith(mode + ",")},
                   "frame_extremes_ms": {name: {
                       "maximum": max(f["total_us"] for f in p["frames"]) / 1000,
                       "p95": sorted(f["total_us"] for f in p["frames"])[max(0, math.ceil(.95 * len(p["frames"])) - 1)] / 1000}
                       for name, p in payloads.items() if p["config"]["mode"] == mode},
                   "accounted_component_maxima_bytes": {name: {
                       key: max(f[key] for f in p["frames"])
                       for key in ("resident_bytes", "index_bytes", "scratch_active_bytes", "scratch_pending_bytes", "scratch_idle_bytes", "readback_ring_high_water_bytes")}
                       for name, p in payloads.items() if p["config"]["mode"] == mode},
                   "environment": packet["environment"], "raw_outcomes": "../outcomes.json"}}
        canonical = canonicalize(raw, manifest_path=path, manifest=manifest, manifests_root=manifests_root,
                                 run_id=run_id, attempt_id=attempt_id,
                                 source_state=packet["source"]["state"], source_revision=packet["source"]["revision"],
                                 claim_eligible=False, snapshot_sha256=None, diff_sha256=None)
        # A result becomes claim eligible only after the required post-run referee audit.
        result_path = directory / "results" / (mode + ".json")
        _, validation_errors = validate_result_data(canonical, result_path, manifests, manifests_root)
        if validation_errors:
            raise ValueError("; ".join(validation_errors))
        write(result_path, canonical)
        results[mode] = canonical
        verdicts[mode] = canonical["status"].upper() + ": " + json.dumps(canonical["threshold_disposition"])
    sections = ['<!doctype html><html lang="en"><meta charset="utf-8"><title>ARCH-024 method execution</title>',
                '<style>body{font:15px system-ui;max-width:1400px;margin:2em auto;padding:0 1em;background:#fafafa;color:#172333}table{border-collapse:collapse;width:100%}td,th{padding:.4em;border-bottom:1px solid #ccd;text-align:left}pre{white-space:pre-wrap;overflow-wrap:anywhere}figure{margin:1em 0}svg{width:100%;max-height:230px;background:white}.views{display:flex;gap:1em}.views figure{width:48%}details{margin:1em 0;padding:.5em;border:1px solid #ccd}</style>',
                '<h1>Method execution: frozen observations</h1>',
                '<p>No performance verdict is inferred automatically. Five paired seeds are independent units; within-process repeats estimate noise. Missing and failed cells remain visible. Results await a post-run referee audit.</p>',
                '<p>Fixed reference triangle is rendering contention load in every GPU arm; the point cloud is unrendered, so these observations measure preview publication, not renderer consumption of method previews. All-frame percentiles include warmup, deliberate delay and Undo overhead; they are not steady-state latency. Queue identity does not prove hardware overlap. Memory components can overlap and are never summed into a total. Traffic is accounted host/device API traffic, not hardware bus bytes. Preview shown-step transitions are observations, not an exact drop count. These algorithms have no training-loss decomposition.</p>',
                '<pre>' + html.escape(json.dumps({"packet_sha256": digest(packet), "execution_id": outcomes["execution_id"], "source": packet["source"], "environment": packet["environment"], "frozen_verdicts": verdicts, "paired_work_errors": errors}, indent=2)) + '</pre>',
                '<h2>Paired latency distributions</h2><p>Ratio below one means lower observed latency. Ranges are across paired seeds; repeat spreads describe within-process noise. These descriptive ratios are not a significance or practical-improvement verdict.</p><pre>' + html.escape(json.dumps(paired, indent=2)) + '</pre>',
                '<h2>Every frozen cell</h2><table><tr><th>Cell</th><th>Outcome</th><th>Evidence</th></tr>']
    observed = {r["id"]: r for r in outcomes["cells"]}
    for cell in packet["cells"]:
        outcome = observed.get(cell["id"], {})
        status = outcome.get("status", "MISSING / not run")
        evidence = outcome.get("failure", outcome.get("output", "No observation"))
        sections.append(f'<tr><td>{html.escape(cell["id"])}</td><td>{html.escape(status)}</td><td>{html.escape(evidence)}</td></tr>')
    sections.append('</table><h2>Observed trajectories, including rejected cells</h2>')
    for identifier, payload in list(failed_payloads.items()) + list(payloads.items()):
        failed = identifier in failed_payloads
        sections.append('<details' + (' open' if failed else '') + '><summary>' +
                        ('FAILED — EXCLUDED FROM PASS SUMMARIES: ' if failed else '') + html.escape(identifier) + '</summary>')
        if failed:
            sections.append('<p><strong>Rejected observation. Available curves are diagnostic only:</strong> ' +
                            html.escape(str(payload.get("failure", observed[identifier].get("failure", "Cell failed")))) + '</p>')
        runs, frames, events = measured(payload), payload.get("frames", []), payload.get("events", [])
        sections.append(cloud_view(payload))
        sections.append('<pre>' + html.escape(json.dumps({"queue": payload.get("queue"), "controls_ms": {k: payload[k] for k in ("pause_ms", "step_ms", "stop_ms") if k in payload}, "runs": [{k: v for k, v in r.items() if k not in {"metrics", "page_samples", "labels_preview"}} for r in runs], "events": events}, indent=2)) + '</pre>')
        for key in ("total_us", "variable_tick_us", "command_us", "completion_us", "collect_us", "gpu_drain_us", "record_us", "present_us", "steps", "preview_copies", "shown_step", "resident_bytes", "index_bytes", "scratch_active_bytes", "scratch_pending_bytes", "scratch_idle_bytes", "residency_upload_bytes", "residency_readback_bytes", "query_upload_bytes", "readback_staged_bytes", "readback_ring_high_water_bytes"):
            sections.append(chart(key + " over process time", frames, "time_ms", key, events))
        for run in payload.get("runs", []):
            prefix = f'Repeat {run["repeat"]} (warmup={run.get("warmup", False)}) '
            for index, name in enumerate(run.get("metric_names", [])):
                if name:
                    samples = [{"step": m["step"], "value": m["values"][index]} for m in run["metrics"]]
                    sections.append(chart(prefix + name + " (native metric units)", samples, "step", "value"))
            for key in ("runtime_ms", "completed_queries", "upload_bytes", "readback_bytes"):
                sections.append(chart(prefix + "page " + key, run.get("page_samples", []), "time_ms", key, events))
        sections.append('</details>')
    sections.append('</html>')
    (directory / "report.html").write_text("\n".join(sections))
    write(directory / "report_verdict.json", {"packet_sha256": digest(packet), "execution_id": outcomes["execution_id"], "workloads": verdicts,
                                             "paired_work_errors": errors, "claim_eligible": False})
    return 0 if len(results) == 3 and all(r["status"] == "passed" for r in results.values()) else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    directory = parser.parse_args().root.resolve()
    try:
        raise SystemExit(render(directory))
    except (ValueError, OSError, KeyError, TypeError) as error:
        # Invalid input is itself retained as a failed report, never a partly trusted plot.
        (directory / "report.html").write_text('<!doctype html><meta charset="utf-8"><title>Rejected method report</title><h1>Report rejected</h1><pre>' + html.escape(str(error)) + '</pre><p>Raw outputs remain unjudged. See packet.json, outcomes.json and logs.</p>')
        raise SystemExit(2)
