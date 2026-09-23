#!/usr/bin/env python3
"""Exercise concurrent PRE_TEST discovery with deterministic private CMake probes."""
from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
HELPER = ROOT / "cmake/IntrinsicTestDiscovery.cmake"
CASE_COUNT = 600


def wait_for(path: Path, processes: tuple[subprocess.Popen, ...] = ()) -> None:
    deadline = time.monotonic() + 15
    while not path.exists():
        for process in processes:
            if process.poll() is not None:
                raise AssertionError(f"CTest exited before barrier {path}: {process.returncode}")
        if time.monotonic() > deadline:
            raise AssertionError(f"Timed out waiting for {path}")
        time.sleep(0.01)


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=30)


class ConcurrentDiscoveryTests(unittest.TestCase):
    def project(self, root: Path, *, guarded: bool, duplicate: bool = False) -> Path:
        source = root / "source"
        source.mkdir()
        build = root / "build"
        # Copy only the discovery implementation. Never edit the installed module.
        probe = source / "locate.cmake"
        probe.write_text('file(WRITE "${CMAKE_CURRENT_LIST_DIR}/cmake-root.txt" "${CMAKE_ROOT}")\n')
        run("cmake", "-P", str(probe))
        module = Path((source / "cmake-root.txt").read_text()) / "Modules/GoogleTestAddTests.cmake"
        original = module.read_text()
        sync = source / "sync.py"
        sync.write_text('''import os, pathlib, sys, time
root = pathlib.Path(os.environ["DISCOVERY_BARRIERS"])
role = os.environ.get("DISCOVERY_ROLE", "single")
stage = sys.argv[1]
def mark(name): (root / name).touch()
def wait(name):
    deadline = time.monotonic() + 15
    while not (root / name).exists():
        if time.monotonic() > deadline: raise RuntimeError("barrier timeout: " + name)
        time.sleep(.01)
mark(role + "-" + stage)
if stage == "enter" and role == "a": wait("release")
if os.environ.get("DISCOVERY_RACE") == "1":
    if stage == "before-write" and role == "b": wait("a-after-write")
    if stage == "after-write" and role == "a": wait("b-after-write")
    if stage == "done": wait(("b" if role == "a" else "a") + "-done")
''')
        def call(stage: str) -> str:
            return (f'execute_process(COMMAND "{sys.executable}" "{sync}" {stage}'
                    ' COMMAND_ERROR_IS_FATAL ANY)\n')
        # CMake 3.28 flushes through a macro; 3.31 inlines its two write sites.
        # Instrument only the initial WRITE in either implementation, preserving
        # APPEND chunks and the production discovery lock unchanged.
        write = 'file(${flush_tests_MODE} "${_CTEST_FILE}" "${script}")'
        mode = "flush_tests_MODE"
        final_flush = "  # Write CTest script\n  flush_script()"
        write_count = 1
        if write not in original:
            write = 'file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")'
            mode = "file_write_mode"
            final_flush = "  # Write remaining content to the CTest script\n  " + write
            write_count = 2
        self.assertEqual(original.count(write), write_count, "CMake flush implementation changed")
        self.assertEqual(original.count(final_flush), 1, "CMake final flush implementation changed")
        instrumented = original.replace(final_flush, final_flush + "\n" + call("done"))
        instrumented = instrumented.replace(write,
            f'if({mode} STREQUAL "WRITE")\n' + call("before-write") + 'endif()\n'
            + write + f'\nif({mode} STREQUAL "WRITE")\n'
            + call("after-write") + 'endif()')
        entry = "function(gtest_discover_tests_impl)\n"
        self.assertEqual(instrumented.count(entry), 1)
        instrumented = instrumented.replace(entry, entry + call("enter"))
        private_module = source / "GoogleTestAddTests.cmake"
        private_module.write_text(instrumented)
        fake = source / "fake-gtest.py"
        fake.write_text(f'#!{sys.executable}\nimport sys\nif "--gtest_list_tests" in sys.argv:\n'
                        f'    print("Synthetic.")\n    for i in range({CASE_COUNT}): print("  Case" + str(i))\n'
                        + ('    print("  Case0")\n' if duplicate else ''))
        fake.chmod(0o755)
        body = f'''cmake_minimum_required(VERSION 3.28)
project(ConcurrentDiscovery NONE)
enable_testing()
include(GoogleTest)
set(_GOOGLETEST_DISCOVER_TESTS_SCRIPT "{private_module}")
add_executable(Fake IMPORTED GLOBAL)
set_target_properties(Fake PROPERTIES IMPORTED_LOCATION "{fake}")
gtest_discover_tests(Fake DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 7 LABELS synthetic)
add_test(NAME NestedRegistry COMMAND "${{CMAKE_CTEST_COMMAND}}" --test-dir "${{CMAKE_BINARY_DIR}}" --show-only=json-v1)
set_tests_properties(NestedRegistry PROPERTIES TIMEOUT 10)
'''
        if guarded:
            body += f'include("{HELPER}")\nintrinsic_serialize_test_discovery()\n'
        (source / "CMakeLists.txt").write_text(body)
        run("cmake", "-S", str(source), "-B", str(build))
        # Newer CMake ignores the private-script variable above. Redirect only
        # this fixture's generated include and verify the instrumented path.
        discovery = build / "Fake[1]_include.cmake"
        generated = discovery.read_text()
        installed_include = f'include("{module}")'
        private_include = f'include("{private_module}")'
        self.assertEqual(generated.count(installed_include) + generated.count(private_include), 1,
                         "CMake discovery include changed")
        discovery.write_text(generated.replace(installed_include, private_include))
        # Startup marker runs before the production lock, demonstrating the
        # second CTest reached registry parsing while the first owns discovery.
        registry = build / "CTestTestfile.cmake"
        registry.write_text(call("started") + registry.read_text())
        return build

    def concurrent(self, root: Path, build: Path, *, race: bool) -> tuple[dict, dict]:
        barriers = root / "barriers"
        barriers.mkdir()
        processes = []
        handles = []
        try:
            for role in ("a", "b"):
                env = dict(os.environ, DISCOVERY_BARRIERS=str(barriers), DISCOVERY_ROLE=role,
                           DISCOVERY_RACE="1" if race else "0")
                handle = (root / f"{role}.log").open("w+")
                handles.append(handle)
                process = subprocess.Popen(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                                           env=env, stdout=handle, stderr=subprocess.STDOUT, text=True)
                processes.append(process)
                wait_for(barriers / f"{role}-started", tuple(processes))
                if role == "a" or race:
                    wait_for(barriers / f"{role}-enter", tuple(processes))
            if not race:
                # A is paused inside real discovery: prove the production lock
                # is held, so B cannot pass it regardless of process scheduling.
                lock_probe = root / "probe-lock.cmake"
                lock_probe.write_text(
                    f'file(LOCK "{build / "IntrinsicTestDiscovery.lock"}" TIMEOUT 0 RESULT_VARIABLE result)\n'
                    'if(result STREQUAL "0")\n'
                    '  message(FATAL_ERROR "Discovery registry lock was not held")\n'
                    'endif()\n')
                run("cmake", "-P", str(lock_probe))
            (barriers / "release").touch()
            documents = []
            for process, handle in zip(processes, handles):
                code = process.wait(timeout=25)
                handle.seek(0)
                output = handle.read()
                self.assertEqual(code, 0, output)
                documents.append(json.loads(output))
            if not race:
                self.assertFalse((barriers / "b-enter").exists(), "second reader repeated discovery")
            return tuple(documents)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.wait()
            for handle in handles:
                handle.close()

    def assert_registry(self, document: dict) -> None:
        tests = document["tests"]
        self.assertEqual(Counter(test["name"] for test in tests),
                         Counter([f"Synthetic.Case{i}" for i in range(CASE_COUNT)] + ["NestedRegistry"]))
        for test in tests:
            if test["name"] == "NestedRegistry":
                continue
            properties = {entry["name"]: entry["value"] for entry in test["properties"]}
            self.assertEqual(properties["LABELS"], ["synthetic"])
            self.assertEqual(properties["TIMEOUT"], 7)
            self.assertIn(f'--gtest_filter={test["name"]}', test["command"])

    def test_unlocked_control_reproduces_duplicate_appends(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-control-") as directory:
            root = Path(directory)
            build = self.project(root, guarded=False)
            documents = self.concurrent(root, build, race=True)
            for document in documents:
                names = [test["name"] for test in document["tests"]]
                self.assertEqual(set(names), {f"Synthetic.Case{i}" for i in range(CASE_COUNT)} | {"NestedRegistry"})
                self.assertGreater(len(names), len(set(names)), "control did not reproduce append corruption")

    def test_guard_serializes_and_releases_before_nested_ctest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-guard-") as directory:
            root = Path(directory)
            build = self.project(root, guarded=True)
            for document in self.concurrent(root, build, race=False):
                self.assert_registry(document)
            env = dict(os.environ, DISCOVERY_BARRIERS=str(root / "barriers"), DISCOVERY_ROLE="nested")
            result = subprocess.run(["ctest", "--test-dir", str(build), "-R", "^NestedRegistry$", "--output-on-failure"],
                                    env=env, text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_guard_preserves_real_duplicate_registrations(self) -> None:
        with tempfile.TemporaryDirectory(prefix="intrinsic-discovery-duplicate-") as directory:
            root = Path(directory)
            build = self.project(root, guarded=True, duplicate=True)
            for document in self.concurrent(root, build, race=False):
                names = Counter(test["name"] for test in document["tests"])
                self.assertEqual(names["Synthetic.Case0"], 2)
                self.assertEqual(sum(names.values()), CASE_COUNT + 2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", type=Path, default=HELPER)
    args, remainder = parser.parse_known_args()
    HELPER = args.helper.resolve()
    unittest.main(argv=[sys.argv[0], *remainder])
