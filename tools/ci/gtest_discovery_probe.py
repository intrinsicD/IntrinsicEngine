#!/usr/bin/env python3
"""Measure GoogleTest PRE_TEST discovery: direct listings and forced-stale CTest runs (BUG-091).

``direct`` reruns the exact ``--gtest_list_tests`` command a generated
``<target>[1]_include.cmake`` would run, cold (page cache evicted) and warm.
``ctest`` forces rediscovery of chosen targets, runs a CTest command and
reports which ``_tests.cmake`` files were rewritten. Both write JSON lines
with host telemetry gathered outside the timed window. ``decide`` applies the
BUG-091 budget rule to direct samples; ``report`` renders an HTML report.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

from collect_test_timing import _host_identity, _load_average

SCHEMA = "intrinsic.gtest-discovery-probe/v1"
CLANG = re.compile(r"^clang(\+\+)?(-\d+)?$")
LINKERS = {"ld", "ld.lld", "lld", "ld.gold", "mold", "ld.bfd"}
DEFAULT_TARGETS = ("IntrinsicECSTests", "IntrinsicCoreTests", "IntrinsicRuntimeContractTests")


class ProbeError(RuntimeError):
    pass


def parse_include(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    fields = dict(re.findall(r"(?m)^\s+([A-Z_]+) \[==\[(.*?)\]==\]\s*$", text))
    if text.count("gtest_discover_tests_impl(") != 1 or "TEST_EXECUTABLE" not in fields:
        raise ProbeError(f"{path}: unexpected GoogleTest include layout")
    # Fail closed: the rebuilt argv below omits these inputs.
    for key in ("TEST_EXECUTOR", "TEST_EXTRA_ARGS", "TEST_FILTER", "TEST_DISCOVERY_EXTRA_ARGS"):
        if fields.get(key, ""):
            raise ProbeError(f"{path}: {key} is not empty; probe would measure a different command")
    return fields


def _ancestors() -> set[int]:
    pids, pid = set(), os.getpid()
    while pid > 1 and pid not in pids:
        pids.add(pid)
        try:
            status = Path(f"/proc/{pid}/status").read_text()
        except OSError:
            break
        pid = int(re.search(r"(?m)^PPid:\s+(\d+)", status).group(1))
    return pids


def classify(argv: list[str]) -> str | None:
    """Classify a process by program name and argv, never by its joined command line."""
    program, args = os.path.basename(argv[0]), set(argv[1:])
    if CLANG.match(program) and "-c" in args:
        return "compile"
    if program.startswith("clang-scan-deps"):
        return "scan_deps"
    if program in LINKERS:
        return "link"
    if program in ("ninja", "ctest"):
        return program
    if program == "cmake" and "--build" in args:
        return "cmake_build"
    return None


def process_counts() -> dict[str, int]:
    counts = {"compile": 0, "scan_deps": 0, "link": 0, "ninja": 0, "ctest": 0, "cmake_build": 0}
    skip = _ancestors()
    for entry in os.scandir("/proc"):
        if not entry.name.isdigit() or int(entry.name) in skip:
            continue
        try:
            argv = Path(entry.path, "cmdline").read_bytes().decode(errors="replace").split("\0")
        except OSError:
            continue
        kind = classify(argv) if argv[0] else None
        if kind:
            counts[kind] += 1
    return counts


def _pressure() -> dict[str, float | None]:
    result = {}
    for kind in ("cpu", "io", "memory"):
        try:
            line = Path(f"/proc/pressure/{kind}").read_text().splitlines()[0]
            result[kind] = float(re.search(r"avg10=([\d.]+)", line).group(1))
        except (OSError, AttributeError, IndexError):
            result[kind] = None
    return result


def _memory() -> dict[str, int]:
    values = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, _, rest = line.partition(":")
        if key in ("MemAvailable", "SwapFree"):
            values[key] = int(rest.split()[0])
    return values


def telemetry() -> dict[str, object]:
    return {"load": _load_average(), "pressure": _pressure(), "memory": _memory(),
            "processes": process_counts()}


def mtimes(*paths: Path) -> dict[str, int | None]:
    return {path.name: (path.stat().st_mtime_ns if path.exists() else None) for path in paths}


def evict(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fdatasync(fd)
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
    finally:
        os.close(fd)


def residency(path: Path) -> int | None:
    if shutil.which("fincore") is None:
        return None
    result = subprocess.run(["fincore", "--bytes", "--noheadings", "--output", "RES", str(path)],
                            text=True, capture_output=True, timeout=30)
    return int(result.stdout.split()[0]) if result.returncode == 0 and result.stdout.split() else None


def idle(sample: dict[str, object], max_load: float) -> bool:
    return sample["load"].get("load_1m", math.inf) < max_load and sample["processes"]["compile"] == 0


def emit(record: dict[str, object], output) -> None:
    line = json.dumps(record, sort_keys=True)
    print(line, flush=True)
    if output is not None:
        output.write(line + "\n")
        output.flush()


def _deadline(args: argparse.Namespace) -> float:
    limit = time.monotonic() + args.max_minutes * 60
    if args.deadline:
        limit = min(limit, time.monotonic() + datetime.fromisoformat(args.deadline).timestamp() - time.time())
    return limit


def time_listing(fields: dict[str, str], scratch: Path, cap: float) -> dict[str, object]:
    json_out = scratch / "listing.json"
    json_out.unlink(missing_ok=True)
    command = [fields["TEST_EXECUTABLE"], "--gtest_list_tests", f"--gtest_output=json:{json_out}"]
    start = time.monotonic()
    process = subprocess.Popen(command, cwd=fields["TEST_WORKING_DIR"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    try:
        stdout, _ = process.communicate(timeout=cap)
        timed_out = False
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, _ = process.communicate()
        timed_out = True
    wall = time.monotonic() - start
    lines = stdout.decode(errors="replace").splitlines()
    cases = sum(1 for line in lines if line.startswith("  ") and line.strip())
    return {"wall_seconds": round(wall, 6), "exit_status": process.returncode, "timed_out": timed_out,
            "listed_cases": cases, "listing_sha256": hashlib.sha256(stdout).hexdigest(),
            "json_written": json_out.exists()}


def cmd_direct(args: argparse.Namespace) -> int:
    includes = {target: args.tests_dir / f"{target}[1]_include.cmake" for target in args.target}
    fields = {target: parse_include(path) for target, path in includes.items()}
    output = args.output.open("a", encoding="utf-8") if args.output else None
    limit = _deadline(args)
    emit({"event": "start", "schema": SCHEMA, "arm": args.arm, "argv": sys.argv,
          "host": _host_identity(1), "targets": args.target,
          "include_sha256": {t: hashlib.sha256(p.read_bytes()).hexdigest() for t, p in includes.items()},
          "executable_bytes": {t: Path(f["TEST_EXECUTABLE"]).stat().st_size for t, f in fields.items()}},
         output)
    stop = "completed"
    with tempfile.TemporaryDirectory(prefix="gtest-discovery-probe-") as scratch:
        for repetition in range(args.repetitions):
            shift = repetition % len(args.target)
            for target in args.target[shift:] + args.target[:shift]:
                exe = Path(fields[target]["TEST_EXECUTABLE"])
                for condition in ("cold", "warm"):
                    while True:
                        if time.monotonic() >= limit:
                            stop = "deadline"
                            break
                        before = telemetry()
                        if not args.wait_idle or idle(before, args.idle_max_load):
                            break
                        emit({"event": "wait_idle", "load": before["load"],
                              "processes": before["processes"]}, output)
                        time.sleep(min(args.poll_seconds, max(0.0, limit - time.monotonic())))
                    if stop != "completed":
                        break
                    if condition == "cold":
                        evict(exe)
                    resident = residency(exe)
                    result = time_listing(fields[target], Path(scratch), args.cap_seconds)
                    ctest_file = Path(fields[target]["CTEST_FILE"])
                    emit({"event": "sample", "arm": args.arm, "repetition": repetition, "target": target,
                          "condition": condition, "residency_bytes": resident, **result, **before,
                          "mtime_ns": mtimes(exe, includes[target], ctest_file),
                          "wall_clock": time.time()}, output)
                if stop != "completed":
                    break
            if stop != "completed":
                break
    emit({"event": "stop", "reason": stop}, output)
    return 0


def tests_files(tests_dir: Path) -> dict[str, int]:
    return {path.name.split("[", 1)[0]: path.stat().st_mtime_ns for path in tests_dir.glob("*_tests.cmake")}


def cmd_ctest(args: argparse.Namespace) -> int:
    if not args.command:
        raise ProbeError("ctest mode needs a command after --")
    before_files = tests_files(args.tests_dir)
    for target in args.force:
        include = args.tests_dir / f"{target}[1]_include.cmake"
        os.utime(include)
        if args.evict:
            evict(Path(parse_include(include)["TEST_EXECUTABLE"]))
    before = telemetry()
    start = time.monotonic()
    result = subprocess.run(args.command, text=True, capture_output=True, timeout=args.cap_seconds)
    wall = time.monotonic() - start
    after_files = tests_files(args.tests_dir)
    total = re.search(r"Total Tests: (\d+)", result.stdout)
    output = args.output.open("a", encoding="utf-8") if args.output else None
    emit({"event": "ctest", "schema": SCHEMA, "arm": args.arm, "command": args.command,
          "forced": args.force, "evicted": args.evict, "wall_seconds": round(wall, 6),
          "exit_status": result.returncode,
          "enumerated": sorted(t for t, m in after_files.items() if before_files.get(t) != m),
          "total_tests": int(total.group(1)) if total else None,
          "discovery_timeout_seen": "Process terminated due to timeout" in result.stdout + result.stderr,
          "output_tail": (result.stdout + result.stderr)[-2000:], **before, "wall_clock": time.time()},
         output)
    return 0



# --- BUG-091 decision rule (frozen with the pre-flight packet) ---------------
LADDER = (15, 20, 30, 45, 60)
CURRENT_BUDGET = 5.0
MIN_CELL_SAMPLES = 300
MAX_INVALID_SHARE = 0.05


def load_records(paths: list[Path]) -> list[dict]:
    records = []
    for path in paths:
        records += [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    return records


def rank(values: list[float], q: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * q) - 1)]


def invalid_reason(sample: dict, idle_max_load: float) -> str | None:
    if sample["condition"] == "cold" and sample.get("residency_bytes") != 0:
        return "residency"
    if sample["arm"] == "idle" and not idle(sample, idle_max_load):
        return "not-idle"
    return None


def summarize(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"n": 0}
    return {"n": len(values), "median": round(statistics.median(values), 6), "p95": rank(values, 0.95),
            "p99": rank(values, 0.99), "max": max(values),
            "at_or_above_5s": sum(v >= CURRENT_BUDGET for v in values)}


def evaluate(records: list[dict], args: argparse.Namespace) -> dict[str, object]:
    samples = [r for r in records if r.get("event") == "sample"]
    real = [r for r in records if r.get("event") == "ctest"]
    kills: list[str] = []
    hashes: dict[str, set[str]] = {}
    cells: dict[str, dict] = {}
    for s in samples:
        hashes.setdefault(s["target"], set()).add(s["listing_sha256"])
        if s["exit_status"] != 0 or s["timed_out"]:
            kills.append(f"nonzero or capped sample: {s['arm']}/{s['condition']}/{s['target']} "
                         f"rep {s['repetition']} exit={s['exit_status']} timed_out={s['timed_out']}")
        cell = cells.setdefault(f"{s['arm']}/{s['condition']}/{s['target']}",
                                {"valid": [], "invalid": {}, "compile_active": 0, "loads": []})
        cell["loads"].append(s["load"].get("load_1m", 0.0))
        cell["compile_active"] += s["processes"]["compile"] >= 1
        reason = invalid_reason(s, args.idle_max_load)
        if reason:
            cell["invalid"][reason] = cell["invalid"].get(reason, 0) + 1
        else:
            cell["valid"].append(s["wall_seconds"])
    kills += [f"listing hash changed for {t}: {len(h)} distinct" for t, h in hashes.items() if len(h) != 1]
    arms: dict[str, dict] = {}
    for name, cell in cells.items():
        arm = name.split("/", 1)[0]
        total = len(cell["valid"]) + sum(cell["invalid"].values())
        cell["collected"] = (len(cell["valid"]) >= MIN_CELL_SAMPLES
                             and sum(cell["invalid"].values()) <= MAX_INVALID_SHARE * total)
        summary = arms.setdefault(arm, {"cells": 0, "collected": 0, "loads": [], "compile_active": 0, "n": 0})
        summary["cells"] += 1
        summary["collected"] += cell["collected"]
        summary["loads"] += cell["loads"]
        summary["compile_active"] += cell["compile_active"]
        summary["n"] += total
    for arm, summary in arms.items():
        summary["median_load_1m"] = statistics.median(summary.pop("loads"))
        summary["compile_active_share"] = round(summary.pop("compile_active") / summary["n"], 4)
        summary["valid"] = summary["cells"] == len(args.targets) * 2 and summary["collected"] == summary["cells"]
        if arm == "contention":
            summary["valid"] = (summary["valid"] and summary["median_load_1m"] >= args.contention_min_load
                                and summary["compile_active_share"] >= 0.8)
    missing = [arm for arm in args.required_arms if not arms.get(arm, {}).get("valid")]
    kills += [f"required arm invalid or not collected: {arm}" for arm in missing]
    valid_walls = [w for c in cells.values() for w in c["valid"]]
    m_obs = max(valid_walls) if valid_walls else None
    real_timeouts = [r for r in real if r["discovery_timeout_seen"]]
    if (m_obs is not None and m_obs >= CURRENT_BUDGET) or real_timeouts:
        h1 = "supported"
    elif not missing and m_obs is not None and m_obs < CURRENT_BUDGET / 2:
        h1 = "rejected"
    else:
        h1 = "inconclusive"
    chosen = None
    if h1 == "supported" and not kills:
        need = 3 * max(CURRENT_BUDGET, m_obs or 0.0)
        fits = [b for b in LADDER if b >= need]
        if fits:
            chosen = fits[0]
        else:
            kills.append(f"3 x max(5, M_obs) = {need:.3f} s exceeds the 60 s ceiling")
    return {"schema": SCHEMA, "rule": "bug-091-budget/v1", "m_obs_seconds": m_obs, "h1": h1,
            "kill_conditions": kills, "chosen_budget_seconds": chosen,
            "decision": (f"set DISCOVERY_TIMEOUT {chosen}" if chosen is not None else
                         "no value chosen: keep explicit 5 s; operator decision"),
            "arms": arms,
            "cells": {name: {**summarize(c["valid"]), "invalid": c["invalid"], "collected": c["collected"]}
                      for name, c in sorted(cells.items())},
            "hypotheses": {"H3": h3(cells, args.targets), "H4": h4(cells, args.targets, args.ecs_target)},
            "ctest_runs": [{k: r[k] for k in ("arm", "exit_status", "wall_seconds", "enumerated",
                                               "total_tests", "discovery_timeout_seen")} for r in real]}


def h3(cells: dict, targets: list[str]) -> dict[str, object]:
    verdict = {}
    for condition in ("cold", "warm"):
        ratios = {}
        for target in targets:
            busy, quiet = cells.get(f"contention/{condition}/{target}"), cells.get(f"idle/{condition}/{target}")
            if busy and quiet and busy["collected"] and quiet["collected"]:
                ratios[target] = round(rank(busy["valid"], 0.99) / rank(quiet["valid"], 0.99), 3)
        verdict[condition] = {"p99_ratio": ratios, "supported": sum(r >= 2 for r in ratios.values()) >= 2,
                              "assessed": len(ratios) == len(targets)}
    return verdict


def h4(cells: dict, targets: list[str], ecs: str) -> dict[str, object]:
    verdict = {}
    for condition in ("cold", "warm"):
        outliers = {}
        for target in targets:
            cell = cells.get(f"idle/{condition}/{target}")
            if cell and cell["collected"]:
                median = statistics.median(cell["valid"])
                outliers[target] = sum(w > max(10 * median, median + 0.5) for w in cell["valid"])
        assessed = len(outliers) == len(targets)
        verdict[condition] = {"outliers": outliers, "assessed": assessed,
                              "supported": assessed and outliers[ecs] >= 3
                              and all(v <= 1 for t, v in outliers.items() if t != ecs)}
    return verdict


def cmd_decide(args: argparse.Namespace) -> int:
    print(json.dumps(evaluate(load_records(args.inputs), args), indent=2, sort_keys=True))
    return 0



STYLE = """
.viz-root{color-scheme:light;--surface-1:#fcfcfb;--text-primary:#0b0b0b;--text-secondary:#52514e;
--grid:#e4e3df;--series-1:#2a78d6;--series-2:#eb6834;--series-3:#1baf7a;--ref:#52514e;
font:14px system-ui,sans-serif;background:var(--surface-1);color:var(--text-primary);padding:16px}
@media (prefers-color-scheme:dark){:root:where(:not([data-theme="light"])) .viz-root{color-scheme:dark;
--surface-1:#1a1a19;--text-primary:#f0efec;--text-secondary:#c3c2b7;--grid:#3a3a38;--series-1:#3987e5;
--series-2:#d95926;--series-3:#199e70;--ref:#c3c2b7}}
table{border-collapse:collapse;margin:8px 0}td,th{padding:2px 8px;border-bottom:1px solid var(--grid);
text-align:right}th:first-child,td:first-child{text-align:left}text{fill:var(--text-secondary);font-size:11px}
pre{white-space:pre-wrap;font-size:12px}
"""


def _svg_scatter(title: str, series: dict[str, list[tuple[float, float]]], t0: float, t1: float,
                 log: bool, refs: list[tuple[float, str]], marks: list[tuple[float, str]]) -> str:
    width, height, left, bottom = 860, 220, 56, 28
    values = [y for points in series.values() for _, y in points] + [r for r, _ in refs]
    if not values:
        return f"<h3>{title}</h3><p>no samples</p>"
    floor = 1e-3
    lo, hi = (min(max(v, floor) for v in values), max(values)) if log else (0.0, max(values) or 1.0)
    lo, hi = (math.log10(lo) - 0.1, math.log10(hi) + 0.1) if log else (lo, hi * 1.08)
    def x(v: float) -> float: return left + (v - t0) / max(t1 - t0, 1e-9) * (width - left - 8)
    def y(v: float) -> float:
        v = math.log10(max(v, floor)) if log else v
        return height - bottom - (v - lo) / max(hi - lo, 1e-9) * (height - bottom - 8)
    parts = [f'<h3>{title}</h3><svg width="{width}" height="{height}" role="img" aria-label="{title}">']
    ticks = [10 ** e for e in range(math.floor(lo), math.ceil(hi) + 1)] if log else [hi / 4 * i for i in range(5)]
    for tick in ticks:
        if (math.log10(tick) if log else tick) < lo or (math.log10(tick) if log else tick) > hi:
            continue
        parts.append(f'<line x1="{left}" x2="{width - 8}" y1="{y(tick):.1f}" y2="{y(tick):.1f}" '
                     f'stroke="var(--grid)"/><text x="{left - 4}" y="{y(tick) + 4:.1f}" '
                     f'text-anchor="end">{tick:g}</text>')
    for at, label in marks:
        parts.append(f'<line x1="{x(at):.1f}" x2="{x(at):.1f}" y1="8" y2="{height - bottom}" '
                     f'stroke="var(--ref)" stroke-dasharray="2 3"/><text x="{x(at) + 3:.1f}" y="18">{label}</text>')
    for value, label in refs:
        parts.append(f'<line x1="{left}" x2="{width - 8}" y1="{y(value):.1f}" y2="{y(value):.1f}" '
                     f'stroke="var(--ref)" stroke-width="2"/><text x="{width - 10}" y="{y(value) - 4:.1f}" '
                     f'text-anchor="end">{label}</text>')
    for index, (name, points) in enumerate(series.items()):
        color = f"var(--series-{index + 1})"
        for at, value in points:
            parts.append(f'<circle cx="{x(at):.1f}" cy="{y(value):.1f}" r="2.5" fill="{color}">'
                         f'<title>{name}: {value:.4g} at +{(at - t0) / 60:.1f} min</title></circle>')
    legend = " ".join(f'<tspan fill="var(--series-{i + 1})">&#9679;</tspan> {name}'
                      for i, name in enumerate(series))
    parts.append(f'<text x="{left}" y="{height - 6}">{legend} (x: wall clock, minutes from first sample)</text></svg>')
    return "".join(parts)


def cmd_report(args: argparse.Namespace) -> int:
    records = load_records(args.inputs)
    verdict = evaluate(records, args)
    samples = [r for r in records if r.get("event") == "sample"]
    real = [r for r in records if r.get("event") == "ctest"]
    stamped = samples + real
    t0 = min((r["wall_clock"] for r in stamped), default=0.0)
    t1 = max((r["wall_clock"] for r in stamped), default=1.0)
    marks = []
    for arm in sorted({r["arm"] for r in stamped}):
        marks.append((min(r["wall_clock"] for r in stamped if r["arm"] == arm), f"start {arm}"))
    refs = [(CURRENT_BUDGET, "current budget 5 s")]
    if verdict["chosen_budget_seconds"]:
        refs.append((float(verdict["chosen_budget_seconds"]), f"chosen {verdict['chosen_budget_seconds']} s"))
    html = [f"<!doctype html><meta charset=utf-8><title>BUG-091 discovery probe</title><style>{STYLE}</style>"
            '<body class="viz-root"><h1>BUG-091 GoogleTest discovery probe</h1>'
            f"<p>Inputs: {', '.join(str(p) for p in args.inputs)}</p>"
            f"<h2>Frozen verdicts</h2><pre>{json.dumps({k: verdict[k] for k in ('h1', 'm_obs_seconds', 'decision', 'chosen_budget_seconds', 'kill_conditions', 'hypotheses', 'arms')}, indent=2, sort_keys=True)}</pre>"
            "<h2>Cells (valid samples, seconds)</h2><table><tr><th>cell</th><th>n</th><th>median</th>"
            "<th>p95</th><th>p99</th><th>max</th><th>&ge;5 s</th><th>invalid</th><th>collected</th></tr>"]
    for name, cell in verdict["cells"].items():
        html.append(f"<tr><td>{name}</td><td>{cell['n']}</td>" + "".join(
            f"<td>{cell.get(k, '')}</td>" for k in ("median", "p95", "p99", "max", "at_or_above_5s"))
            + f"<td>{cell['invalid'] or ''}</td><td>{cell['collected']}</td></tr>")
    html.append("</table>")
    for target in args.targets:
        series = {f"{c} wall s": [(s["wall_clock"], s["wall_seconds"]) for s in samples
                                  if s["target"] == target and s["condition"] == c] for c in ("cold", "warm")}
        html.append(_svg_scatter(f"{target}: listing wall time (log s)", series, t0, t1, True, refs, marks))
    html.append(_svg_scatter("Load average, 1 min (at sample start)",
                             {"load_1m": [(s["wall_clock"], s["load"].get("load_1m", 0.0)) for s in stamped]},
                             t0, t1, False, [], marks))
    html.append(_svg_scatter("Concurrent compile processes (at sample start)",
                             {"compile": [(s["wall_clock"], float(s["processes"]["compile"])) for s in stamped]},
                             t0, t1, False, [], marks))
    html.append("<h2>Forced-stale CTest runs</h2><table><tr><th>arm</th><th>exit</th><th>wall s</th>"
                "<th>enumerated</th><th>total</th><th>timeout seen</th></tr>")
    for run in verdict["ctest_runs"]:
        html.append(f"<tr><td>{run['arm']}</td><td>{run['exit_status']}</td><td>{run['wall_seconds']}</td>"
                    f"<td>{len(run['enumerated'])}</td><td>{run['total_tests']}</td>"
                    f"<td>{run['discovery_timeout_seen']}</td></tr>")
    html.append("</table></body>")
    args.html.write_text("".join(html), encoding="utf-8")
    print(json.dumps({"html": str(args.html), "decision": verdict["decision"]}))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode", required=True)
    for name in ("direct", "ctest"):
        mode = sub.add_parser(name)
        mode.add_argument("--tests-dir", type=Path, required=True, help="directory of *_include.cmake")
        mode.add_argument("--arm", default="unlabelled")
        mode.add_argument("--output", type=Path, help="append JSON lines here as well as stdout")
        mode.add_argument("--cap-seconds", type=float, default=120.0 if name == "direct" else 600.0)
    direct = sub.choices["direct"]
    direct.add_argument("--target", action="append", required=True)
    direct.add_argument("--repetitions", type=int, default=1)
    direct.add_argument("--max-minutes", type=float, default=35.0)
    direct.add_argument("--deadline", help="ISO time with offset; stop before it")
    direct.add_argument("--wait-idle", action="store_true")
    direct.add_argument("--idle-max-load", type=float, default=2.0)
    direct.add_argument("--poll-seconds", type=float, default=120.0)
    ctest = sub.choices["ctest"]
    ctest.add_argument("--force", action="append", default=[], help="target whose include is touched")
    ctest.add_argument("--evict", action="store_true", help="evict forced executables from page cache")
    ctest.add_argument("command", nargs=argparse.REMAINDER)
    for name in ("decide", "report"):
        mode = sub.add_parser(name)
        mode.add_argument("inputs", type=Path, nargs="+", help="JSON lines from direct/ctest runs")
        mode.add_argument("--targets", nargs="+", default=list(DEFAULT_TARGETS))
        mode.add_argument("--ecs-target", default="IntrinsicECSTests")
        mode.add_argument("--required-arms", nargs="+", default=["contention", "idle"])
        mode.add_argument("--contention-min-load", type=float, default=4.0)
        mode.add_argument("--idle-max-load", type=float, default=2.0)
    sub.choices["report"].add_argument("--html", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.mode == "ctest" and args.command[:1] == ["--"]:
        args.command = args.command[1:]
    try:
        return {"direct": cmd_direct, "ctest": cmd_ctest, "decide": cmd_decide,
                "report": cmd_report}[args.mode](args)
    except (OSError, ProbeError, subprocess.SubprocessError) as error:
        print(f"gtest discovery probe: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
