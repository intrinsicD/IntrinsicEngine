#!/usr/bin/env python3
"""Measure GoogleTest PRE_TEST discovery: direct listings and forced-stale CTest runs (BUG-091).

``direct`` reruns the exact ``--gtest_list_tests`` command a generated
discovery call would run (in ``<target>[1]_include.cmake``; on CMake 4.4+ in the
``<target>_<sha8>_discovery.cmake`` that ``<target>_<sha8>_include.cmake``
includes), cold (page cache evicted) and warm.
``ctest`` forces rediscovery of chosen targets, runs a CTest command and
reports which ``_tests.cmake`` files were rewritten. Both write JSON lines
with host telemetry gathered outside the timed window. ``decide`` applies the
BUG-091 budget rule to direct samples; ``report`` renders an HTML report.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime
import glob
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

from collect_test_timing import _host_identity, _load_average

SCHEMA = "intrinsic.gtest-discovery-probe/v1"
CLANG = re.compile(r"^clang(\+\+)?(-\d+)?$")
LINKERS = {"ld", "ld.lld", "lld", "ld.gold", "mold", "ld.bfd"}
DEFAULT_TARGETS = ("IntrinsicECSTests", "IntrinsicCoreTests", "IntrinsicRuntimeContractTests")


class ProbeError(RuntimeError):
    pass


# Generated PRE_TEST files: <target>[N]_<kind>.cmake before CMake 4.4; 4.4 names them
# MAKE_C_IDENTIFIER(<target>_<sha8>) (sha8: SHA-256 prefix of the call's arguments) and moves the
# discovery call from _include.cmake into a _discovery.cmake that the include includes.
GENERATED = re.compile(r"(.+)(\[\d+\]|_[0-9a-f]{8})_(include|discovery|tests)\.cmake")
LAYOUTS = "<target>[N]_include.cmake (CMake < 4.4) or <target>_<sha8>_include.cmake (4.4+)"


def generated_target(path: Path) -> str:
    match = GENERATED.fullmatch(path.name)
    if match is None:
        raise ProbeError(f"{path}: unknown GoogleTest discovery file layout; known: {LAYOUTS}")
    return match[1]


def target_include(tests_dir: Path, target: str) -> Path:
    """The target's one generated include ([1] before 4.4); none or several is an error, not a guess."""
    own = re.compile(rf"{re.escape(target)}(\[1\]|_[0-9a-f]{{8}})_include\.cmake")
    found = sorted(p for p in tests_dir.glob(f"{glob.escape(target)}*_include.cmake") if own.fullmatch(p.name))
    if len(found) != 1:
        raise ProbeError(f"{tests_dir}: {len(found)} GoogleTest discovery includes for {target} "
                         f"{[p.name for p in found]}; expected one <target>[1]_include.cmake (CMake < 4.4) "
                         "or <target>_<sha8>_include.cmake (4.4+)")
    return found[0]


def discovery_call(include: Path) -> Path:
    """The file holding the include's gtest_discover_tests_impl call."""
    generated_target(include)
    if include.name.endswith("]_include.cmake"):
        return include
    call = include.with_name(include.name.removesuffix("_include.cmake") + "_discovery.cmake")
    if include.read_text(encoding="utf-8").count(f'include("{call}")') != 1:
        raise ProbeError(f"{include}: expected one include of {call.name}; unknown GoogleTest include layout")
    return call


def call_fields(text: str) -> dict[str, str]:
    # CMake 4.4 writes list fields as separate bracket arguments; join them as one CMake list.
    return {key: value.replace("]==] [==[", ";")
            for key, value in re.findall(r"(?m)^\s+([A-Z_]+) \[==\[(.*?)\]==\]\s*$", text)}


