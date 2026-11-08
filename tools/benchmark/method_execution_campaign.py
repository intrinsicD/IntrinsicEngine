#!/usr/bin/env python3
"""Freeze and execute ARCH-024's fixed method campaign; never infer experiment approval."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
SEEDS = (11, 23, 37, 53, 71)
GLOBAL_SECONDS = 1800
BINDINGS = (
    "tests/benchmark/Test.MethodExecutionMeasurement.cpp",
    "tests/CMakeLists.txt",
    "tools/benchmark/method_execution_campaign.py",
    "tools/benchmark/method_execution_report.py",
    "benchmarks/rendering/manifests/runtime_method_execution_kmeans.yaml",
    "benchmarks/rendering/manifests/runtime_method_execution_queries.yaml",
    "benchmarks/rendering/manifests/runtime_method_execution_controls.yaml",
)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def file_digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def read(path):
    return json.loads(Path(path).read_text())


def new_outcomes(packet):
    return {"packet_sha256": digest(packet), "execution_id": uuid.uuid4().hex, "cells": []}


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()


def cells():
    result = []

    def add(mode, points, seed, queue="separate", preview=0, buffers=3, strategy="lbvh"):
        identifier = f"{mode}-{points}-{seed}"
        if mode in {"kmeans", "controls"}:
            identifier += f"-{queue}-p{preview}-b{buffers}"
        elif mode == "queries":
            identifier += f"-{strategy}"
        result.append({"id": identifier, "mode": mode, "points": points, "seed": seed,
                       "queue": queue, "preview_every": preview, "preview_buffers": buffers,
                       "strategy": strategy, "timeout_seconds": 10 if mode == "queries" else 20})

    for points in (100000, 1000000):
        for seed in SEEDS:
            add("reference", points, seed)
    arms = [(queue, preview, buffers) for queue in ("graphics", "separate")
            for preview, buffers in ((0, 3), (1, 2), (1, 3))]
    for index, seed in enumerate(SEEDS):
        rotated = arms[index:] + arms[:index]
        sizes = (100000, 1000000) if index % 2 == 0 else (1000000, 100000)
        for points in sizes:
            for queue, preview, buffers in rotated:
                add("kmeans", points, seed, queue, preview, buffers)
    for index, seed in enumerate(SEEDS):
        for points in (4096, 32768):
            for strategy in (("brute_force", "lbvh") if index % 2 == 0 else ("lbvh", "brute_force")):
                add("queries", points, seed, strategy=strategy)
    for points in (100000, 1000000):
        for queue in ("graphics", "separate"):
            add("controls", points, 11, queue, 1, 3)
    return result


def live_source(executable, technical):
    status = git("status", "--porcelain", "--untracked-files=normal")
    if status and not technical:
        raise ValueError("A measurement packet requires a clean committed source tree")
    return {"revision": git("rev-parse", "HEAD"), "state": "dirty_worktree" if status else "clean_commit",
            "status_sha256": hashlib.sha256(status.encode()).hexdigest(),
            "files": {name: file_digest(ROOT / name) for name in BINDINGS},
            "executable": str(executable), "executable_sha256": file_digest(executable)}


def validate_packet(packet):
    if packet.get("schema") != "intrinsic.method_execution.packet.v1" or packet.get("cells") != cells():
        raise ValueError("Packet schema/cell plan differs from the fixed campaign")
    if packet.get("global_seconds") != GLOBAL_SECONDS or packet.get("report_seconds") != 60:
        raise ValueError("Packet deadlines differ from the reviewed design")
    if packet["source"] != live_source(Path(packet["source"]["executable"]), packet["technical_only"]):
        raise ValueError("Live source, executable, status or bindings changed after freeze")


def check_reviews(packet, reviews):
    if packet["technical_only"] or packet["source"]["state"] != "clean_commit":
        raise ValueError("Technical/dirty packets are never measurement eligible")
    expected = digest(packet)
    answers = reviews.get("reviews", [])
    if len(answers) != 2 or {r.get("provider") for r in answers} != {"claude", "codex"}:
        raise ValueError("Two independent Claude and Codex experiment reviews are required")
    if len({r.get("session") for r in answers}) != 2:
        raise ValueError("Reviewer sessions must be independent")
    for answer in answers:
        if answer.get("verdict") != "PASS" or answer.get("packet_sha256") != expected or not answer.get("session"):
            raise ValueError("Missing PASS verdict bound to this exact packet")
        if answer.get("author", True):
            raise ValueError("An author cannot supply the independent experiment review")
        transcript = Path(answer["transcript"])
        if file_digest(transcript) != answer.get("transcript_sha256"):
            raise ValueError("Review transcript changed or is absent")


def check_technical(packet, receipt):
    required = {cell["id"] for cell in packet["cells"] if cell["seed"] == SEEDS[0]}
    if receipt.get("packet_sha256") != digest(packet) or not required <= set(receipt.get("passed_cells", [])):
        raise ValueError("Final-configuration technical checks are incomplete")
    # These existing deterministic tests cover pending-device and no-render lifetime seams that timing cannot prove.
    required_contracts = {"device_completion_not_submission", "preview_depth_2_and_3", "minimized_init_preview_accept"}
    checks = receipt.get("contract_checks", {})
    if not isinstance(checks, dict) or not required_contracts <= set(checks):
        raise ValueError("Missing deterministic device-completion/preview/minimized contract checks")
    for name in required_contracts:
        check = checks[name]
        if check.get("exit_code") != 0 or not check.get("command") or check.get("source_revision") != packet["source"]["revision"]:
            raise ValueError("Contract test evidence is missing, failed or from another revision")
        if file_digest(check["output"]) != check.get("output_sha256"):
            raise ValueError("Contract test output changed or is absent")


def bounded(command, env, timeout, stdout, stderr):
    with stdout.open("wb") as out, stderr.open("wb") as err:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=out, stderr=err, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise TimeoutError(f"Process exceeded {timeout:g} seconds") from None


def execute_cell(packet, cell, directory, deadline):
    validate_packet(packet)  # before every process, not just at campaign entry
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("Global work deadline reached")
    config = dict(cell)
    if cell["mode"] != "reference" and cell["mode"] != "queries":
        config["reference"] = str(directory / "refs" / f"reference-{cell['points']}-{cell['seed']}.json")
        if not Path(config["reference"]).is_file():
            raise ValueError("Planned CPU reference is missing")
    output = directory / ("refs" if cell["mode"] == "reference" else "raw") / (cell["id"] + ".json")
    config_path = directory / "configs" / (cell["id"] + ".json")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise ValueError("Never overwrite an existing cell; use a fresh output directory")
    write(config_path, config)
    logs = directory / "logs"; logs.mkdir(exist_ok=True)
    env = dict(os.environ, INTRINSIC_METHOD_EXECUTION_CONFIG=str(config_path), INTRINSIC_METHOD_EXECUTION_OUT=str(output))
    command = [packet["source"]["executable"], "--gtest_filter=MethodExecutionMeasurement.FrozenCell"]
    result = {"id": cell["id"], "config_sha256": file_digest(config_path), "command": command,
              "output": str(output.relative_to(directory)), "status": "error"}
    try:
        code = bounded(command, env, min(float(cell["timeout_seconds"]), remaining), logs / (cell["id"] + ".out"),
                       logs / (cell["id"] + ".err"))
        result["exit_code"] = code
        payload = read(output)
        if code != 0 or payload.get("technical_passed") is not True:
            raise ValueError(payload.get("failure", "Cell did not complete its assertions"))
        result["status"] = "passed"
    except (OSError, ValueError, TimeoutError, json.JSONDecodeError) as error:
        result["failure"] = str(error)
    finally:
        if output.is_file():
            result["output_sha256"] = file_digest(output)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare")
    prepare.add_argument("--executable", type=Path, required=True)
    prepare.add_argument("--packet", type=Path, required=True)
    prepare.add_argument("--technical-only", action="store_true")
    prepare.add_argument("--environment", type=Path, required=True,
                         help="Explicit device/driver/display/build metadata; never an environment dump")
    for name in ("technical", "run"):
        sub = commands.add_parser(name)
        sub.add_argument("--packet", type=Path, required=True)
        sub.add_argument("--output", type=Path, required=True)
        if name == "run":
            sub.add_argument("--reviews", type=Path, required=True)
            sub.add_argument("--technical-receipt", type=Path, required=True)
        else:
            sub.add_argument("--cells", nargs="*", help="Defaults to first seed of every frozen shape, plus references")
    args = parser.parse_args(argv)
    if args.command == "prepare":
        packet = {"schema": "intrinsic.method_execution.packet.v1", "global_seconds": GLOBAL_SECONDS,
                  "report_seconds": 60, "technical_only": args.technical_only, "cells": cells(),
                  "source": live_source(args.executable.resolve(), args.technical_only),
                  "environment": read(args.environment), "environment_sha256": file_digest(args.environment),
                  "questions": "tasks/active/ARCH-024-plugin-execution-completion-and-performance.md",
                  "hypothesis": "Queue separation and loose previews reduce contention without changing results",
                  "null_prediction": "No completed work has zero throughput; corrupted/empty outputs fail quality",
                  "kill_condition": "source/config mismatch, wrong queue, validation/quality error, process/global timeout",
                  "scope": "warmed resident runs; five paired seeds, three within-process repeats; no hardware-overlap claim",
                  "manifest_ids": ["runtime.method_execution." + x for x in ("kmeans", "queries", "controls")]}
        if args.packet.exists():
            raise ValueError("Refusing to replace a frozen packet")
        write(args.packet, packet)
        print(digest(packet))
        return 0
    packet = read(args.packet); validate_packet(packet)
    if args.output.exists():
        raise ValueError("Use a new output directory; existing evidence is never replaced")
    if args.command == "run":
        check_reviews(packet, read(args.reviews)); check_technical(packet, read(args.technical_receipt))
        if packet["environment"].get("exclusive_gpu") is not True:
            raise ValueError("Frozen environment must explicitly confirm exclusive GPU use")
        selected = packet["cells"]
    else:
        wanted = set(args.cells or [c["id"] for c in packet["cells"] if c["seed"] == SEEDS[0]])
        known = {c["id"] for c in packet["cells"]}
        if not wanted <= known:
            raise ValueError("Unknown technical cell")
        for cell in packet["cells"]:
            if cell["id"] in wanted and cell["mode"] in {"kmeans", "controls"}:
                wanted.add(f"reference-{cell['points']}-{cell['seed']}")
        selected = [c for c in packet["cells"] if c["id"] in wanted]
    directory = args.output.resolve(); directory.mkdir(parents=True)
    write(directory / "packet.json", packet)
    start = time.monotonic(); deadline = start + GLOBAL_SECONDS - packet["report_seconds"]
    ledger = new_outcomes(packet)
    outcomes = ledger["cells"]
    write(directory / "outcomes.json", ledger)
    for cell in selected:
        try:
            outcome = execute_cell(packet, cell, directory, deadline)
        except (ValueError, OSError, TimeoutError, subprocess.CalledProcessError) as error:
            outcome = {"id": cell["id"], "status": "error", "failure": str(error)}
        outcomes.append(outcome)
        write(directory / "outcomes.json", ledger)
        print(cell["id"] + ": " + outcome["status"], flush=True)
        if outcome["status"] != "passed":
            break  # technical validity stop, not selective omission or retry
    passed = [r["id"] for r in outcomes if r["status"] == "passed"]
    if args.command == "technical":
        write(directory / "technical_receipt.json", {"packet_sha256": digest(packet), "passed_cells": passed,
              "execution_id": ledger["execution_id"],
              "contract_checks": {}, "note": "Coordinator must attach actual deterministic CTest evidence; this is not measurement evidence"})
    else:
        command = [sys.executable, str(ROOT / "tools/benchmark/method_execution_report.py"), "--root", str(directory)]
        report_code = bounded(command, os.environ, min(60.0, max(0.001, start + GLOBAL_SECONDS - time.monotonic())),
                              directory / "report.out", directory / "report.err")
        if report_code:
            raise ValueError("Report/validation failed; raw outputs remain unjudged")
    return 0 if len(passed) == len(selected) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, TimeoutError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(2)
