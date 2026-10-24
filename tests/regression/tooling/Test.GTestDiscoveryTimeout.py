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


def lock_timeout() -> int:
    match = re.search(r"GUARD PROCESS TIMEOUT (\d+)", HELPER.read_text(encoding="utf-8"))
    if match is None:
        raise AssertionError("discovery lock timeout not found in IntrinsicTestDiscovery.cmake")
    return int(match.group(1))


def run(*args: str, timeout: float = 60, **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=timeout, **kwargs)


def googletest_module() -> str:
    with tempfile.TemporaryDirectory(prefix="intrinsic-cmake-root-") as directory:
        probe = Path(directory) / "locate.cmake"
        probe.write_text('file(WRITE "${CMAKE_CURRENT_LIST_DIR}/root.txt" "${CMAKE_ROOT}")\n')
        run("cmake", "-P", str(probe))
        root = (Path(directory) / "root.txt").read_text()
    return (Path(root) / "Modules/GoogleTestAddTests.cmake").read_text()


def parse_include(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    fields = dict(re.findall(r"(?m)^\s+([A-Z_]+) \[==\[(.*?)\]==\]\s*$", text))
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
        # A hung lister must fail with its own diagnostic before a concurrent
        # reader's registry-lock wait expires.
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
                f"#!{sys.executable}\nimport os, pathlib, sys, time\n"
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
        return self.build / f"{target}[1]_include.cmake"

    def stale(self, target: str) -> None:
        (self.build / f"{target}[1]_tests.cmake").unlink(missing_ok=True)

    def ctest(self, *args: str, env: dict[str, str], timeout: float) -> tuple[subprocess.CompletedProcess[str], float]:
        start = time.monotonic()
        result = subprocess.run(["ctest", "--test-dir", str(self.build), *args], text=True,
                                capture_output=True, timeout=timeout, env={**os.environ, **env})
        return result, time.monotonic() - start


def _alive(pid: int) -> bool:
    stat = Path(f"/proc/{pid}/stat")
    try:
        return stat.read_text().rsplit(")", 1)[1].split()[0] != "Z"
    except (FileNotFoundError, IndexError):
        return False


class FakeListerBudgetTests(unittest.TestCase):
    def test_declared_budget_passes_below_and_fails_bounded_above(self) -> None:
        budget = declared_budget()
        module = googletest_module()
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
            output = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, output)
            self.assertGreaterEqual(elapsed, budget, output)
            self.assertLess(elapsed, budget + 15, output)
            self.assertIn(str(project.fakes["Fake"]), output, "failure must name the lister executable")
            self.assertIn("Process terminated due to timeout", output)
            if "  Timeout: '" in module:
                self.assertIn(f"Timeout: '{budget}'", output, "failure must print the declared budget")
            pid = int(pid_file.read_text())
            deadline = time.monotonic() + 5
            while _alive(pid) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse(_alive(pid), "timed-out lister is still running")

            # The registry lock is released after the failed discovery.
            result, elapsed = project.ctest("-N", env={}, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(f"Total Tests: {CASE_COUNT}", result.stdout)


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

    def test_process_classifier_ignores_command_lines_that_mention_clang(self) -> None:
        sys.path.insert(0, str(PROBE.parent))
        try:
            import gtest_discovery_probe as probe
        finally:
            sys.path.remove(str(PROBE.parent))
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

    def records(self, wall: float = 0.1, n: int = 300, **override) -> list[dict]:
        rows = []
        for arm, load, compile_count in (("contention", 9.0, 3), ("idle", 0.5, 0)):
            for repetition in range(n):
                for target in self.TARGETS:
                    for condition in ("cold", "warm"):
                        rows.append({"event": "sample", "arm": arm, "repetition": repetition, "target": target,
                                     "condition": condition, "residency_bytes": 0, "wall_seconds": wall,
                                     "exit_status": 0, "timed_out": False, "listing_sha256": target,
                                     "load": {"load_1m": load}, "processes": {"compile": compile_count},
                                     "mtime_ns": {"exe": 1, "include": 1}, "wall_clock": float(len(rows))})
        rows[-1].update(override)
        for arm in ("contention", "idle"):
            rows += [{"event": "ctest", "arm": arm, "repetition": repetition, "exit_status": 0,
                      "timed_out": False, "wall_seconds": 1.0, "forced": list(self.TARGETS),
                      "enumerated": list(self.TARGETS), "total_tests": 7, "discovery_timeout_seen": False,
                      "load": {"load_1m": 1.0}, "processes": {"compile": 0}, "wall_clock": float(len(rows))}
                     for repetition in range(20)]
        return rows

    def decide(self, records: list[dict], *extra: str) -> dict:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-decide-") as directory:
            path = Path(directory) / "samples.jsonl"
            path.write_text("".join(json.dumps(record) + "\n" for record in records))
            html = Path(directory) / "report.html"
            result = subprocess.run([sys.executable, str(PROBE), "decide", str(path), *extra], text=True,
                                    capture_output=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = subprocess.run([sys.executable, str(PROBE), "report", str(path), "--html", str(html)],
                                    text=True, capture_output=True, timeout=120)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("Frozen verdicts", html.read_text())
            return json.loads(result.stdout)

    def test_fast_listings_reject_h1_and_choose_nothing(self) -> None:
        verdict = self.decide(self.records(wall=0.1))
        self.assertEqual((verdict["h1"], verdict["chosen_budget_seconds"], verdict["kill_conditions"]),
                         ("rejected", None, []))

    def test_tail_at_budget_chooses_ladder_step(self) -> None:
        verdict = self.decide(self.records(wall=0.1, wall_seconds=6.0))
        self.assertEqual((verdict["h1"], verdict["m_obs_seconds"], verdict["chosen_budget_seconds"]),
                         ("supported", 6.0, 20))

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
        changed = self.records(mtime_ns={"exe": 2, "include": 1})
        rehashed = self.records(listing_sha256="other")
        failed_real = self.records()
        failed_real[-1].update(exit_status=8)
        timeout_only = self.records()
        timeout_only[-1].update(exit_status=8, discovery_timeout_seen=True)
        for rows, needle in ((pooled, "duplicate"), (changed, "changed during the run"),
                             (rehashed, "listing hash"), (failed_real, "without a discovery timeout"),
                             (timeout_only, "not reproduced")):
            verdict = self.decide(rows)
            self.assertIsNone(verdict["chosen_budget_seconds"])
            self.assertNotEqual(verdict["h1"], "rejected", needle)
            self.assertTrue(any(needle in kill for kill in verdict["kill_conditions"]), (needle, verdict))
        few_real = [row for row in self.records() if not (row["event"] == "ctest" and row["repetition"] >= 19)]
        self.assertFalse(self.decide(few_real)["arms"]["idle"]["valid"])

    def test_failed_sample_blocks_a_value(self) -> None:
        verdict = self.decide(self.records(wall=0.1, wall_seconds=6.0, exit_status=1))
        self.assertIsNone(verdict["chosen_budget_seconds"])

    def test_short_or_contaminated_cells_are_not_collected(self) -> None:
        self.assertEqual(self.decide(self.records(n=299))["h1"], "inconclusive")
        rows = self.records()
        for row in rows[-6 * 16:]:
            row["residency_bytes"] = 4096
        verdict = self.decide(rows)
        self.assertEqual(verdict["h1"], "inconclusive")
        self.assertFalse(verdict["arms"]["idle"]["valid"])
        idle_build = self.records()
        for row in idle_build[:6 * 16]:
            row["processes"] = {"compile": 0}
        self.assertEqual(self.decide(idle_build)["cells"]["contention/cold/IntrinsicECSTests"]["invalid"],
                         {"no-compile": 16})
        quiet = self.records()
        for row in quiet:
            if row["arm"] == "contention" and row["event"] == "sample":
                row["load"] = {"load_1m": 1.0}
        self.assertFalse(self.decide(quiet)["arms"]["contention"]["valid"])
        self.assertEqual(self.decide(quiet, "--required-arms", "idle")["h1"], "rejected")


def reconcile(build_dir: Path) -> str:
    budget = declared_budget()
    default_case, slow_case = case_budgets()
    with (build_dir / "test-inventories/RegisteredTestTargets.tsv").open(encoding="utf-8") as handle:
        labels = {row["target"]: row["labels"].split(",") for row in csv.DictReader(handle, delimiter="\t")}
    includes = sorted((build_dir / "tests").glob("*_include.cmake"))
    if not includes:
        raise AssertionError(f"no GoogleTest discovery includes under {build_dir / 'tests'}")
    seen = {default_case: 0, slow_case: 0}
    errors = []
    for include in includes:
        target = include.name.split("[", 1)[0]
        fields = parse_include(include)
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
            f"case_timeouts={seen}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, help="reconcile this configured tree only")
    args, remainder = parser.parse_known_args()
    if args.build_dir is not None:
        try:
            print(reconcile(args.build_dir.resolve()))
        except (AssertionError, OSError, ValueError, KeyError) as error:
            print(f"gtest discovery budget: error: {error}", file=sys.stderr)
            raise SystemExit(1)
        raise SystemExit(0)
    unittest.main(argv=[sys.argv[0], *remainder])