def parse_include(path: Path) -> dict[str, str | bool]:
    """Fields of the discovery call in ``path`` (see discovery_call)."""
    text = path.read_text(encoding="utf-8")
    fields: dict[str, str | bool] = call_fields(text)
    module = re.findall(r'(?m)^\s*include\("([^"]*/GoogleTestAddTests\.cmake)"\)\s*$', text)
    if text.count("gtest_discover_tests_impl(") != 1 or "TEST_EXECUTABLE" not in fields or len(module) != 1:
        raise ProbeError(f"{path}: unexpected GoogleTest include layout")
    # Fail closed: the rebuilt argv below omits these inputs.
    for key in ("TEST_EXECUTOR", "TEST_EXTRA_ARGS", "TEST_FILTER", "TEST_DISCOVERY_EXTRA_ARGS"):
        if fields.get(key, ""):
            raise ProbeError(f"{path}: {key} is not empty; probe would measure a different command")
    # The module this include runs decides the argv: CMake 4.x adds --gtest_output=json:<file>,
    # CMake 3.28/3.31 pass --gtest_list_tests alone.
    fields["json_output"] = "--gtest_output=json:" in Path(module[0]).read_text(encoding="utf-8")
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
    """Monotonic time after which no new sample may start: it reserves one full cap."""
    limit = time.monotonic() + args.max_minutes * 60
    if args.deadline:
        limit = min(limit, time.monotonic() + datetime.fromisoformat(args.deadline).timestamp() - time.time())
    return limit - args.cap_seconds


def _open_output(args: argparse.Namespace):
    # Exclusive create: one run per file, never pooled with an earlier start.
    return args.output.open("x", encoding="utf-8") if args.output else None


def _tools() -> dict[str, str | None]:
    found = {}
    for tool in ("cmake", "ctest"):
        path = shutil.which(tool)
        version = subprocess.run([path, "--version"], text=True, capture_output=True,
                                 timeout=30).stdout.splitlines()[:1] if path else []
        found[tool] = f"{path} {version[0] if version else ''}".strip() if path else None
    return found


def _die_with(parent: int):
    def bind() -> None:
        # The cap lives in this probe; if the probe is killed, the child must not outlive it.
        # Checking the parent after registering closes the fork-to-prctl window.
        if ctypes.CDLL(None, use_errno=True).prctl(1, signal.SIGKILL) != 0 or os.getppid() != parent:
            os._exit(127)  # PR_SET_PDEATHSIG failed, or the probe already died
    return bind


def _run_bounded(command: list[str], cap: float, **kwargs) -> tuple[bytes, int | None, bool, float]:
    """Run in its own session; on timeout kill the whole group so no lister survives."""
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, start_new_session=True,
                               preexec_fn=_die_with(os.getpid()), **kwargs)
    try:
        stdout, _ = process.communicate(timeout=cap)
        timed_out = False
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, 9)
        stdout, _ = process.communicate()
        timed_out = True
    return stdout, process.returncode, timed_out, time.monotonic() - start


def time_listing(target: str, fields: dict[str, str | bool], cap: float) -> dict[str, object]:
    # Same argv and cwd as the include's GoogleTestAddTests.cmake (only the JSON file name
    # differs); stdout captured, stderr inherited (CMake passes it through).
    json_out = Path(fields["TEST_WORKING_DIR"]) / f"gtest_discovery_probe_{target}.json"
    json_out.unlink(missing_ok=True)
    command = [fields["TEST_EXECUTABLE"], "--gtest_list_tests"]
    if fields["json_output"]:
        command.append(f"--gtest_output=json:{json_out}")
    stdout, status, timed_out, wall = _run_bounded(command, cap, cwd=fields["TEST_WORKING_DIR"])
    written = json_out.exists()
    json_out.unlink(missing_ok=True)
    lines = stdout.decode(errors="replace").splitlines()
    cases = sum(1 for line in lines if line.startswith("  ") and line.strip())
    return {"wall_seconds": round(wall, 6), "exit_status": status, "timed_out": timed_out,
            "listed_cases": cases, "listing_sha256": hashlib.sha256(stdout).hexdigest(),
            "json_written": written}


