#!/usr/bin/env python3
"""GoogleTest PRE_TEST discovery budget: declared policy, bounded fake lister, live registrations.

Without arguments this runs hermetic checks (source policy, a private CMake
project with a controlled slow fake lister, and probe self-checks). With
``--build-dir`` it only reconciles that tree's generated discovery includes.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
CMAKELISTS = ROOT / "tests/CMakeLists.txt"
HELPER = ROOT / "cmake/IntrinsicTestDiscovery.cmake"
PROBE = ROOT / "tools/ci/gtest_discovery_probe.py"
sys.path.insert(0, str(PROBE.parent))
import gtest_discovery_probe as discovery_probe  # noqa: E402  (shared discovery-file layout)
sys.path.remove(str(PROBE.parent))
BUDGET_VARIABLE = "_intrinsic_gtest_discovery_timeout_seconds"
CASE_COUNT = 40


def _set_value(text: str, variable: str) -> int:
    match = re.search(rf"(?m)^set\({variable} (\d+)\)\s*$", text)
    if match is None:
        raise AssertionError(f"tests/CMakeLists.txt declares no integer {variable}")
    return int(match.group(1))


def declared_budget() -> int:
    text = CMAKELISTS.read_text(encoding="utf-8")
    if not re.search(rf"(?m)^set\({BUDGET_VARIABLE}\b", text):
        raise AssertionError(
            f"tests/CMakeLists.txt has no declared discovery budget ({BUDGET_VARIABLE})")
    return _set_value(text, BUDGET_VARIABLE)


def case_budgets() -> tuple[int, int]:
    text = CMAKELISTS.read_text(encoding="utf-8")
    return (_set_value(text, "_intrinsic_default_test_timeout_seconds"),
            _set_value(text, "_intrinsic_slow_test_timeout_seconds"))


def sanitizer_scale() -> int:
    return _set_value(CMAKELISTS.read_text(encoding="utf-8"), "_intrinsic_sanitizer_test_timeout_scale")


def tree_scale(build_dir: Path) -> int:
    """Case TIMEOUT factor a configured tree must carry (BUG-240: sanitized trees only)."""
    cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(r"(?m)^INTRINSIC_SANITIZER_IDENTITY:INTERNAL=(.*)$", cache)
    if match is None:
        raise AssertionError(f"{build_dir / 'CMakeCache.txt'} has no INTRINSIC_SANITIZER_IDENTITY")
    return 1 if match.group(1) == "none" else sanitizer_scale()


def lock_timeout() -> int:
    match = re.search(r"GUARD PROCESS TIMEOUT (\d+)", HELPER.read_text(encoding="utf-8"))
    if match is None:
        raise AssertionError("discovery lock timeout not found in IntrinsicTestDiscovery.cmake")
    return int(match.group(1))


def lister_survives_timeout(version: tuple[int, ...]) -> bool:
    """CTest returns at the budget but leaves the lister running (BUG-252).

    CMake 3.29 moved execute_process to cmUVProcessChain, which stopped terminating
    the child on TIMEOUT; upstream #27378 restored it in 3.31.10, 4.0.5 and 4.1.3.
    """
    return (3, 29) <= version < (3, 31, 10) or (4, 0) <= version < (4, 0, 5) or (4, 1) <= version < (4, 1, 3)


def ctest_version() -> tuple[int, ...]:
    first = run("ctest", "--version").stdout.splitlines()[0]
    return tuple(int(part) for part in re.search(r"(\d+)\.(\d+)\.(\d+)", first).groups())


def run(*args: str, timeout: float = 60, **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=timeout, **kwargs)


def parse_include(path: Path) -> dict[str, str]:
    """Fields of the gtest_discover_tests_impl call in ``path`` (see discovery_probe.discovery_call)."""
    text = path.read_text(encoding="utf-8")
    fields = discovery_probe.call_fields(text)
    if text.count("gtest_discover_tests_impl(") != 1 or "TEST_DISCOVERY_TIMEOUT" not in fields:
        raise AssertionError(f"{path.name}: unexpected GoogleTest include layout")
    return fields


def case_timeout(properties: str) -> int:
    values = properties.split(";")
    if values.count("TIMEOUT") != 1:
        raise AssertionError(f"expected one TIMEOUT case property, got {properties!r}")
    return int(values[values.index("TIMEOUT") + 1])


class SourcePolicyTests(unittest.TestCase):
    def test_discovery_budget_is_declared_separately_from_case_budgets(self) -> None:
        budget = declared_budget()
        self.assertGreater(budget, 0)
        # One hung lister must fail with its own diagnostic before a concurrent
        # reader's 90 s registry-lock wait expires. Several slow stale listings
        # in one CTest call add up and are not bounded by this check.
        self.assertLess(budget, lock_timeout())
        text = CMAKELISTS.read_text(encoding="utf-8")
        call = re.search(r"gtest_discover_tests\(\$\{name\}(?P<body>.*?)DISCOVERY_MODE PRE_TEST\s*\)",
                         text, flags=re.DOTALL)
        self.assertIsNotNone(call, "intrinsic_test_executable() discovery call not found")
        assert call is not None
        tokens = call.group("body").split()
        self.assertEqual(tokens.count("DISCOVERY_TIMEOUT"), 1, tokens)
        self.assertEqual(tokens[tokens.index("DISCOVERY_TIMEOUT") + 1], f"${{{BUDGET_VARIABLE}}}")
        self.assertIn("PROPERTIES", tokens)
        properties = tokens[tokens.index("PROPERTIES") + 1:]
        self.assertEqual(properties[:2], ["TIMEOUT", "${_intrinsic_test_timeout}"])
        self.assertNotIn("DISCOVERY_TIMEOUT", properties, "discovery budget must not be a case property")

    def test_sanitizer_lanes_scale_case_budgets_by_the_operator_factor(self) -> None:
        self.assertEqual(sanitizer_scale(), 2, "BUG-240 operator decision: factor 2")


class FakeListerProject:
    """Private CMake project whose PRE_TEST listers are controlled Python fakes."""

    def __init__(self, root: Path, budget: int, targets: tuple[str, ...] = ("Fake",)) -> None:
        self.root = root
        self.source = root / "source"
        self.build = root / "build"
        self.source.mkdir()
        self.fakes = {}
        lines = ["cmake_minimum_required(VERSION 3.28)", "project(DiscoveryBudget NONE)",
                 "enable_testing()", "include(GoogleTest)"]
        for target in targets:
            fake = self.source / f"{target}.py"
            fake.write_text(
                f"#!{sys.executable}\nimport json, os, pathlib, sys, time\n"
                f'argv_file = os.environ.get("FAKE_ARGV_FILE_{target}")\n'
                "if argv_file:\n"
                "    with open(argv_file, 'a') as log: log.write(json.dumps([os.getcwd(), *sys.argv[1:]]) + '\\n')\n"
                'if "--gtest_list_tests" in sys.argv:\n'
                f'    pid_file = os.environ.get("FAKE_PID_FILE_{target}")\n'
                "    if pid_file: pathlib.Path(pid_file).write_text(str(os.getpid()))\n"
                f'    time.sleep(float(os.environ.get("FAKE_DELAY_{target}", "0")))\n'
                f'    print("{target}.")\n'
                f'    for i in range({CASE_COUNT}): print("  Case" + str(i))\n')
            fake.chmod(0o755)
            self.fakes[target] = fake
            lines += [f"add_executable({target} IMPORTED GLOBAL)",
                      f'set_target_properties({target} PROPERTIES IMPORTED_LOCATION "{fake}")',
                      # Same argument order as intrinsic_test_executable().
                      f"gtest_discover_tests({target} DISCOVERY_TIMEOUT {budget} "
                      "PROPERTIES TIMEOUT 30 DISCOVERY_MODE PRE_TEST)"]
        lines += [f'include("{HELPER}")', "intrinsic_serialize_test_discovery()"]
        (self.source / "CMakeLists.txt").write_text("\n".join(lines) + "\n")
        run("cmake", "-S", str(self.source), "-B", str(self.build))

    def include(self, target: str) -> Path:
        """The file holding the target's discovery call, on this CMake's layout."""
        try:
            return discovery_probe.discovery_call(discovery_probe.target_include(self.build, target))
        except discovery_probe.ProbeError as error:
            version = ".".join(map(str, ctest_version()))
            raise AssertionError(f"CMake {version}: {error}") from error

    def stale(self, target: str) -> None:
        Path(parse_include(self.include(target))["CTEST_FILE"]).unlink(missing_ok=True)

    def ctest(self, *args: str, env: dict[str, str], timeout: float) -> tuple[subprocess.CompletedProcess[str], float]:
        start = time.monotonic()
        result = subprocess.run(["ctest", "--test-dir", str(self.build), *args], text=True,
                                capture_output=True, timeout=timeout, env={**os.environ, **env})
        return result, time.monotonic() - start


def _alive(pid: int) -> bool:
    stat = Path(f"/proc/{pid}/stat")
    try:
        return stat.read_text().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):  # vanished between open and read
        return False


class FakeListerBudgetTests(unittest.TestCase):
    def test_orphaning_ctest_range_matches_the_upstream_fix(self) -> None:
        survives = [(3, 29, 0), (3, 31, 6), (3, 31, 9), (4, 0, 3), (4, 0, 4), (4, 1, 2)]
        killed = [(3, 28, 3), (3, 31, 10), (4, 0, 5), (4, 1, 3), (4, 2, 0), (4, 3, 2), (4, 4, 4)]
        self.assertEqual([v for v in survives + killed if lister_survives_timeout(v)], survives)

    def test_declared_budget_passes_below_and_fails_bounded_above(self) -> None:
        budget = declared_budget()
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-budget-") as directory:
            root = Path(directory)
            project = FakeListerProject(root, budget)
            self.assertIn(f"TEST_DISCOVERY_TIMEOUT [==[{budget}]==]", project.include("Fake").read_text())

            delay = min(6.0, budget / 2)
            project.stale("Fake")
            result, elapsed = project.ctest("--show-only=json-v1", env={"FAKE_DELAY_Fake": str(delay)},
                                            timeout=budget + 30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertGreaterEqual(elapsed, delay, "fake lister did not run during this CTest call")
            # Callers merge stderr into this JSON (cpu_test_selection), so success stays silent.
            self.assertNotIn("IntrinsicTestDiscovery:", result.stderr)
            tests = json.loads(result.stdout)["tests"]
            self.assertEqual(sorted(test["name"] for test in tests),
                             sorted(f"Fake.Case{i}" for i in range(CASE_COUNT)))
            for test in tests:
                properties = {entry["name"]: entry["value"] for entry in test["properties"]}
                self.assertEqual(properties["TIMEOUT"], 30, "case TIMEOUT must stay independent of discovery")

            pid_file = root / "hung.pid"
            project.stale("Fake")
            result, elapsed = project.ctest("-N", env={"FAKE_DELAY_Fake": str(10 * budget),
                                                       "FAKE_PID_FILE_Fake": str(pid_file)},
                                            timeout=budget + 60)
            pid = int(pid_file.read_text())
            self.addCleanup(lambda: _alive(pid) and os.kill(pid, 9))  # upstream CTest orphan, even on failure
            output = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, output)
            self.assertGreaterEqual(elapsed, budget, output)
            self.assertLess(elapsed, budget + 15, output)
            self.assertIn(str(project.fakes["Fake"]), output, "failure must name the lister executable")
            self.assertIn("Process terminated due to timeout", output)
            # Repository-owned line: CMake < 4 does not print the budget itself.
            self.assertIn(f"IntrinsicTestDiscovery: listing '{project.fakes['Fake']}' failed (Process terminated "
                          f"due to timeout); its GoogleTest discovery budget (TEST_DISCOVERY_TIMEOUT) is {budget} s",
                          output, "failure must name the executable and the declared budget")
            deadline = time.monotonic() + 5
            while _alive(pid) and time.monotonic() < deadline:
                time.sleep(0.05)
            orphaned = lister_survives_timeout(ctest_version())
            self.assertEqual(_alive(pid), orphaned, "timed-out lister is still running" if not orphaned
                             else "this CTest should leave the timed-out lister running; recheck the version range")

            # The registry lock is released after the failed discovery, even with an orphan.
            result, elapsed = project.ctest("-N", env={}, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(f"Total Tests: {CASE_COUNT}", result.stdout)
            if orphaned:
                # The gate is still bounded; the cleanup reaps the orphan.
                print(f"note: CTest {'.'.join(map(str, ctest_version()))} left the timed-out lister running",
                      file=sys.stderr)


class ProbeSelfTests(unittest.TestCase):
    def probe(self, *args: str, env: dict[str, str] | None = None) -> list[dict]:
        result = subprocess.run([sys.executable, str(PROBE), *args], text=True, capture_output=True,
                                timeout=120, env={**os.environ, **(env or {})})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]

    def test_direct_timing_matches_known_fake_delay(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-probe-") as directory:
            project = FakeListerProject(Path(directory), declared_budget(), ("Slow", "Instant"))
            records = self.probe("direct", "--tests-dir", str(project.build), "--target", "Slow",
                                 "--target", "Instant", "--repetitions", "2", "--max-minutes", "2", "--cap-seconds", "10",
                                 env={"FAKE_DELAY_Slow": "1.0"})
            samples = [record for record in records if record.get("event") == "sample"]
            self.assertEqual(len(samples), 8)
            self.assertEqual({(s["target"], s["condition"]) for s in samples},
                             {(t, c) for t in ("Slow", "Instant") for c in ("cold", "warm")})
            for sample in samples:
                self.assertEqual(sample["exit_status"], 0, sample)
                self.assertFalse(sample["timed_out"])
                self.assertEqual(sample["listed_cases"], CASE_COUNT)
                for key in ("load", "pressure", "memory", "processes", "mtime_ns", "residency_bytes"):
                    self.assertIn(key, sample)
                if sample["target"] == "Slow":
                    self.assertGreaterEqual(sample["wall_seconds"], 1.0)
                    self.assertLess(sample["wall_seconds"], 2.0)
                else:
                    self.assertLess(sample["wall_seconds"], 0.5)
            # Rotation: each repetition starts with a different target.
            self.assertEqual([s["target"] for s in samples[::4]], ["Slow", "Instant"])

    def test_direct_replays_this_cmakes_discovery_command(self) -> None:
        # The fake logs cwd and argv; only the JSON file name may differ (CMake 4.x passes one).
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-argv-") as directory:
            project = FakeListerProject(Path(directory), declared_budget())
            log = Path(directory) / "argv.jsonl"
            env = {"FAKE_ARGV_FILE_Fake": str(log)}
            project.stale("Fake")
            result, _ = project.ctest("-N", env=env, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.probe("direct", "--tests-dir", str(project.build), "--target", "Fake",
                       "--max-minutes", "2", "--cap-seconds", "10", env=env)
            calls = [[re.sub(r"^--gtest_output=json:.*", "--gtest_output=json:<file>", arg) for arg in json.loads(line)]
                     for line in log.read_text().splitlines()]
            self.assertEqual(len(calls), 3, calls)  # CTest discovery, then the probe's cold and warm listing
            self.assertEqual(calls[1:], [calls[0]] * 2)

    def test_listing_child_dies_with_a_killed_probe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-orphan-") as directory:
            project = FakeListerProject(Path(directory), declared_budget())
            pid_file = Path(directory) / "pid"
            probe = subprocess.Popen([sys.executable, str(PROBE), "direct", "--tests-dir", str(project.build),
                                      "--target", "Fake", "--max-minutes", "2", "--cap-seconds", "60"],
                                     stdout=subprocess.DEVNULL, env={**os.environ, "FAKE_DELAY_Fake": "60",
                                                                     "FAKE_PID_FILE_Fake": str(pid_file)})
            try:
                deadline = time.monotonic() + 10
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                pid = int(pid_file.read_text())
            finally:
                probe.kill()
                probe.wait()
            deadline = time.monotonic() + 5
            while _alive(pid) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse(_alive(pid), "listing child outlived the killed probe")
        # A child whose probe died before PR_SET_PDEATHSIG was registered must not exec.
        self.assertEqual(subprocess.run(["true"], preexec_fn=discovery_probe._die_with(-1)).returncode, 127)
        self.assertEqual(subprocess.run(["true"], preexec_fn=discovery_probe._die_with(os.getpid())).returncode, 0)

    def test_planned_sequence_does_not_trip_identity_checks(self) -> None:
        # Run plan order: direct arm, forced-stale C-real, second direct arm, decide.
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-sequence-") as directory:
            root = Path(directory)
            project = FakeListerProject(root, declared_budget(), ("A", "B"))
            run("ctest", "--test-dir", str(project.build), "-N")
            common = ("--tests-dir", str(project.build), "--max-minutes", "2", "--cap-seconds", "10")
            targets = ("--target", "A", "--target", "B", "--repetitions", "2")
            self.probe("direct", *common, *targets, "--arm", "contention", "--output", str(root / "c.jsonl"))
            self.probe("ctest", *common, "--arm", "contention", "--force", "A", "--force", "B", "--evict",
                       "--output", str(root / "r.jsonl"), "--", "ctest", "--test-dir", str(project.build), "-N")
            self.probe("direct", *common, *targets, "--arm", "idle", "--output", str(root / "i.jsonl"))
            verdict = json.loads(subprocess.run(
                [sys.executable, str(PROBE), "decide", str(root / "c.jsonl"), str(root / "r.jsonl"),
                 str(root / "i.jsonl"), "--targets", "A", "B", "--ecs-target", "A",
                 "--baseline", str(root / "c.jsonl"), "--expected-ctest-total", str(2 * CASE_COUNT)],
                text=True, capture_output=True, timeout=60, check=True).stdout)
            identity = [kill for kill in verdict["kill_conditions"]
                        if any(word in kill for word in ("changed", "differs", "duplicate", "listing", "C-real"))]
            self.assertEqual(identity, [])
            # The regime depends on the host (other builds, tmpfs eviction); only failures matter.
            status = verdict["ctest_runs"][0]["status"]
            self.assertTrue(status == "valid" or status.startswith("invalid:"), verdict["ctest_runs"])

    def test_ctest_mode_caps_a_hung_command_and_keeps_its_record(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-cap-") as directory:
            project = FakeListerProject(Path(directory), declared_budget())
            project.stale("Fake")
            pid_file = Path(directory) / "pid"
            start = time.monotonic()
            records = self.probe("ctest", "--tests-dir", str(project.build), "--cap-seconds", "1",
                                 "--repetitions", "2", "--max-minutes", "1", "--",
                                 "ctest", "--test-dir", str(project.build), "-N",
                                 env={"FAKE_DELAY_Fake": "60", "FAKE_PID_FILE_Fake": str(pid_file)})
            self.assertLess(time.monotonic() - start, 10)
            runs = [record for record in records if record["event"] == "ctest"]
            self.assertEqual([run["timed_out"] for run in runs], [True, True])
            self.assertEqual(records[-1], {"event": "stop", "reason": "completed"})
            deadline = time.monotonic() + 5
            while _alive(int(pid_file.read_text())) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse(_alive(int(pid_file.read_text())), "capped CTest left its lister running")
            stopped = self.probe("ctest", "--tests-dir", str(project.build), "--cap-seconds", "1",
                                 "--max-minutes", "0.01", "--", "ctest", "--test-dir", str(project.build), "-N")
            self.assertEqual(stopped[-1], {"event": "stop", "reason": "deadline"})
            never_idle = self.probe("ctest", "--tests-dir", str(project.build), "--cap-seconds", "1",
                                    "--max-minutes", "0.1", "--wait-idle", "--idle-max-load", "0",
                                    "--poll-seconds", "1", "--", "ctest", "--test-dir", str(project.build), "-N")
            self.assertEqual({record["event"] for record in never_idle}, {"start", "wait_idle", "stop"})
            self.assertEqual(never_idle[-1], {"event": "stop", "reason": "deadline"})

    def test_process_classifier_ignores_command_lines_that_mention_clang(self) -> None:
        probe = discovery_probe
        cases = {("/usr/bin/clang++-23", "-c", "a.cpp", "-o", "a.o"): "compile",
                 ("clang", "-c", "a.c"): "compile",
                 ("/usr/bin/clang++-23", "-cc1", "-emit-obj"): None,
                 ("/bin/bash", "-c", "pgrep -fc 'clang.*-c '"): None,
                 ("python3", "probe.py", "--note", "clang++ -c x"): None,
                 ("/usr/bin/clang-scan-deps-23", "-format=p1689"): "scan_deps",
                 ("/usr/bin/ld.lld", "-o", "bin/x"): "link",
                 ("ninja", "-C", "build"): "ninja",
                 ("cmake", "--build", "--preset", "ci"): "cmake_build",
                 ("cmake", "--preset", "ci"): None}
        for argv, kind in cases.items():
            self.assertEqual(probe.classify(list(argv)), kind, argv)
        self.assertIn(os.getpid(), probe._ancestors())

    def test_ctest_mode_counts_forced_stale_enumerations(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-fanout-") as directory:
            project = FakeListerProject(Path(directory), declared_budget(), ("A", "B"))
            run("ctest", "--test-dir", str(project.build), "-N")
            command = ["--", "ctest", "--test-dir", str(project.build), "-N", "-R", r"^A\."]
            start, forced, stop = self.probe("ctest", "--tests-dir", str(project.build), "--force", "B", *command)
            self.assertEqual((start["event"], stop), ("start", {"event": "stop", "reason": "completed"}))
            self.assertFalse(forced["timed_out"])
            self.assertEqual(forced["exit_status"], 0, forced)
            self.assertEqual(forced["enumerated"], ["B"])
            self.assertEqual(forced["total_tests"], CASE_COUNT)
            _, fresh, _ = self.probe("ctest", "--tests-dir", str(project.build), *command)
            self.assertEqual(fresh["enumerated"], [])
            self.assertEqual(fresh["total_tests"], CASE_COUNT)


class DecisionRuleTests(unittest.TestCase):
    TARGETS = ("IntrinsicECSTests", "IntrinsicCoreTests", "IntrinsicRuntimeContractTests")
    GATES = ("--post-arm-gates", "contention=pass", "idle=pass", "--expected-ctest-total", "7")
    REGIMES = {"contention": ({"load_1m": 9.0}, {"compile": 3}), "idle": ({"load_1m": 0.5}, {"compile": 0})}

    def records(self, wall: float = 0.1, n: int = 300, **override) -> list[dict]:
        rows = []
        for arm, (load, processes) in self.REGIMES.items():
            for repetition in range(n):
                for target in self.TARGETS:
                    for condition in ("cold", "warm"):
                        rows.append({"event": "sample", "arm": arm, "repetition": repetition, "target": target,
                                     "condition": condition, "residency_bytes": 0, "wall_seconds": wall,
                                     "exit_status": 0, "timed_out": False, "listing_sha256": target,
                                     "listed_cases": 10, "json_written": True, "load": load,
                                     "processes": processes, "wall_clock": float(len(rows)),
                                     "mtime_ns": {f"{target}[1]_include.cmake": 1 + repetition},
                                     "executable_mtime_ns": 1})
        rows[-1].update(override)
        for arm, (load, processes) in self.REGIMES.items():
            rows += [{"event": "ctest", "arm": arm, "repetition": repetition, "exit_status": 0,
                      "timed_out": False, "wall_seconds": 1.0, "forced": list(self.TARGETS), "evicted": True,
                      "residency_bytes": {target: 0 for target in self.TARGETS},
                      "enumerated": list(self.TARGETS), "removed": [], "total_tests": 7,
                      "discovery_timeout_seen": False, "load": load, "processes": processes,
                      "wall_clock": float(len(rows))} for repetition in range(20)]
        return rows

    def decide(self, records: list[dict], *extra: str, gates: tuple[str, ...] = GATES) -> dict:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-decide-") as directory:
            path = Path(directory) / "samples.jsonl"
            path.write_text("".join(json.dumps(record) + "\n" for record in records))
            baseline = Path(directory) / "baseline.jsonl"
            baseline.write_text("".join(json.dumps({"event": "sample", "target": target, "listing_sha256": target})
                                        + "\n" for target in self.TARGETS))
            gates = ("--baseline", str(baseline), *gates)
            html = Path(directory) / "report.html"
            result = subprocess.run([sys.executable, str(PROBE), "decide", str(path), *gates, *extra], text=True,
                                    capture_output=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = subprocess.run([sys.executable, str(PROBE), "report", str(path), "--html", str(html),
                                     *gates, *extra], text=True, capture_output=True, timeout=120)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("Frozen verdicts", html.read_text())
            return json.loads(result.stdout)

    def test_fast_listings_reject_h1_and_choose_nothing(self) -> None:
        verdict = self.decide(self.records(wall=0.1))
        self.assertEqual((verdict["h1"], verdict["chosen_budget_seconds"], verdict["kill_conditions"]),
                         ("rejected", None, []))
        self.assertTrue(verdict["hypotheses"]["H3"]["cold"]["assessed"])
        self.assertTrue(verdict["hypotheses"]["H4"]["cold"]["assessed"])

    def test_tail_at_budget_chooses_ladder_step(self) -> None:
        verdict = self.decide(self.records(wall=0.1, wall_seconds=6.0))
        self.assertEqual((verdict["h1"], verdict["m_obs_seconds"], verdict["chosen_budget_seconds"]),
                         ("supported", 6.0, 20))

    def test_real_discovery_timeout_supports_h1_only_when_direct_reproduces_it(self) -> None:
        def with_timeout(rows: list[dict]) -> list[dict]:
            run = [row for row in rows if row["event"] == "ctest"][-1]
            # CMake 4.x removed the target's _tests.cmake and aborted before printing a total.
            run.update(exit_status=8, discovery_timeout_seen=True, enumerated=list(self.TARGETS[:2]),
                       removed=[self.TARGETS[2]], total_tests=None)
            return rows
        verdict = self.decide(with_timeout(self.records(wall=0.1, wall_seconds=6.0)))
        self.assertEqual((verdict["h1"], verdict["chosen_budget_seconds"], verdict["kill_conditions"]),
                         ("supported", 20, []))
        verdict = self.decide(with_timeout(self.records(wall=0.1)))
        self.assertIsNone(verdict["chosen_budget_seconds"])
        self.assertTrue(any("not reproduced" in kill for kill in verdict["kill_conditions"]))

    def test_tail_above_ceiling_is_killed(self) -> None:
        verdict = self.decide(self.records(wall=0.1, wall_seconds=21.0))
        self.assertIsNone(verdict["chosen_budget_seconds"])
        self.assertTrue(any("ceiling" in kill for kill in verdict["kill_conditions"]))

    def test_slow_idle_warm_null_probe_blocks_a_value(self) -> None:
        verdict = self.decide(self.records(wall=0.6, wall_seconds=6.0))
        self.assertEqual(verdict["h1"], "supported")
        self.assertIsNone(verdict["chosen_budget_seconds"])
        self.assertTrue(any("null probe" in kill for kill in verdict["kill_conditions"]))

    def test_identity_and_real_path_failures_block_rejection(self) -> None:
        pooled = self.records()
        pooled.append(dict(pooled[0]))
        relinked = self.records(executable_mtime_ns=2)
        rehashed = self.records(listing_sha256="other")
        empty = self.records(listed_cases=0)
        failed_real = self.records()
        failed_real[-1].update(exit_status=8)
        unlisted = self.records()
        unlisted[-1].update(enumerated=[])
        recount = self.records()
        recount[-1].update(total_tests=6)
        cases = ((pooled, "duplicate", self.GATES), (relinked, "executable changed", self.GATES),
                 (rehashed, "listing hash", self.GATES), (empty, "empty listing", self.GATES),
                 (failed_real, "without a discovery timeout", self.GATES),
                 (unlisted, "not relisted", self.GATES), (recount, "count 6 != 7", self.GATES),
                 (self.records(), "gate missing", ("--expected-ctest-total", "7")),
                 (self.records(), "gate fail", ("--post-arm-gates", "contention=pass", "idle=fail",
                                                 "--expected-ctest-total", "7")))
        for rows, needle, gates in cases:
            verdict = self.decide(rows, gates=gates)
            self.assertIsNone(verdict["chosen_budget_seconds"])
            self.assertNotEqual(verdict["h1"], "rejected", needle)
            self.assertTrue(any(needle in kill for kill in verdict["kill_conditions"]), (needle, verdict))
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-baseline-") as directory:
            baseline = Path(directory) / "baseline.jsonl"
            baseline.write_text(json.dumps({"event": "sample", "target": self.TARGETS[0],
                                            "listing_sha256": "validated"}) + "\n")
            verdict = self.decide(self.records(), "--baseline", str(baseline))
            self.assertTrue(any("validated baseline" in kill for kill in verdict["kill_conditions"]))
        few_real = [row for row in self.records() if not (row["event"] == "ctest" and row["repetition"] >= 19)]
        self.assertFalse(self.decide(few_real)["arms"]["idle"]["valid"])

    def test_c_real_runs_count_only_with_the_planned_forced_cold_discovery(self) -> None:
        def every_real(rows: list[dict], **fields) -> list[dict]:
            for row in rows:
                if row["event"] == "ctest":
                    row.update(fields)
            return rows
        # Cached CTest calls that forced, evicted and observed nothing must not count as coverage.
        verdict = self.decide(every_real(self.records(), forced=[], enumerated=[], evicted=False, residency_bytes={}))
        self.assertNotEqual(verdict["h1"], "rejected")
        self.assertTrue(any("not forced and evicted as planned" in kill for kill in verdict["kill_conditions"]))
        for fields in ({"evicted": False}, {"forced": [self.TARGETS[0]], "residency_bytes": {self.TARGETS[0]: 0}}):
            rows = self.records()
            rows[-1].update(fields)
            verdict = self.decide(rows)
            self.assertNotEqual(verdict["h1"], "rejected", fields)
            self.assertTrue(any("not forced and evicted as planned" in kill for kill in verdict["kill_conditions"]),
                            (fields, verdict["kill_conditions"]))
        # A forced target without an observed zero residency was not shown cold.
        for residency in ({target: 0 for target in self.TARGETS[:2]}, {target: None for target in self.TARGETS}):
            verdict = self.decide(every_real(self.records(), residency_bytes=residency))
            self.assertEqual(verdict["arms"]["idle"]["ctest_runs"], {"invalid:residency": 20}, residency)
            self.assertEqual(verdict["h1"], "inconclusive")

    def test_failed_sample_blocks_a_value(self) -> None:
        verdict = self.decide(self.records(wall=0.1, wall_seconds=6.0, exit_status=1))
        self.assertIsNone(verdict["chosen_budget_seconds"])

    def test_short_or_contaminated_cells_are_not_collected(self) -> None:
        self.assertEqual(self.decide(self.records(n=299))["h1"], "inconclusive")
        rows = self.records()
        for row in rows[-6 * 16 - 40:-40]:
            row["residency_bytes"] = 4096
        verdict = self.decide(rows)
        self.assertEqual(verdict["h1"], "inconclusive")
        self.assertFalse(verdict["arms"]["idle"]["valid"])
        self.assertFalse(verdict["hypotheses"]["H4"]["cold"]["assessed"])
        idle_build = self.records()
        for row in idle_build[:6 * 16]:
            row["processes"] = {"compile": 0}
        self.assertEqual(self.decide(idle_build)["cells"]["contention/cold/IntrinsicECSTests"]["invalid"],
                         {"no-compile": 16})
        quiet = self.records()
        for row in quiet:
            if row["arm"] == "contention" and row["event"] == "sample":
                row["load"] = {"load_1m": 1.0}
        verdict = self.decide(quiet)
        self.assertFalse(verdict["arms"]["contention"]["valid"])
        self.assertFalse(verdict["hypotheses"]["H3"]["cold"]["assessed"])
        self.assertEqual(self.decide(quiet, "--required-arms", "idle")["h1"], "rejected")
        busy_real = self.records()
        for row in busy_real:
            if row["event"] == "ctest" and row["arm"] == "idle":
                row["processes"] = {"compile": 2}
        verdict = self.decide(busy_real)
        self.assertEqual(verdict["arms"]["idle"]["ctest_runs"], {"invalid:not-idle": 20})
        self.assertFalse(verdict["arms"]["idle"]["valid"])
        busy_real[-1]["exit_status"] = 8  # a failure is never hidden behind a regime exclusion
        self.assertTrue(any("without a discovery timeout" in kill
                            for kill in self.decide(busy_real)["kill_conditions"]))
        # Off-regime timeouts are H1 evidence but do not count toward C-real coverage.
        off_regime = self.records(wall=0.1, wall_seconds=6.0)
        idle_runs = [row for row in off_regime if row["event"] == "ctest" and row["arm"] == "idle"]
        for row in idle_runs[:6]:
            row.update(processes={"compile": 2}, exit_status=8, discovery_timeout_seen=True,
                       enumerated=[], removed=[self.TARGETS[0]], total_tests=None)
        verdict = self.decide(off_regime)
        self.assertEqual(verdict["arms"]["idle"]["ctest_runs"], {"invalid:not-idle": 6, "valid": 14})
        self.assertFalse(verdict["arms"]["idle"]["valid"])
        self.assertEqual(verdict["h1"], "supported")
        self.assertIsNone(verdict["chosen_budget_seconds"])
        unstamped = self.records()
        del unstamped[0]["executable_mtime_ns"]
        self.assertTrue(any("executable changed" in kill for kill in self.decide(unstamped)["kill_conditions"]))


class ReconcileTests(unittest.TestCase):
    def tree(self, root: Path, rows: dict[str, tuple[str, int, int]], layout: str = "3.x",
             identity: str = "none") -> Path:
        (root / "tests").mkdir()
        (root / "CMakeCache.txt").write_text(f"INTRINSIC_SANITIZER_IDENTITY:INTERNAL={identity}\n")
        (root / "test-inventories").mkdir()
        lines = ["target\tlabels"]
        for target, (labels, discovery, case) in rows.items():
            lines.append(f"{target}\t{labels}")
            if layout == "3.x":  # CMake < 4.4: the include holds the call
                (root / "tests" / f"{target}[1]_include.cmake").write_text(
                    "    gtest_discover_tests_impl(\n"
                    f"      TEST_PROPERTIES [==[TIMEOUT;{case}]==]\n"
                    f"      TEST_DISCOVERY_TIMEOUT [==[{discovery}]==]\n    )\n")
            else:  # CMake 4.4: the include includes _discovery.cmake, lists are separate brackets
                call = root / "tests" / f"{target}_0123abcd_discovery.cmake"
                (root / "tests" / f"{target}_0123abcd_include.cmake").write_text(f'    include("{call}")\n')
                call.write_text("gtest_discover_tests_impl(\n"
                                f"  TEST_TARGET [==[{target}]==]\n"
                                f"  TEST_PROPERTIES [==[TIMEOUT]==] [==[{case}]==] [==[LABELS]==] [==[{labels}]==]\n"
                                f"  TEST_DISCOVERY_TIMEOUT [==[{discovery}]==]\n)\n")
        (root / "test-inventories/RegisteredTestTargets.tsv").write_text("\n".join(lines) + "\n")
        return root

    def test_live_check_accepts_policy_and_names_each_violation(self) -> None:
        budget = declared_budget()
        default_case, slow_case = case_budgets()
        good = {"Fast": ("unit", budget, default_case), "Slow": ("unit,slow", budget, slow_case)}
        for layout in ("3.x", "4.4"):
            with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
                self.assertIn(f"discovery_timeout={budget}", reconcile(self.tree(Path(directory), good, layout)))
            for rows, needle in (({**good, "Fast": ("unit", budget + 2, default_case)}, "stale include"),
                                 ({**good, "Slow": ("unit", budget, slow_case)}, f"case TIMEOUT {slow_case}"),
                                 ({"Fast": good["Fast"]}, "need default and slow")):
                with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
                    with self.assertRaisesRegex(AssertionError, needle):
                        reconcile(self.tree(Path(directory), rows, layout))

    def test_sanitized_tree_needs_scaled_case_budgets(self) -> None:
        budget = declared_budget()
        default_case, slow_case = case_budgets()
        scale = sanitizer_scale()
        scaled = {"Fast": ("unit", budget, default_case * scale), "Slow": ("unit,slow", budget, slow_case * scale)}
        unscaled = {"Fast": ("unit", budget, default_case), "Slow": ("unit,slow", budget, slow_case)}
        for identity, rows, needle in (("asan", scaled, None), ("ubsan", scaled, None),
                                       ("asan-ubsan", unscaled, f"case TIMEOUT {default_case} != {default_case * scale}"),
                                       ("none", scaled, f"case TIMEOUT {default_case * scale} != {default_case}")):
            with self.subTest(identity=identity), \
                    tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
                tree = self.tree(Path(directory), rows, identity=identity)
                if needle is None:
                    self.assertIn(f"timeout_scale={scale}", reconcile(tree))
                else:
                    with self.assertRaisesRegex(AssertionError, needle):
                        reconcile(tree)
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
            tree = self.tree(Path(directory), unscaled)
            (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
            with self.assertRaisesRegex(AssertionError, "no INTRINSIC_SANITIZER_IDENTITY"):
                reconcile(tree)

    def test_unknown_layout_fails_closed(self) -> None:
        budget = declared_budget()
        default_case, slow_case = case_budgets()
        good = {"Fast": ("unit", budget, default_case), "Slow": ("unit,slow", budget, slow_case)}
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
            tests = self.tree(Path(directory), good) / "tests"
            (tests / "Other_include.cmake").write_text("")
            with self.assertRaisesRegex(discovery_probe.ProbeError, "unknown GoogleTest discovery file layout"):
                reconcile(tests.parent)
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-live-") as directory:
            tests = self.tree(Path(directory), good, "4.4") / "tests"
            (tests / "Fast_0123abcd_include.cmake").write_text("# no discovery include\n")
            with self.assertRaisesRegex(discovery_probe.ProbeError, "Fast_0123abcd_discovery.cmake"):
                reconcile(tests.parent)


class LayoutTests(unittest.TestCase):
    """The probe finds one discovery call per target on both layouts and never guesses."""

    def test_finds_the_call_on_both_layouts_and_refuses_to_guess(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-layout-") as directory:
            tests = Path(directory)
            old = tests / "Old[1]_include.cmake"
            old.write_text("")
            (tests / "Old[2]_include.cmake").write_text("")  # a second call; [1] stays the probe's choice
            new, call = tests / "New_89abcdef_include.cmake", tests / "New_89abcdef_discovery.cmake"
            new.write_text(f'include("{call}")\n')
            self.assertEqual(discovery_probe.target_include(tests, "Old"), old)
            self.assertEqual(discovery_probe.discovery_call(old), old)
            self.assertEqual(discovery_probe.target_include(tests, "New"), new)
            self.assertEqual(discovery_probe.discovery_call(new), call)
            (tests / "Old_01234567_include.cmake").write_text("")
            with self.assertRaisesRegex(discovery_probe.ProbeError, "2 GoogleTest discovery includes for Old"):
                discovery_probe.target_include(tests, "Old")
            with self.assertRaisesRegex(discovery_probe.ProbeError, "0 GoogleTest discovery includes for Gone"):
                discovery_probe.target_include(tests, "Gone")
            for name in ("Old[1]_tests.cmake", "New_89abcdef_tests.cmake"):
                (tests / name).write_text("")
            self.assertEqual(set(discovery_probe.tests_files(tests)), {"Old", "New"})
            (tests / "Odd_tests.cmake").write_text("")
            with self.assertRaisesRegex(discovery_probe.ProbeError, "unknown GoogleTest discovery file layout"):
                discovery_probe.tests_files(tests)


def reconcile(build_dir: Path) -> str:
    budget = declared_budget()
    scale = tree_scale(build_dir)
    default_case, slow_case = (budget_ * scale for budget_ in case_budgets())
    with (build_dir / "test-inventories/RegisteredTestTargets.tsv").open(encoding="utf-8") as handle:
        labels = {row["target"]: row["labels"].split(",") for row in csv.DictReader(handle, delimiter="\t")}
    includes = sorted((build_dir / "tests").glob("*_include.cmake"))
    if not includes:
        raise AssertionError(f"no GoogleTest discovery includes under {build_dir / 'tests'}")
    seen = {default_case: 0, slow_case: 0}
    errors = []
    for include in includes:
        target = discovery_probe.generated_target(include)
        fields = parse_include(discovery_probe.discovery_call(include))
        discovery = int(fields["TEST_DISCOVERY_TIMEOUT"])
        if discovery != budget:
            errors.append(f"{include.name}: TEST_DISCOVERY_TIMEOUT {discovery} != declared {budget} "
                          "(stale include: reconfigure)")
        if target not in labels:
            errors.append(f"{include.name}: target missing from RegisteredTestTargets.tsv")
            continue
        expected = slow_case if "slow" in labels[target] else default_case
        actual = case_timeout(fields["TEST_PROPERTIES"])
        if actual != expected:
            errors.append(f"{include.name}: case TIMEOUT {actual} != {expected}")
        seen[expected] += 1
    if not all(seen.values()):
        errors.append(f"need default and slow case budgets to show independence, saw {seen}")
    if errors:
        raise AssertionError("\n".join(errors))
    return (f"gtest discovery budget: ok: includes={len(includes)} discovery_timeout={budget} "
            f"timeout_scale={scale} case_timeouts={seen}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, help="reconcile this configured tree only")
    args, remainder = parser.parse_known_args()
    if args.build_dir is not None:
        try:
            print(reconcile(args.build_dir.resolve()))
        except (AssertionError, OSError, ValueError, KeyError, discovery_probe.ProbeError) as error:
            print(f"gtest discovery budget: error: {error}", file=sys.stderr)
            raise SystemExit(1)
        raise SystemExit(0)
    unittest.main(argv=[sys.argv[0], *remainder])
