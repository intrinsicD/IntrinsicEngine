#!/usr/bin/env python3
"""Measure serial, source-bound engine rebuilds in a disposable worktree/build tree."""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from datetime import UTC, datetime
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import yaml

import compile_hotspots as hotspots

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/benchmark"))
from seal_benchmark_results import canonicalize  # noqa: E402
from validate_benchmark_results import validate_result_data  # noqa: E402


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def command(args: list[str], cwd: Path) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True)


def dependency_snapshot(root: Path) -> dict:
    files = sorted(path for path in root.rglob("*") if path.is_symlink() or path.is_file())
    if not files:
        raise ValueError(f"Preinstalled dependencies are missing from {root}")
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        digest.update(str(path.lstat().st_mode).encode() + b"\0")
        if path.is_symlink():
            if path.is_dir():
                raise ValueError(f"Unsupported dependency directory symlink: {path}")
            digest.update(os.fsencode(path.readlink()) + b"\0")
        if not path.is_file():
            continue  # A dangling link is still part of the recorded identity.
        digest.update(str(path.stat().st_size).encode() + b"\0")
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
    return {"sha256": digest.hexdigest(), "file_count": len(files)}


def log_window(before: bytes, after: bytes) -> bytes:
    """Reject compaction instead of misattributing historical compiler invocations."""
    if not after.startswith(before):
        raise ValueError("Ninja compacted its log during the measured command")
    if not before:
        return after
    return before.splitlines(keepends=True)[0] + after[len(before):]