def _executable_identity(fields: dict[str, dict[str, str]]) -> dict[str, list[int]]:
    return {t: [Path(f["TEST_EXECUTABLE"]).stat().st_size, Path(f["TEST_EXECUTABLE"]).stat().st_mtime_ns]
            for t, f in fields.items()}


def cmd_direct(args: argparse.Namespace) -> int:
    includes = {target: target_include(args.tests_dir, target) for target in args.target}
    calls = {target: discovery_call(path) for target, path in includes.items()}
    fields = {target: parse_include(path) for target, path in calls.items()}
    output = _open_output(args)
    limit = _deadline(args)
    emit({"event": "start", "schema": SCHEMA, "arm": args.arm, "argv": sys.argv,
          "host": _host_identity(1), "targets": args.target, "tools": _tools(),
          "include_sha256": {t: hashlib.sha256(p.read_bytes()).hexdigest() for t, p in calls.items()},
          "executable_size_mtime_ns": _executable_identity(fields)}, output)
    stop = "completed"
    for repetition in range(args.repetitions):
        if repetition:  # spread repetitions over time instead of one burst
            time.sleep(max(0.0, min(paced - time.monotonic(), limit - time.monotonic())))
        paced = time.monotonic() + args.pace_seconds
        shift = repetition % len(args.target)
        for target in args.target[shift:] + args.target[:shift]:
            exe = Path(fields[target]["TEST_EXECUTABLE"])
            for condition in ("cold", "warm"):
                before = wait_for_regime(args, limit, output)
                if before is None:
                    stop = "deadline"
                    break
                if condition == "cold":
                    evict(exe)
                resident = residency(exe)
                if time.monotonic() >= limit:
                    stop = "deadline"
                    break
                result = time_listing(target, fields[target], args.cap_seconds)
                ctest_file = Path(fields[target]["CTEST_FILE"])
                emit({"event": "sample", "arm": args.arm, "repetition": repetition, "target": target,
                      "condition": condition, "residency_bytes": resident, **result, **before,
                      "mtime_ns": mtimes(exe, includes[target], ctest_file),
                      "executable_mtime_ns": exe.stat().st_mtime_ns,
                      "wall_clock": time.time()}, output)
            if stop != "completed":
                break
        if stop != "completed":
            break
    emit({"event": "stop", "reason": stop}, output)
    return 0


def tests_files(tests_dir: Path) -> dict[str, int]:
    return {generated_target(path): path.stat().st_mtime_ns for path in tests_dir.glob("*_tests.cmake")}


def wait_for_regime(args: argparse.Namespace, limit: float, output) -> dict | None:
    """Telemetry once the regime holds (always with --wait-idle off); None at the deadline."""
    while time.monotonic() < limit:
        before = telemetry()
        if not args.wait_idle or idle(before, args.idle_max_load):
            return before
        emit({"event": "wait_idle", "load": before["load"], "processes": before["processes"]}, output)
        time.sleep(min(args.poll_seconds, max(0.0, limit - time.monotonic())))
    return None


def cmd_ctest(args: argparse.Namespace) -> int:
    if not args.command:
        raise ProbeError("ctest mode needs a command after --")
    output = _open_output(args)
    limit = _deadline(args)
    emit({"event": "start", "schema": SCHEMA, "arm": args.arm, "argv": sys.argv, "tools": _tools()}, output)
    stop = "completed"
    for repetition in range(args.repetitions):
        if repetition:
            time.sleep(max(0.0, min(args.pace_seconds, limit - time.monotonic())))
        if time.monotonic() >= limit or not ctest_once(args, repetition, output, limit):
            stop = "deadline"
            break
    emit({"event": "stop", "reason": stop}, output)
    return 0


def ctest_once(args: argparse.Namespace, repetition: int, output, limit: float) -> bool:
    # Wait for the regime first, so eviction and residency describe the moment of launch.
    if wait_for_regime(args, limit, output) is None:
        return False
    before_files = tests_files(args.tests_dir)
    resident = {}
    for target in args.force:
        include = target_include(args.tests_dir, target)
        os.utime(include)  # the include's own mtime gates rediscovery on every layout
        exe = Path(parse_include(discovery_call(include))["TEST_EXECUTABLE"])
        if args.evict:
            evict(exe)
        resident[target] = residency(exe)
    before = telemetry()
    if time.monotonic() >= limit:
        return False
    raw, status, timed_out, wall = _run_bounded(args.command, args.cap_seconds, stderr=subprocess.STDOUT)
    text = raw.decode(errors="replace")
    after_files = tests_files(args.tests_dir)
    total = re.search(r"Total Tests: (\d+)", text)
    emit({"event": "ctest", "schema": SCHEMA, "arm": args.arm, "repetition": repetition,
          "command": args.command, "forced": args.force, "evicted": args.evict,
          "wall_seconds": round(wall, 6), "exit_status": status, "timed_out": timed_out,
          "enumerated": sorted(t for t, m in after_files.items() if before_files.get(t) != m),
          # CMake 4.x deletes _tests.cmake before listing; a failed listing leaves it absent.
          "removed": sorted(t for t in before_files if t not in after_files),
          "residency_bytes": resident,
          "total_tests": int(total.group(1)) if total else None,
          "discovery_timeout_seen": "Process terminated due to timeout" in text,
          "output_tail": text[-2000:], **before, "wall_clock": time.time()}, output)
    return True


# --- BUG-091 decision rule (v3 frozen with the pre-flight packet; v4 after review) ---
# shortcut: one-off BUG-091 analysis (decide/report); delete it when BUG-091 retires, together
# with DecisionRuleTests and test_planned_sequence_does_not_trip_identity_checks.
LADDER = (15, 20, 30, 45, 60)
CURRENT_BUDGET = 5.0
MIN_CELL_SAMPLES = 300
MAX_INVALID_SHARE = 0.05
NULL_PROBE_MAX_MEDIAN = 0.5


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
    if sample["arm"] == "contention" and sample["processes"]["compile"] == 0:
        return "no-compile"
    return None


def summarize(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"n": 0}
    return {"n": len(values), "median": round(statistics.median(values), 6), "p95": rank(values, 0.95),
            "p99": rank(values, 0.99), "max": max(values),
            "at_or_above_5s": sum(v >= CURRENT_BUDGET for v in values)}


def _identity_kills(samples: list[dict], real: list[dict], starts: list[dict],
                    baseline: dict[str, str]) -> list[str]:
    kills = []
    keys = [(s["arm"], s["repetition"], s["target"], s["condition"]) for s in samples]
    keys += [("ctest", r["arm"], r["repetition"]) for r in real]
    if len(keys) != len(set(keys)):
        kills.append("duplicate sample or ctest records (pooled starts)")
    if len({json.dumps(r.get("tools"), sort_keys=True) for r in starts}) > 1:
        kills.append("cmake/ctest identity differs between starts")
    for target in sorted({s["target"] for s in samples}):
        own = [s for s in samples if s["target"] == target]
        hashes = {s["listing_sha256"] for s in own}
        if len(hashes) != 1:
            kills.append(f"listing hash changed for {target}")
        elif hashes != {baseline.get(target)}:
            kills.append(f"listing differs from the validated baseline: {target}")
        if not all(s["listed_cases"] > 0 for s in own):
            kills.append(f"empty listing: {target}")
        # Include mtimes change by design when C-real forces rediscovery; content is
        # compared through the start records' include hashes instead.
        if len({s.get("executable_mtime_ns") for s in own} - {None}) != 1 or any(
                "executable_mtime_ns" not in s for s in own):
            kills.append(f"executable changed during the run: {target}")
        if len({r["include_sha256"][target] for r in starts if target in r.get("include_sha256", {})}) > 1:
            kills.append(f"include content differs between starts: {target}")
    return kills