def critical_path(dot: str, log: bytes) -> dict:
    """Weight Ninja's dependency DAG with this invocation's unfiltered durations."""
    labels, adjacency, indegree = {}, defaultdict(set), defaultdict(int)
    for line in dot.splitlines():
        node = re.match(r'"([^"]+)" \[label=("(?:[^"\\]|\\.)*")', line)
        edge = re.match(r'"([^"]+)" -> "([^"]+)"', line)
        if node:
            labels[node[1]] = json.loads(node[2])
            indegree[node[1]] += 0
        if edge and edge[2] not in adjacency[edge[1]]:
            adjacency[edge[1]].add(edge[2])
            indegree[edge[1]] += 0
            indegree[edge[2]] += 1
    if not labels:
        raise ValueError("Empty Ninja graph")
    # Ninja prints ancillary outputs of a multi-output command without visiting
    # (or labelling) them when they are outside this target's dependency closure.
    unlabelled = set(indegree) - set(labels)
    if any(adjacency[node] for node in unlabelled):
        raise ValueError("Unlabelled internal node in Ninja graph")
    labels.update({node: f"unlabelled-output:{node}" for node in unlabelled})
    weights, siblings = {}, defaultdict(set)
    for line in log.decode().splitlines()[1:]:
        start, end, _, output, command_hash = line.split("\t")
        weights[output] = int(end) - int(start)
        siblings[(start, end, command_hash)].add(output)
    named = set(labels.values())
    unmatched = sorted(set(weights) - named)
    ancillary = {output for outputs in siblings.values() if outputs & named
                 for output in outputs - named}
    meta = {p for p in unmatched if p == "build.ninja" or "VerifyGlobs.cmake_force" in p
            or Path(p).parts[-2:] == ("CMakeFiles", "cmake.verify_globs")}
    unexpected = set(unmatched) - ancillary - meta
    if unexpected:
        raise ValueError(f"Timed commands absent from target graph: {sorted(unexpected)}")
    queue = deque(n for n, count in indegree.items() if count == 0)
    distance, previous = {}, {}
    visited = 0
    while queue:
        node = queue.popleft()
        visited += 1
        distance[node] = distance.get(node, 0) + weights.get(labels[node], 0)
        for child in adjacency[node]:
            if distance[node] > distance.get(child, -1):
                distance[child], previous[child] = distance[node], node
            indegree[child] -= 1
            if indegree[child] == 0:
                queue.append(child)
    if visited != len(labels):
        raise ValueError("Cycle in Ninja target graph")
    end = max(distance, key=distance.get)
    path = []
    node = end
    while True:
        label = labels[node]
        if label in weights:
            path.append({"output": label, "duration_ms": weights[label]})
        if node not in previous:
            break
        node = previous[node]
    return {"duration_ms": distance[end], "timed_path": list(reversed(path)),
            "timed_output_count": len(weights), "unlabelled_ancillary_outputs": len(unlabelled),
            "unmapped_ancillary_outputs": sorted(ancillary), "unmapped_meta_outputs": sorted(meta)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    source, build, output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    manifest_path = args.manifest.resolve()
    manifest = yaml.safe_load(manifest_path.read_text())
    params = manifest["params"]
    assert source != REPO and (source / ".git").is_file(), "Use a detached disposable worktree"
    assert not output.exists(), "Never overwrite a measurement population"
    assert not build.exists(), "Initial build directory must be absent"
    assert build.parent.is_dir() and build not in (source, REPO)
    output.mkdir(parents=True)
    env = dict(os.environ, CCACHE_DISABLE="1", VCPKG_FORCE_SYSTEM_BINARIES="1")
    # This runner borrows preinstalled packages; configure must never reinstall
    # them when a disposable worktree changes vcpkg's toolchain/overlay identity.
    configure = (["cmake", "--preset", params["preset"], "-B", str(build)] +
                 params["configure_options"] + ["-DVCPKG_MANIFEST_INSTALL=OFF"])
    compile_command = ["cmake", "--build", str(build), "--target", params["target"],
                       "--parallel", str(params["jobs"])]
    counters = defaultdict(int)
    dependency_digest = None
    owner = build / ".compile-measurement-owner"
    write_json(output / "protocol.json", {"manifest": manifest, "manifest_sha256":
               hashlib.sha256(manifest_path.read_bytes()).hexdigest(), "source": str(source),
               "build": str(build), "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "host": {"system": platform.system(), "release": platform.release(),
                        "machine": platform.machine(), "logical_cpus": os.cpu_count(),
                        "compiler": command([params["compiler"], "--version"], source),
                        "cmake": command(["cmake", "--version"], source),
                        "ninja": command(["ninja", "--version"], source),
                        "cpu_model": next((line.split(":", 1)[1].strip() for line in
                            Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")), "unknown")}})

    def clean_source(revision: str) -> None:
        assert command(["git", "rev-parse", "HEAD"], source).strip() == revision
        assert not command(["git", "status", "--porcelain"], source), "Source is dirty"

    def measured(label: str, argv: list[str], directory: Path, *, capture_ninja: bool = False) -> dict:
        before = (build / ".ninja_log").read_bytes() if (build / ".ninja_log").exists() else b""
        stamp = datetime.now(UTC).isoformat()
        load_before = os.getloadavg()
        started = time.perf_counter()
        with (directory / f"{label}.log").open("w") as stream:
            result = subprocess.run(["/usr/bin/time", "-q", "-f", '{"cpu_user_s":%U,"cpu_system_s":%S,"max_single_process_rss_kib":%M}',
                                     "-o", str(directory / f"{label}.time.json"), "--", *argv],
                                    cwd=source, env=env, stdout=stream, stderr=subprocess.STDOUT)
        wall = (time.perf_counter() - started) * 1000
        time_lines = (directory / f"{label}.time.json").read_text().splitlines()
        record = json.loads(time_lines[-1])
        record.update(wall_ms=wall, started_at=stamp, exit_code=result.returncode, command=argv, loadavg_before=load_before)
        write_json(directory / f"{label}.execution.json", record)
        if result.returncode:
            raise RuntimeError(f"{label} failed; preserve {directory}")
        after = (build / ".ninja_log").read_bytes() if (build / ".ninja_log").exists() else b""
        # CMake may deliberately recompact Ninja state during generation. Only
        # build invocations need an append-only window for compiler attribution.
        if capture_ninja:
            window = log_window(before, after) if after else b"# ninja log v5\n"
            (directory / f"{label}.ninja_log").write_bytes(window)
        return record

    for position, arm in enumerate(params["sample_order"], 1):
        counters[arm] += 1
        revision = params["source_revisions"][arm]
        assert not command(["git", "status", "--porcelain"], source)
        subprocess.run(["git", "checkout", "--detach", revision], cwd=source, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        clean_source(revision)
        if build.exists():
            assert owner.read_text() == str(output)
            shutil.rmtree(build)
        assert shutil.disk_usage(build.parent).free >= 8 * 1024**3, "Insufficient disposable build space"
        build.mkdir()
        owner.write_text(str(output))
        directory = output / f"{position:02d}-{arm}-{counters[arm]}"
        directory.mkdir()
        print(f"START {directory.name}", flush=True)
        # Pre-read the same input populations; this is not a cold-filesystem experiment.
        files = [source / f for f in command(["git", "ls-files"], source).splitlines()]
        dependency_root = source / "external/vcpkg-installed/ci"
        for path in files:
            if path.is_file():
                with path.open("rb") as stream:
                    while chunk := stream.read(1024 * 1024):
                        pass
        snapshot = dependency_snapshot(dependency_root)
        digest = snapshot["sha256"]
        assert dependency_digest in (None, digest), "Preinstalled dependencies changed between samples"
        dependency_digest = digest
        write_json(directory / "dependencies.json", snapshot)
        initial_configure = measured("initial-configure", configure, directory)
        assert dependency_snapshot(dependency_root) == snapshot, "Preinstalled dependencies changed during configure"
        commands = json.loads((build / "compile_commands.json").read_text())
        assert all("ccache" not in row["command"] for row in commands)
        assert all(params["compiler"] in row["command"] for row in commands if row["file"].endswith((".cpp", ".cppm")))
        shutil.copyfile(build / "CMakeCache.txt", directory / "CMakeCache.txt")
        shutil.copyfile(build / "compile_commands.json", directory / "compile_commands.json")
        resolver = hotspots.SourceResolver(source, build)
        timings, configure_times, rss, details = {}, {}, {}, {}
        configure_times["initial"] = initial_configure["wall_ms"]
        for scenario, path in params["scenarios"].items():
            subprocess.run(["ninja", "-C", str(build), "-t", "recompact"],
                           check=True, stdout=subprocess.DEVNULL)
            if path and scenario != "reconfigure":
                os.utime(source / path, None)
            if scenario == "reconfigure":
                cfg = measured("reconfigure-configure", configure, directory)
                assert dependency_snapshot(dependency_root) == snapshot, "Preinstalled dependencies changed during reconfigure"
                configure_times[scenario] = cfg["wall_ms"]
                assert (build / "compile_commands.json").read_bytes() == (directory / "compile_commands.json").read_bytes(), "Reconfigure changed compiler commands"
            clean_source(revision)
            print(f"  {scenario}", flush=True)
            record = measured(scenario, compile_command, directory, capture_ninja=True)
            assert dependency_snapshot(dependency_root) == snapshot, "Preinstalled dependencies changed during measured build"
            clean_source(revision)
            window_path = directory / f"{scenario}.ninja_log"
            rows = [hotspots.build_report_row(source, resolver, entry)
                    for entry in hotspots.parse_ninja_log(window_path, build)]
            assert not any(row["resolution"]["status"] in {"unresolved", "ambiguous", "outside-declared-roots"} for row in rows)
            if path and scenario != "reconfigure":
                expected = params.get("probe_sources", {}).get(scenario, [path])
                assert expected and set(expected) <= {row["source"] for row in rows}, \
                    "Probe did not compile its declared target sources"
            if scenario.endswith("interface"):
                fanout = {row["source"] for row in rows if row["source"] != path}
                assert len(fanout) >= params["minimum_interface_importers"], "Interface probe did not invalidate importers"
            if scenario == "noop":
                assert not rows, "No-op build was not settled"
            graph = command(["ninja", "-C", str(build), "-t", "graph", params["target"]], source)
            (directory / f"{scenario}.dot").write_text(graph)
            critical = critical_path(graph, window_path.read_bytes())
            assert critical["duration_ms"] <= record["wall_ms"] + 100
            rows.sort(key=lambda row: -row["duration_ms"])
            write_json(directory / f"{scenario}.hotspots.json", rows)
            details[scenario] = {"execution": record, "compiler_invocations": len(rows),
                                 "critical_path": critical, "top_producers": rows[:12]}
            timings[scenario] = record["wall_ms"]
            rss[scenario] = record["max_single_process_rss_kib"] * 1024
            print(f"  PASS {scenario}: {record['wall_ms'] / 1000:.3f}s, {len(rows)} compiler invocations", flush=True)
        raw = {"benchmark_id": manifest["benchmark_id"], "method": manifest["method"],
               "dataset": manifest["dataset"], "backend": "external_baseline", "commit": revision,
               "status": "passed", "metrics": {"build_time_ms": timings,
               "configure_time_ms": configure_times, "memory_peak_bytes": rss, "sample_count": 1},
               "diagnostics": {"arm": arm, "position": position, "sample": counters[arm],
                               "toolchain_backend": params["compiler"],
                               "scenarios": details, "source_clean_before_after": True}}
        write_json(directory / "raw-result.json", raw)
        sealed = canonicalize(raw, manifest_path=manifest_path, manifest=manifest,
                              manifests_root=REPO / "benchmarks", run_id=output.name,
                              attempt_id=directory.name, source_state="clean_commit",
                              source_revision=revision, claim_eligible=False,
                              snapshot_sha256=None, diff_sha256=None)
        results = output / "results"
        results.mkdir(exist_ok=True)
        destination = results / f"{directory.name}.json"
        _, errors = validate_result_data(sealed, destination,
            {manifest["benchmark_id"]: (manifest_path, manifest)}, REPO / "benchmarks")
        if errors:
            raise ValueError("; ".join(errors))
        write_json(destination, sealed)
        print(f"COMPLETE {directory.name}", flush=True)


if __name__ == "__main__":
    main()