def _real_run_status(run: dict, args: argparse.Namespace, expected_total: int | None) -> str:
    """valid | timeout | invalid:<reason> | kill:<reason> for one forced-stale CTest run."""
    # Failure kinds first, so a failure is never hidden behind a regime exclusion. A discovery
    # timeout is H1 evidence in any regime (see evaluate) but counts toward coverage only when
    # the run qualifies.
    if run.get("timed_out"):
        return "kill:capped"
    if run["exit_status"] != 0 and not run["discovery_timeout_seen"]:
        return "kill:failed without a discovery timeout"
    # Coverage needs the planned forced, evicted discovery of every target (rule v4).
    if sorted(run["forced"]) != sorted(args.targets) or run.get("evicted") is not True:
        return "kill:not forced and evicted as planned"
    if run["arm"] == "idle" and not idle(run, args.idle_max_load):
        return "invalid:not-idle"
    if run["arm"] == "contention" and run["processes"]["compile"] == 0:
        return "invalid:no-compile"
    residency = run.get("residency_bytes", {})
    if set(residency) != set(run["forced"]) or any(v != 0 for v in residency.values()):
        return "invalid:residency"
    if run["discovery_timeout_seen"]:
        return "timeout"
    if not set(run["forced"]) <= set(run["enumerated"]):
        return "kill:forced target not relisted"
    if run["total_tests"] is None or run["total_tests"] != expected_total:
        return f"kill:selected-test count {run['total_tests']} != {expected_total}"
    return "valid"


def evaluate(records: list[dict], args: argparse.Namespace) -> dict[str, object]:
    samples = [r for r in records if r.get("event") == "sample"]
    real = [r for r in records if r.get("event") == "ctest"]
    starts = [r for r in records if r.get("event") == "start"]
    baseline = {r["target"]: r["listing_sha256"] for r in load_records([args.baseline])
                if r.get("event") == "sample"}
    kills = _identity_kills(samples, real, starts, baseline)
    hash_ok = not any("listing" in kill for kill in kills)
    cells: dict[str, dict] = {}
    for s in samples:
        if s["exit_status"] != 0 or s["timed_out"]:
            kills.append(f"nonzero or capped sample: {s['arm']}/{s['condition']}/{s['target']} "
                         f"rep {s['repetition']} exit={s['exit_status']} timed_out={s['timed_out']}")
        cell = cells.setdefault(f"{s['arm']}/{s['condition']}/{s['target']}",
                                {"valid": [], "invalid": {}, "loads": []})
        cell["loads"].append(s["load"].get("load_1m", 0.0))
        reason = invalid_reason(s, args.idle_max_load)
        if reason:
            cell["invalid"][reason] = cell["invalid"].get(reason, 0) + 1
        else:
            cell["valid"].append(s["wall_seconds"])
    arms: dict[str, dict] = {}
    for name, cell in cells.items():
        arm = name.split("/", 1)[0]
        total = len(cell["valid"]) + sum(cell["invalid"].values())
        cell["collected"] = (len(cell["valid"]) >= MIN_CELL_SAMPLES
                             and sum(cell["invalid"].values()) <= MAX_INVALID_SHARE * total)
        summary = arms.setdefault(arm, {"cells": 0, "collected": 0, "loads": []})
        summary["cells"] += 1
        summary["collected"] += cell["collected"]
        summary["loads"] += cell["loads"]
    statuses = [_real_run_status(r, args, args.expected_ctest_total) for r in real]
    kills += sorted({f"C-real {s[5:]}: {r['arm']}" for r, s in zip(real, statuses) if s.startswith("kill:")})
    gates = dict(gate.split("=", 1) for gate in args.post_arm_gates)
    for arm, summary in arms.items():
        summary["median_load_1m"] = statistics.median(summary.pop("loads"))
        own = [s for r, s in zip(real, statuses) if r["arm"] == arm]
        summary["ctest_runs"] = {status: own.count(status) for status in sorted(set(own))}
        summary["post_arm_gate"] = gates.get(arm, "missing")
        summary["valid"] = (summary["cells"] == len(args.targets) * 2 and summary["collected"] == summary["cells"]
                            and sum(s in ("valid", "timeout") for s in own) >= args.min_ctest_runs
                            and summary["post_arm_gate"] == "pass")
        if arm == "contention":
            summary["valid"] = summary["valid"] and summary["median_load_1m"] >= args.contention_min_load
    kills += [f"post-arm manifest gate {gates.get(arm, 'missing')}: {arm}"
              for arm in args.required_arms if gates.get(arm) != "pass"]
    null = cells.get(f"idle/warm/{args.ecs_target}")
    if null and null["valid"] and statistics.median(null["valid"]) > NULL_PROBE_MAX_MEDIAN:
        kills.append(f"null probe: idle warm {args.ecs_target} median > {NULL_PROBE_MAX_MEDIAN} s")
    missing = [arm for arm in args.required_arms if not arms.get(arm, {}).get("valid")]
    kills += [f"required arm invalid or not collected: {arm}" for arm in missing]
    valid_walls = [w for c in cells.values() for w in c["valid"]]
    m_obs = max(valid_walls) if valid_walls else None
    real_timeouts = [r for r in real if r["discovery_timeout_seen"] and not r.get("timed_out")]
    if real_timeouts and (m_obs is None or m_obs < CURRENT_BUDGET):
        kills.append("C-real discovery timeout not reproduced by any direct sample: diagnose")
    if (m_obs is not None and m_obs >= CURRENT_BUDGET) or real_timeouts:
        h1 = "supported"
    elif not kills and m_obs is not None and m_obs < CURRENT_BUDGET / 2:
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
    valid_arms = {arm for arm, summary in arms.items() if summary["valid"]}
    return {"schema": SCHEMA, "rule": "bug-091-budget/v4", "m_obs_seconds": m_obs, "h1": h1,
            "kill_conditions": kills, "chosen_budget_seconds": chosen,
            "decision": (f"set DISCOVERY_TIMEOUT {chosen}" if chosen is not None else
                         "no value chosen: keep explicit 5 s; operator decision"),
            "arms": arms,
            "cells": {name: {**summarize(c["valid"]), "invalid": c["invalid"], "collected": c["collected"]}
                      for name, c in sorted(cells.items())},
            "hypotheses": {"H3": h3(cells, args.targets, {"contention", "idle"} <= valid_arms and hash_ok),
                           "H4": h4(cells, args.targets, args.ecs_target, "idle" in valid_arms)},
            "ctest_runs": [{**{k: r.get(k) for k in ("arm", "repetition", "exit_status", "timed_out", "wall_seconds",
                                                      "enumerated", "removed", "total_tests",
                                                      "discovery_timeout_seen")}, "status": s}
                           for r, s in zip(real, statuses)]}


def h3(cells: dict, targets: list[str], arms_valid: bool) -> dict[str, object]:
    verdict = {}
    for condition in ("cold", "warm"):
        ratios = {}
        for target in targets if arms_valid else ():
            busy, quiet = cells.get(f"contention/{condition}/{target}"), cells.get(f"idle/{condition}/{target}")
            if busy and quiet and busy["collected"] and quiet["collected"]:
                ratios[target] = round(rank(busy["valid"], 0.99) / rank(quiet["valid"], 0.99), 3)
        verdict[condition] = {"p99_ratio": ratios, "supported": sum(r >= 2 for r in ratios.values()) >= 2,
                              "assessed": len(ratios) == len(targets)}
    return verdict


def h4(cells: dict, targets: list[str], ecs: str, idle_valid: bool) -> dict[str, object]:
    verdict = {}
    for condition in ("cold", "warm"):
        outliers = {}
        for target in targets if idle_valid else ():
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
    for arm in sorted({r["arm"] for r in samples}):
        marks.append((min(r["wall_clock"] for r in samples if r["arm"] == arm), f"direct {arm}"))
    for arm in sorted({r["arm"] for r in real}):
        marks.append((min(r["wall_clock"] for r in real if r["arm"] == arm), f"C-real {arm}"))
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
    html.append(_svg_scatter("CPU pressure, PSI some avg10 % (at sample start)",
                             {"psi_cpu": [(s["wall_clock"], s["pressure"]["cpu"] or 0.0) for s in stamped
                                          if "pressure" in s]}, t0, t1, False, [], marks))
    html.append(_svg_scatter("IO pressure, PSI some avg10 % (at sample start)",
                             {"psi_io": [(s["wall_clock"], s["pressure"]["io"] or 0.0) for s in stamped
                                         if "pressure" in s]}, t0, t1, False, [], marks))
    html.append(_svg_scatter("Concurrent compile processes (at sample start)",
                             {"compile": [(s["wall_clock"], float(s["processes"]["compile"])) for s in stamped]},
                             t0, t1, False, [], marks))
    html.append("<h2>Forced-stale CTest runs</h2><table><tr><th>arm</th><th>exit</th><th>wall s</th>"
                "<th>enumerated</th><th>total</th><th>timeout seen</th><th>status</th></tr>")
    for run in verdict["ctest_runs"]:
        html.append(f"<tr><td>{run['arm']}</td><td>{run['exit_status']}</td><td>{run['wall_seconds']}</td>"
                    f"<td>{len(run['enumerated'])}</td><td>{run['total_tests']}</td>"
                    f"<td>{run['discovery_timeout_seen']}</td><td>{run['status']}</td></tr>")
    html.append("</table></body>")
    args.html.write_text("".join(html), encoding="utf-8")
    print(json.dumps({"html": str(args.html), "decision": verdict["decision"]}))
    return 0


def _gate(value: str) -> str:
    if not re.fullmatch(r"[a-z]+=(pass|fail)", value):
        raise argparse.ArgumentTypeError(f"expected ARM=pass|fail, got {value!r}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode", required=True)
    for name in ("direct", "ctest"):
        mode = sub.add_parser(name)
        mode.add_argument("--tests-dir", type=Path, required=True, help="directory of *_include.cmake")
        mode.add_argument("--arm", default="unlabelled")
        mode.add_argument("--output", type=Path, help="append JSON lines here as well as stdout")
        mode.add_argument("--cap-seconds", type=float, default=120.0)
        mode.add_argument("--repetitions", type=int, default=1)
        mode.add_argument("--wait-idle", action="store_true")
        mode.add_argument("--idle-max-load", type=float, default=2.0)
        mode.add_argument("--poll-seconds", type=float, default=120.0)
        mode.add_argument("--max-minutes", type=float, default=35.0)
        mode.add_argument("--deadline", help="ISO time with offset; no sample starts unless it can end before it")
        mode.add_argument("--pace-seconds", type=float, default=0.0, help="minimum spacing of repetitions")
    direct = sub.choices["direct"]
    direct.add_argument("--target", action="append", required=True)
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
        mode.add_argument("--min-ctest-runs", type=int, default=20)
        mode.add_argument("--expected-ctest-total", type=int, required=True,
                          help="selected-test count of a valid C-real run")
        mode.add_argument("--baseline", type=Path, required=True,
                          help="validated direct run whose listing hashes bind identity")
        mode.add_argument("--post-arm-gates", nargs="*", default=[], metavar="ARM=pass|fail", type=_gate)
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
