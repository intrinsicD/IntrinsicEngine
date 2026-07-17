#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import json
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Iterable, Sequence


REPO_ROOT = Path(__file__).resolve().parents[3]
RUN_COVERAGE = REPO_ROOT / "tools" / "ci" / "run_source_coverage.py"
COMPARE_COVERAGE = REPO_ROOT / "tools" / "ci" / "compare_source_coverage.py"
MINIMUM_LLVM_MAJOR = 20
sys.path.insert(0, str(REPO_ROOT / "tools" / "ci"))

from source_coverage import CoverageError, normalize_llvm_cov_export  # noqa: E402

CMAKE_PROJECT = r"""
cmake_minimum_required(VERSION 3.24)
project(IntrinsicCoverageFixture LANGUAGES CXX)

enable_testing()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(EXTRINSIC_PLATFORM "Linux" CACHE STRING "")
set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING "")
set(INTRINSIC_PLATFORM_BACKEND "Glfw" CACHE STRING "")
set(INTRINSIC_PLATFORM_BACKEND_SELECTED "Glfw" CACHE STRING "")
set(INTRINSIC_HEADLESS_NO_GLFW OFF CACHE BOOL "")
set(BUILD_TESTING ON CACHE BOOL "")
set(INTRINSIC_BUILD_BENCHMARKS OFF CACHE BOOL "")
set(INTRINSIC_BUILD_SANDBOX OFF CACHE BOOL "")
set(INTRINSIC_BUILD_TESTS ON CACHE BOOL "")
set(INTRINSIC_ENABLE_CUDA OFF CACHE BOOL "")
set(INTRINSIC_ENABLE_SOURCE_COVERAGE ON CACHE BOOL "")
set(INTRINSIC_ENABLE_SANITIZERS OFF CACHE BOOL "")
set(INTRINSIC_SANITIZER_IDENTITY none CACHE INTERNAL "")
set(INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE atomic CACHE INTERNAL "")
set(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN OFF CACHE BOOL "")
option(INCLUDE_UNINSTRUMENTED "Include a deliberately invalid CPU producer" OFF)
option(INCLUDE_NO_PROFILE "Include a producer that emits no raw profile" OFF)
option(INCLUDE_XML_OMISSION "Include a producer with incomplete execution XML" OFF)
option(RENAME_TEST_TARGETS "Rename only the GoogleTest producer targets" OFF)
option(MOVE_TEST_WORKING_DIRECTORY "Move one case to another build subdirectory" OFF)
option(MOVE_MANUAL_TEST_WORKING_DIRECTORY "Move the manual CTest producer" OFF)

function(add_coverage_test target production_source test_source case_name)
    add_executable("${target}" "${production_source}" "${test_source}")
    target_compile_features("${target}" PRIVATE cxx_std_23)
    target_compile_options(
        "${target}"
        PRIVATE
        -fprofile-instr-generate
        -fprofile-update=atomic
        -fcoverage-mapping
    )
    target_link_options("${target}" PRIVATE -fprofile-instr-generate)
    add_test(
        NAME "${case_name}"
        COMMAND
            "${target}"
            "--gtest_filter=${case_name}"
            "--gtest_also_run_disabled_tests"
    )
    set_tests_properties("${case_name}" PROPERTIES LABELS "core;unit")
endfunction()

set(alpha_target AlphaTests)
set(beta_target BetaTests)
if(RENAME_TEST_TARGETS)
    set(alpha_target AlphaSplitTests)
    set(beta_target BetaSplitTests)
endif()

add_coverage_test(
    "${alpha_target}"
    "${CMAKE_SOURCE_DIR}/src/alpha.cpp"
    "${CMAKE_SOURCE_DIR}/tests/alpha_test.cpp"
    "Alpha.CoversBothArms"
)
add_coverage_test(
    "${beta_target}"
    "${CMAKE_SOURCE_DIR}/src/beta.cpp"
    "${CMAKE_SOURCE_DIR}/tests/beta_test.cpp"
    "Beta.CoversBothArms"
)

if(MOVE_TEST_WORKING_DIRECTORY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/moved-working-directory")
    set_tests_properties(
        "Alpha.CoversBothArms"
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/moved-working-directory"
    )
endif()

add_executable(
    ManualTests
    "${CMAKE_SOURCE_DIR}/src/manual.cpp"
    "${CMAKE_SOURCE_DIR}/tests/manual_test.cpp"
)
target_compile_features(ManualTests PRIVATE cxx_std_23)
target_compile_options(
    ManualTests
    PRIVATE
    -fprofile-instr-generate
    -fprofile-update=atomic
    -fcoverage-mapping
)
target_link_options(ManualTests PRIVATE -fprofile-instr-generate)
add_test(NAME "Manual.Contract" COMMAND ManualTests)
set_tests_properties("Manual.Contract" PROPERTIES LABELS "core;unit")
if(MOVE_MANUAL_TEST_WORKING_DIRECTORY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/moved-manual-working-directory")
    set_tests_properties(
        "Manual.Contract"
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/moved-manual-working-directory"
    )
endif()

set(cpu_targets "${alpha_target}" "${beta_target}" ManualTests)
if(INCLUDE_UNINSTRUMENTED)
    add_executable(
        PlainTests
        "${CMAKE_SOURCE_DIR}/src/plain.cpp"
        "${CMAKE_SOURCE_DIR}/tests/plain_test.cpp"
    )
    target_compile_features(PlainTests PRIVATE cxx_std_23)
    add_test(
        NAME "Plain.Uninstrumented"
        COMMAND PlainTests "--gtest_filter=Plain.Uninstrumented"
    )
    set_tests_properties(
        "Plain.Uninstrumented"
        PROPERTIES LABELS "core;unit"
    )
    list(APPEND cpu_targets PlainTests)
endif()

if(INCLUDE_NO_PROFILE)
    add_executable(
        NoProfileTests
        "${CMAKE_SOURCE_DIR}/src/no_profile.cpp"
        "${CMAKE_SOURCE_DIR}/tests/no_profile_test.cpp"
    )
    target_compile_features(NoProfileTests PRIVATE cxx_std_23)
    target_compile_options(
        NoProfileTests
        PRIVATE
        -fprofile-instr-generate
        -fprofile-update=atomic
        -fcoverage-mapping
    )
    target_link_options(NoProfileTests PRIVATE -fprofile-instr-generate)
    add_test(
        NAME "NoProfile.ExitsWithoutFlush"
        COMMAND
            NoProfileTests
            "--gtest_filter=NoProfile.ExitsWithoutFlush"
            "--gtest_also_run_disabled_tests"
    )
    set_tests_properties(
        "NoProfile.ExitsWithoutFlush"
        PROPERTIES LABELS "core;unit"
    )
    list(APPEND cpu_targets NoProfileTests)
endif()

if(INCLUDE_XML_OMISSION)
    add_coverage_test(
        LiarTests
        "${CMAKE_SOURCE_DIR}/src/liar.cpp"
        "${CMAKE_SOURCE_DIR}/tests/liar_test.cpp"
        "Liar.Requested"
    )
    list(APPEND cpu_targets LiarTests)
endif()

list(SORT cpu_targets)
add_custom_target(IntrinsicCpuTests DEPENDS ${cpu_targets})
add_custom_target(IntrinsicCpuCoverageTests DEPENDS ${cpu_targets})

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-inventories")
set(target_registry "target\tlabels\n")
set(aggregate_inventory "")
foreach(target IN LISTS cpu_targets)
    string(APPEND target_registry "${target}\tcore,unit\n")
    string(APPEND aggregate_inventory "${target}\n")
endforeach()
file(
    WRITE
    "${CMAKE_BINARY_DIR}/test-inventories/RegisteredTestTargets.tsv"
    "${target_registry}"
)
file(
    WRITE
    "${CMAKE_BINARY_DIR}/test-inventories/IntrinsicCpuTests.txt"
    "${aggregate_inventory}"
)
file(
    WRITE
    "${CMAKE_BINARY_DIR}/test-inventories/IntrinsicCpuCoverageTests.txt"
    "${aggregate_inventory}"
)
"""

ALPHA_SOURCE = r"""
[[gnu::noinline]] int alpha_value(int value) {
    if (value > 0) {
        return 1;
    }
    return -1;
}
"""

ALPHA_TEST = r"""
#include "fake_gtest.hpp"

#include <iostream>

int alpha_value(int);

int main(int argc, char** argv) {
    const auto arguments = fake_gtest::parse(argc, argv);
    if (arguments.list_tests) {
        std::cout << "Alpha.\n  CoversBothArms\n";
        return 0;
    }
    fake_gtest::write_xml(arguments, "Alpha", "CoversBothArms");
    return alpha_value(7) + alpha_value(-7);
}
"""

BETA_SOURCE = r"""
[[gnu::noinline]] int beta_value(int value) {
    if ((value % 2) == 0) {
        return 2;
    }
    return -2;
}
"""

BETA_TEST = r"""
#include "fake_gtest.hpp"

#include <iostream>

int beta_value(int);

int main(int argc, char** argv) {
    const auto arguments = fake_gtest::parse(argc, argv);
    if (arguments.list_tests) {
        std::cout << "Beta.\n  CoversBothArms\n";
        return 0;
    }
    fake_gtest::write_xml(arguments, "Beta", "CoversBothArms");
    return beta_value(2) + beta_value(3);
}
"""

MANUAL_SOURCE = r"""
[[gnu::noinline]] int manual_value() {
    return 0;
}
"""

MANUAL_TEST = r"""
int manual_value();

int main() {
    return manual_value();
}
"""

PLAIN_SOURCE = r"""
int plain_value() {
    return 0;
}
"""

PLAIN_TEST = r"""
#include "fake_gtest.hpp"

#include <iostream>

int plain_value();

int main(int argc, char** argv) {
    const auto arguments = fake_gtest::parse(argc, argv);
    if (arguments.list_tests) {
        std::cout << "Plain.\n  Uninstrumented\n";
        return 0;
    }
    fake_gtest::write_xml(arguments, "Plain", "Uninstrumented");
    return plain_value();
}
"""

NO_PROFILE_SOURCE = r"""
int no_profile_value() {
    return 0;
}
"""

NO_PROFILE_TEST = r"""
#include "fake_gtest.hpp"

#include <cstdio>
#include <cstdlib>

int no_profile_value();

int main(int argc, char** argv) {
    const auto arguments = fake_gtest::parse(argc, argv);
    if (arguments.list_tests) {
        std::fputs("NoProfile.\n  ExitsWithoutFlush\n", stdout);
        std::fflush(stdout);
        return 0;
    }
    fake_gtest::write_xml(arguments, "NoProfile", "ExitsWithoutFlush");
    (void)no_profile_value();
    std::_Exit(0);
}
"""

LIAR_SOURCE = r"""
int liar_value() {
    return 0;
}
"""

LIAR_TEST = r"""
#include "fake_gtest.hpp"

#include <iostream>

int liar_value();

int main(int argc, char** argv) {
    const auto arguments = fake_gtest::parse(argc, argv);
    if (arguments.list_tests) {
        std::cout << "Liar.\n  Requested\n";
        return 0;
    }
    fake_gtest::write_xml(arguments, "Liar", "Requested", false);
    return liar_value();
}
"""

FAKE_GTEST_HEADER = r"""
#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fake_gtest {

struct Arguments {
    bool list_tests{false};
    std::filesystem::path xml_output;
};

inline Arguments parse(int argc, char** argv) {
    Arguments arguments;
    constexpr std::string_view output_prefix = "--gtest_output=xml:";
    for (int index = 1; index < argc; ++index) {
        const std::string_view value(argv[index]);
        if (value == "--gtest_list_tests") {
            arguments.list_tests = true;
        } else if (value.starts_with(output_prefix)) {
            arguments.xml_output =
                std::string(value.substr(output_prefix.size()));
        }
    }
    return arguments;
}

inline void write_xml(
    const Arguments& arguments,
    std::string_view suite,
    std::string_view test_case,
    bool include_case = true
) {
    if (arguments.xml_output.empty()) {
        return;
    }
    if (!arguments.xml_output.parent_path().empty()) {
        std::filesystem::create_directories(
            arguments.xml_output.parent_path()
        );
    }
    std::ofstream output(arguments.xml_output);
    if (!output) {
        throw std::runtime_error("cannot write synthetic GoogleTest XML");
    }
    output
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << '\n'
        << R"(<testsuites tests=")" << (include_case ? 1 : 0)
        << R"(" failures="0" disabled="0" errors="0" time="0">)"
        << '\n'
        << R"(  <testsuite name=")" << suite
        << R"(" tests=")" << (include_case ? 1 : 0)
        << R"(" failures="0" disabled="0" errors="0" time="0">)"
        << '\n';
    if (include_case) {
        output
            << R"(    <testcase name=")" << test_case
            << R"(" status="run" result="completed" time="0" classname=")"
            << suite << R"("/>)" << '\n';
    }
    output << "  </testsuite>\n</testsuites>\n";
}

}  // namespace fake_gtest
"""

FAKE_RECONCILER = r"""
#!/usr/bin/env python3
import argparse
import os
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", required=True)
parser.add_argument("--aggregate", required=True)
arguments = parser.parse_args()
if arguments.aggregate not in {"IntrinsicCpuCoverageTests", "IntrinsicCpuTests"}:
    parser.error("unexpected aggregate")
profile_pattern = os.environ.get("LLVM_PROFILE_FILE")
if profile_pattern is None:
    parser.error("LLVM_PROFILE_FILE was not propagated to discovery")
profile_path = Path(profile_pattern)
if not profile_path.is_absolute():
    parser.error("discovery profile path must be absolute")
if profile_path.parent.name != "discovery-profiles":
    parser.error("discovery profiles must use an isolated directory")
if profile_path.name != "%m-%p.profraw":
    parser.error("discovery profile path must retain %m-%p placeholders")
expanded_profile = Path(
    str(profile_path)
    .replace("%m", "fake-reconciler")
    .replace("%p", str(os.getpid()))
)
expanded_profile.parent.mkdir(parents=True, exist_ok=True)
expanded_profile.write_bytes(b"isolated discovery profile sentinel")
print("synthetic test-gate reconciliation passed")
"""


class CommandError(AssertionError):
    def __init__(self, command: Sequence[str], result: subprocess.CompletedProcess[str]):
        super().__init__(
            f"command failed with exit code {result.returncode}: "
            f"{' '.join(command)}\n{result.stdout[-16000:]}"
        )
        self.command = tuple(command)
        self.result = result


def _run(
    command: Sequence[str],
    *,
    cwd: Path,
    check: bool = True,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(command),
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=timeout,
    )
    if check and result.returncode != 0:
        raise CommandError(command, result)
    return result


def _tool_major(path: str, *, cwd: Path) -> int | None:
    result = _run([path, "--version"], cwd=cwd, check=False, timeout=15)
    match = re.search(r"(?:clang|LLVM) version\s+(\d+)", result.stdout)
    if result.returncode != 0 or match is None:
        return None
    return int(match.group(1))


def _candidate_paths(prefix: str) -> Iterable[str]:
    for major in range(30, MINIMUM_LLVM_MAJOR - 1, -1):
        candidate = shutil.which(f"{prefix}-{major}")
        if candidate:
            yield candidate
    candidate = shutil.which(prefix)
    if candidate:
        yield candidate


def _matching_llvm_tools(cwd: Path) -> tuple[str, str, str] | None:
    compilers: dict[int, str] = {}
    cov_tools: dict[int, str] = {}
    profdata_tools: dict[int, str] = {}
    for prefix, destination in (
        ("clang++", compilers),
        ("llvm-cov", cov_tools),
        ("llvm-profdata", profdata_tools),
    ):
        for path in _candidate_paths(prefix):
            major = _tool_major(path, cwd=cwd)
            if major is not None and major >= MINIMUM_LLVM_MAJOR:
                destination.setdefault(major, path)

    # Prefer the compatibility floor when multiple complete LLVM suites exist.
    matching = sorted(set(compilers) & set(cov_tools) & set(profdata_tools))
    if not matching:
        return None
    major = matching[0]
    return compilers[major], cov_tools[major], profdata_tools[major]


def _write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(contents).lstrip(), encoding="utf-8")


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _json_digest(value: Any) -> str:
    payload = json.dumps(value, separators=(",", ":"), sort_keys=True)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _covered_set(report: dict[str, Any], name: str) -> list[str]:
    value = report["coverage"][name]
    if not isinstance(value, list):
        raise AssertionError(f"coverage.{name} is not a list: {value!r}")
    return value


def _recursive_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, child in value.items():
            yield str(key)
            yield from _recursive_strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from _recursive_strings(child)


class SourceCoverageTests(unittest.TestCase):
    temporary: tempfile.TemporaryDirectory[str]
    root: Path
    source_root: Path
    build_a: Path
    build_b: Path
    build_moved_manual_working_directory: Path
    build_moved_working_directory: Path
    build_renamed_targets: Path
    report_a: Path
    report_b: Path
    report_parallel: Path
    report_moved_manual_working_directory: Path
    report_moved_working_directory: Path
    report_renamed_targets: Path
    compiler: str
    llvm_cov: str
    llvm_profdata: str
    reconciler: Path

    @classmethod
    def setUpClass(cls) -> None:
        super().setUpClass()
        cls.temporary = tempfile.TemporaryDirectory(
            prefix="intrinsic-source-coverage-"
        )
        cls.root = Path(cls.temporary.name)

        missing = [
            tool
            for tool in ("cmake", "ctest", "ninja")
            if shutil.which(tool) is None
        ]
        llvm_tools = _matching_llvm_tools(cls.root)
        if missing or llvm_tools is None:
            cls.temporary.cleanup()
            detail = ", ".join(missing) if missing else "matching LLVM tools"
            raise unittest.SkipTest(
                f"requires CMake, Ninja, and matching Clang/LLVM "
                f"{MINIMUM_LLVM_MAJOR}+ tools; unavailable: {detail}"
            )
        if not RUN_COVERAGE.is_file() or not COMPARE_COVERAGE.is_file():
            cls.temporary.cleanup()
            raise AssertionError("CI-010 coverage entry points are missing")

        cls.compiler, cls.llvm_cov, cls.llvm_profdata = llvm_tools
        cls.source_root = cls.root / "fixture" / "project"
        _write(cls.source_root / "CMakeLists.txt", CMAKE_PROJECT)
        _write(cls.source_root / "src" / "alpha.cpp", ALPHA_SOURCE)
        _write(cls.source_root / "src" / "beta.cpp", BETA_SOURCE)
        _write(cls.source_root / "src" / "manual.cpp", MANUAL_SOURCE)
        _write(cls.source_root / "src" / "plain.cpp", PLAIN_SOURCE)
        _write(cls.source_root / "src" / "no_profile.cpp", NO_PROFILE_SOURCE)
        _write(cls.source_root / "src" / "liar.cpp", LIAR_SOURCE)
        (cls.source_root / "methods").mkdir(parents=True)
        _write(
            cls.source_root / "tests" / "fake_gtest.hpp",
            FAKE_GTEST_HEADER,
        )
        _write(cls.source_root / "tests" / "alpha_test.cpp", ALPHA_TEST)
        _write(cls.source_root / "tests" / "beta_test.cpp", BETA_TEST)
        _write(cls.source_root / "tests" / "manual_test.cpp", MANUAL_TEST)
        _write(cls.source_root / "tests" / "plain_test.cpp", PLAIN_TEST)
        _write(
            cls.source_root / "tests" / "no_profile_test.cpp",
            NO_PROFILE_TEST,
        )
        _write(cls.source_root / "tests" / "liar_test.cpp", LIAR_TEST)
        cls.reconciler = cls.root / "fake_reconciler.py"
        _write(cls.reconciler, FAKE_RECONCILER)

        cls.build_a = cls.root / "build-a"
        cls.build_b = cls.root / "different" / "build-b"
        cls.build_moved_manual_working_directory = (
            cls.root / "build-moved-manual-working-directory"
        )
        cls.build_moved_working_directory = cls.root / "build-moved-working-directory"
        cls.build_renamed_targets = cls.root / "build-renamed-targets"
        cls._configure_and_build(cls.build_a)
        cls._configure_and_build(cls.build_b)
        cls._configure_and_build(
            cls.build_moved_manual_working_directory,
            move_manual_test_working_directory=True,
        )
        cls._configure_and_build(
            cls.build_moved_working_directory,
            move_test_working_directory=True,
        )
        cls._configure_and_build(
            cls.build_renamed_targets,
            rename_test_targets=True,
        )
        cls.report_a = cls._collect(cls.build_a, "coverage-a", jobs=1)
        cls.report_b = cls._collect(cls.build_b, "coverage-b", jobs=1)
        cls.report_parallel = cls._collect(
            cls.build_a,
            "coverage-parallel",
            jobs=2,
        )
        cls.report_moved_manual_working_directory = cls._collect(
            cls.build_moved_manual_working_directory,
            "coverage-moved-manual-working-directory",
            jobs=1,
        )
        cls.report_moved_working_directory = cls._collect(
            cls.build_moved_working_directory,
            "coverage-moved-working-directory",
            jobs=1,
        )
        cls.report_renamed_targets = cls._collect(
            cls.build_renamed_targets,
            "coverage-renamed-targets",
            jobs=1,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "temporary"):
            cls.temporary.cleanup()
        super().tearDownClass()

    @classmethod
    def _configure_and_build(
        cls,
        build_dir: Path,
        *,
        include_uninstrumented: bool = False,
        include_no_profile: bool = False,
        include_xml_omission: bool = False,
        move_manual_test_working_directory: bool = False,
        move_test_working_directory: bool = False,
        rename_test_targets: bool = False,
    ) -> None:
        _run(
            [
                "cmake",
                "-S",
                str(cls.source_root),
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                f"-DCMAKE_CXX_COMPILER={cls.compiler}",
                "-DCMAKE_BUILD_TYPE=Debug",
                f"-DINCLUDE_UNINSTRUMENTED={'ON' if include_uninstrumented else 'OFF'}",
                f"-DINCLUDE_NO_PROFILE={'ON' if include_no_profile else 'OFF'}",
                f"-DINCLUDE_XML_OMISSION={'ON' if include_xml_omission else 'OFF'}",
                "-DMOVE_MANUAL_TEST_WORKING_DIRECTORY="
                f"{'ON' if move_manual_test_working_directory else 'OFF'}",
                "-DMOVE_TEST_WORKING_DIRECTORY="
                f"{'ON' if move_test_working_directory else 'OFF'}",
                f"-DRENAME_TEST_TARGETS={'ON' if rename_test_targets else 'OFF'}",
            ],
            cwd=cls.root,
        )
        _run(
            [
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                "IntrinsicCpuTests",
                "--parallel",
                "2",
            ],
            cwd=cls.root,
        )

    @classmethod
    def _coverage_command(
        cls,
        build_dir: Path,
        output: Path,
        *,
        cohort: str | None = None,
        jobs: int = 1,
        llvm_profdata: str | None = None,
    ) -> list[str]:
        command = [
            sys.executable,
            str(RUN_COVERAGE),
            "--build-dir",
            str(build_dir),
            "--output",
            str(output),
            "--preset",
            "synthetic-coverage",
            "--jobs",
            str(jobs),
            "--reconciler",
            str(cls.reconciler),
            "--llvm-cov",
            cls.llvm_cov,
            "--llvm-profdata",
            llvm_profdata or cls.llvm_profdata,
            "--repo-root",
            str(cls.source_root),
        ]
        if cohort is not None:
            command.extend(["--cohort", cohort])
        return command

    @classmethod
    def _collect(
        cls,
        build_dir: Path,
        name: str,
        *,
        cohort: str | None = None,
        jobs: int = 1,
    ) -> Path:
        output = cls.root / name
        shutil.rmtree(output, ignore_errors=True)
        _run(
            cls._coverage_command(
                build_dir,
                output,
                cohort=cohort,
                jobs=jobs,
            ),
            cwd=cls.root,
            timeout=180,
        )
        return output

    def _assert_collection_fails(
        self,
        build_dir: Path,
        name: str,
        pattern: str,
        *,
        llvm_profdata: str | None = None,
        reconciler: Path | None = None,
    ) -> str:
        output = self.root / name
        shutil.rmtree(output, ignore_errors=True)
        command = self._coverage_command(
            build_dir,
            output,
            llvm_profdata=llvm_profdata,
        )
        if reconciler is not None:
            command[command.index("--reconciler") + 1] = str(reconciler)
        result = _run(
            command,
            cwd=self.root,
            check=False,
            timeout=180,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(result.stdout, pattern)
        return result.stdout

    def _compare(
        self,
        baseline: Path,
        candidate: Path,
        *,
        check: bool,
    ) -> subprocess.CompletedProcess[str]:
        return _run(
            [
                sys.executable,
                str(COMPARE_COVERAGE),
                "--baseline",
                str(baseline),
                "--candidate",
                str(candidate),
                "--test-only-refactor",
            ],
            cwd=self.root,
            check=check,
        )

    def _write_candidate(self, name: str, report: dict[str, Any]) -> Path:
        path = self.root / f"{name}.json"
        path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return path

    @staticmethod
    def _case_working_directories(
        inventory: dict[str, Any],
    ) -> list[dict[str, str]]:
        records: list[dict[str, str]] = []
        for target in inventory["targets"]:
            if target["kind"] == "gtest":
                for case in target["cases"]:
                    if not case["disabled"]:
                        records.append(
                            {
                                "kind": "gtest",
                                "name": case["gtest_filter"],
                                "working_directory": target[
                                    "working_directory_identity"
                                ],
                            }
                        )
            else:
                for test in target["ctest_tests"]:
                    records.append(
                        {
                            "kind": "manual",
                            "name": test["ctest_name"],
                            "working_directory": test[
                                "working_directory_identity"
                            ],
                        }
                    )
        return sorted(records, key=lambda record: (record["kind"], record["name"]))

    @staticmethod
    def _refresh_inventory_digest(inventory: dict[str, Any]) -> None:
        inventory.pop("digest", None)
        inventory["digest"] = _json_digest(inventory)

    @classmethod
    def _refresh_transition_execution(
        cls,
        report: dict[str, Any],
        inventory: dict[str, Any],
    ) -> None:
        records = cls._case_working_directories(inventory)
        execution = report["identity"]["execution"]
        execution["aggregate"] = "IntrinsicCpuCoverageTests"
        execution["excluded_labels"] = [
            "benchmark",
            "flaky-quarantine",
            "gpu",
            "slo",
            "vulkan",
        ]
        execution["case_working_directory_digest"] = _json_digest(records)
        execution["case_working_directory_record_count"] = len(records)
        execution["test_inventory_digest"] = inventory["digest"]
        execution["working_directory_digest"] = _json_digest(
            {
                target["name"]: target["working_directory_identity"]
                for target in inventory["targets"]
                if target["kind"] == "gtest"
            }
        )

    def _write_transition_bundle(
        self,
        name: str,
        report: dict[str, Any],
        inventory: dict[str, Any],
    ) -> Path:
        output = self.root / name
        shutil.rmtree(output, ignore_errors=True)
        output.mkdir(parents=True)
        (output / "coverage.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (output / "test-inventory.json").write_text(
            json.dumps(inventory, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return output / "coverage.json"

    def _transition_fixture(
        self,
        name: str,
    ) -> tuple[Path, Path, Path]:
        baseline_report = copy.deepcopy(
            _load_json(self.report_a / "coverage.json")
        )
        candidate_report = copy.deepcopy(baseline_report)
        baseline_inventory = copy.deepcopy(
            _load_json(self.report_a / "test-inventory.json")
        )
        candidate_inventory = copy.deepcopy(baseline_inventory)

        baseline_inventory["aggregate"] = "IntrinsicCpuCoverageTests"
        candidate_inventory["aggregate"] = "IntrinsicCpuCoverageTests"
        moved_ctest_name = "Display/Alpha.CoversBothArms/2"
        sentinel_ctest_name = "Display/Alpha.SmallSentinel/2"
        for inventory in (baseline_inventory, candidate_inventory):
            inventory_alpha = next(
                target
                for target in inventory["targets"]
                if target["name"] == "AlphaTests"
            )
            inventory_alpha["cases"][0]["ctest_name"] = moved_ctest_name
        alpha = next(
            target
            for target in candidate_inventory["targets"]
            if target["name"] == "AlphaTests"
        )
        moved_case = copy.deepcopy(alpha["cases"][0])
        sentinel_case = copy.deepcopy(moved_case)
        sentinel_case["ctest_name"] = sentinel_ctest_name
        sentinel_case["gtest_filter"] = "Alpha.SmallSentinel"
        alpha["cases"] = [sentinel_case]

        slow_target = copy.deepcopy(alpha)
        slow_target["name"] = "AlphaSlowTests"
        slow_target["labels"] = ["core", "slow", "unit"]
        moved_case["labels"] = ["core", "slow", "unit"]
        slow_target["cases"] = [moved_case]
        candidate_inventory["targets"].append(slow_target)
        candidate_inventory["targets"].sort(key=lambda target: target["name"])
        candidate_inventory["summary"] = {
            "ctest_test_count": 4,
            "enabled_gtest_case_count": 3,
            "gtest_target_count": 3,
            "manual_ctest_test_count": 1,
            "manual_target_count": 1,
            "target_count": 4,
        }

        self._refresh_inventory_digest(baseline_inventory)
        self._refresh_inventory_digest(candidate_inventory)
        self._refresh_transition_execution(
            baseline_report,
            baseline_inventory,
        )
        self._refresh_transition_execution(
            candidate_report,
            candidate_inventory,
        )
        baseline = self._write_transition_bundle(
            f"{name}-baseline",
            baseline_report,
            baseline_inventory,
        )
        candidate = self._write_transition_bundle(
            f"{name}-candidate",
            candidate_report,
            candidate_inventory,
        )
        manifest = self.root / f"{name}-transition.json"
        manifest.write_text(
            json.dumps(
                {
                    "added_fast_sentinels": [sentinel_ctest_name],
                    "moved_to_slow": [moved_ctest_name],
                    "schema": "intrinsic.test-cohort-transition/v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        return baseline, candidate, manifest

    def _compare_transition(
        self,
        baseline: Path,
        candidate: Path,
        manifest: Path,
        *,
        check: bool,
    ) -> subprocess.CompletedProcess[str]:
        return _run(
            [
                sys.executable,
                str(COMPARE_COVERAGE),
                "--baseline",
                str(baseline),
                "--candidate",
                str(candidate),
                "--test-cohort-transition",
                str(manifest),
            ],
            cwd=self.root,
            check=check,
        )

    def test_parallel_collection_merges_every_producer_and_exact_case(self) -> None:
        output = self.report_parallel
        report = _load_json(output / "coverage.json")
        inventory = _load_json(output / "test-inventory.json")

        self.assertEqual(report["schema"], "intrinsic.cpu-source-coverage/v2")
        self.assertEqual(inventory["schema"], "intrinsic.cpu-test-inventory/v2")
        self.assertEqual(report["identity"]["execution"]["producer_jobs"], 2)
        self.assertEqual(inventory["aggregate"], "IntrinsicCpuTests")
        self.assertEqual(inventory["common_environment"], [])
        self.assertEqual(
            inventory["summary"],
            {
                "ctest_test_count": 3,
                "enabled_gtest_case_count": 2,
                "gtest_target_count": 2,
                "manual_ctest_test_count": 1,
                "manual_target_count": 1,
                "target_count": 3,
            },
        )
        self.assertEqual(
            report["identity"]["execution"]["case_working_directory_record_count"],
            3,
        )
        diagnostics = _load_json(output / "diagnostics.json")
        self.assertEqual(diagnostics["producer_jobs"], 2)

    def test_named_cpu_coverage_cohort_binds_aggregate_and_label_identity(
        self,
    ) -> None:
        output = self._collect(
            self.build_a,
            "coverage-named-cpu-cohort",
            cohort="cpu-coverage",
        )
        report = _load_json(output / "coverage.json")
        inventory = _load_json(output / "test-inventory.json")
        self.assertEqual(inventory["aggregate"], "IntrinsicCpuCoverageTests")
        self.assertEqual(
            report["identity"]["execution"]["aggregate"],
            "IntrinsicCpuCoverageTests",
        )
        self.assertEqual(
            report["identity"]["execution"]["excluded_labels"],
            [
                "benchmark",
                "flaky-quarantine",
                "gpu",
                "slo",
                "vulkan",
            ],
        )
        targets_by_name = {
            target["name"]: target for target in inventory["targets"]
        }
        self.assertEqual(
            set(targets_by_name),
            {"AlphaTests", "BetaTests", "ManualTests"},
        )
        self.assertEqual(
            {
                case["ctest_name"]
                for target in inventory["targets"]
                for case in target["cases"]
            },
            {"Alpha.CoversBothArms", "Beta.CoversBothArms"},
        )
        self.assertEqual(
            {
                case["gtest_filter"]
                for target in inventory["targets"]
                for case in target["cases"]
            },
            {"Alpha.CoversBothArms", "Beta.CoversBothArms"},
        )
        manual_target = next(
            target
            for target in inventory["targets"]
            if target["kind"] == "manual"
        )
        self.assertEqual(
            manual_target["ctest_tests"][0]["working_directory_identity"],
            "$BUILD",
        )
        for target in (
            targets_by_name["AlphaTests"],
            targets_by_name["BetaTests"],
        ):
            self.assertEqual(target["labels"], ["core", "unit"])
            self.assertEqual(target["ctest_tests"], [])
            self.assertEqual(target["cases"][0]["labels"], ["core", "unit"])
            self.assertFalse(target["cases"][0]["disabled"])
        manual = targets_by_name["ManualTests"]
        self.assertEqual(manual["kind"], "manual")
        self.assertEqual(manual["cases"], [])
        self.assertEqual(
            [test["ctest_name"] for test in manual["ctest_tests"]],
            ["Manual.Contract"],
        )

        objects = {Path(path).name for path in report["objects"]}
        self.assertEqual(objects, {"AlphaTests", "BetaTests", "ManualTests"})
        profiles = report["profiles"]
        self.assertEqual(
            {target["target"] for target in profiles["targets"]},
            {"AlphaTests", "BetaTests", "ManualTests"},
        )
        execution_profiles: list[str] = []
        probe_profiles: list[str] = []
        for target in profiles["targets"]:
            self.assertGreaterEqual(len(target["probe_raw_profiles"]), 1)
            self.assertGreaterEqual(len(target["execution_raw_profiles"]), 1)
            probe_profiles.extend(target["probe_raw_profiles"])
            execution_profiles.extend(target["execution_raw_profiles"])
        self.assertEqual(len(probe_profiles), len(set(probe_profiles)))
        self.assertEqual(len(execution_profiles), len(set(execution_profiles)))
        self.assertGreaterEqual(len(probe_profiles), 3)
        self.assertGreaterEqual(len(execution_profiles), 3)
        self.assertEqual(
            profiles["execution_raw_profile_count"],
            len(execution_profiles),
        )
        discovery_profiles = profiles["discovery_raw_profiles"]
        self.assertGreaterEqual(len(discovery_profiles), 1)
        self.assertEqual(
            profiles["discovery_raw_profile_count"],
            len(discovery_profiles),
        )
        self.assertTrue(
            all(
                path.startswith("discovery-profiles/")
                for path in discovery_profiles
            )
        )
        self.assertEqual(
            report["artifacts"]["gtest_results"],
            "gtest-results",
        )
        self.assertEqual(
            report["artifacts"]["discovery_profiles"],
            "discovery-profiles",
        )
        diagnostics = _load_json(output / "diagnostics.json")
        self.assertEqual(diagnostics["gtest_result_xml_count"], 2)
        self.assertEqual(diagnostics["producer_jobs"], 1)
        self.assertEqual(
            diagnostics["discovery_raw_profile_count"],
            len(discovery_profiles),
        )
        for target in profiles["targets"]:
            result_path = target["gtest_result_xml"]
            if target["kind"] == "manual":
                self.assertIsNone(result_path)
                continue
            self.assertIsInstance(result_path, str)
            result = output / result_path
            self.assertTrue(result.is_file(), result)
            cases = {
                f"{case.attrib['classname']}.{case.attrib['name']}"
                for case in ET.parse(result).iterfind(".//testcase")
            }
            expected = {
                "AlphaTests": "Alpha.CoversBothArms",
                "BetaTests": "Beta.CoversBothArms",
            }
            self.assertEqual(cases, {expected[target["target"]]})
        self.assertTrue((output / "merged" / "coverage.profdata").is_file())
        self.assertTrue((output / "llvm-cov-export.json").is_file())
        self.assertTrue((output / "diagnostics.json").is_file())

    def test_serial_collection_binds_inventory_and_reports_protocol(self) -> None:
        report = _load_json(self.report_a / "coverage.json")
        inventory = _load_json(self.report_a / "test-inventory.json")
        diagnostics = _load_json(self.report_a / "diagnostics.json")
        execution = report["identity"]["execution"]

        self.assertEqual(
            execution["schema"],
            "intrinsic.cpu-source-coverage-execution/v3",
        )
        self.assertEqual(execution["producer_jobs"], 1)
        self.assertEqual(diagnostics["producer_jobs"], 1)
        self.assertEqual(execution["test_inventory_digest"], inventory["digest"])
        self.assertEqual(report["identity"]["build"]["profile_update"], "atomic")

    def test_non_atomic_coverage_build_identity_fails_closed(self) -> None:
        cache_path = self.build_a / "CMakeCache.txt"
        original = cache_path.read_text(encoding="utf-8")
        modified, replacements = re.subn(
            r"(?m)^INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE:[^=]*=atomic$",
            "INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE:INTERNAL=single",
            original,
        )
        self.assertEqual(replacements, 1)
        cache_path.write_text(modified, encoding="utf-8")
        try:
            self._assert_collection_fails(
                self.build_a,
                "failure-non-atomic-profile-update",
                r"(?is)INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE.*atomic.*single",
            )
        finally:
            cache_path.write_text(original, encoding="utf-8")

    def test_non_atomic_production_compile_command_fails_closed(self) -> None:
        commands_path = self.build_a / "compile_commands.json"
        original = commands_path.read_text(encoding="utf-8")
        commands = json.loads(original)
        record = next(
            command
            for command in commands
            if str(command["file"]).endswith("src/alpha.cpp")
        )
        if "arguments" in record:
            record["arguments"].remove("-fprofile-update=atomic")
        else:
            record["command"] = record["command"].replace(
                " -fprofile-update=atomic",
                "",
                1,
            )
        commands_path.write_text(
            json.dumps(commands, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            self._assert_collection_fails(
                self.build_a,
                "failure-non-atomic-compile-command",
                r"(?is)atomic profile updates.*src/alpha\.cpp",
            )
        finally:
            commands_path.write_text(original, encoding="utf-8")

    def test_conflicting_profile_update_override_fails_before_discovery(
        self,
    ) -> None:
        commands_path = self.build_a / "compile_commands.json"
        original = commands_path.read_text(encoding="utf-8")
        commands = json.loads(original)
        record = next(
            command
            for command in commands
            if str(command["file"]).endswith("src/alpha.cpp")
        )
        if "arguments" in record:
            record["arguments"].append("-fprofile-update=single")
        else:
            record["command"] += " -fprofile-update=single"
        commands_path.write_text(
            json.dumps(commands, indent=2) + "\n",
            encoding="utf-8",
        )
        output_name = "failure-conflicting-profile-update"
        try:
            self._assert_collection_fails(
                self.build_a,
                output_name,
                r"(?is)only atomic profile updates.*alpha\.cpp.*atomic.*single",
                reconciler=self.root / "missing-reconciler.py",
            )
            self.assertFalse((self.root / output_name).exists())
        finally:
            commands_path.write_text(original, encoding="utf-8")

    def test_saturated_execution_counter_fails_closed(self) -> None:
        raw_export = _load_json(self.report_a / "llvm-cov-export.json")
        production_file = next(
            raw_file
            for raw_file in raw_export["data"][0]["files"]
            if str(raw_file["filename"]).endswith("src/alpha.cpp")
        )
        self.assertTrue(production_file["branches"])
        production_file["branches"][0][4] = (1 << 63) - 1
        with self.assertRaisesRegex(
            CoverageError,
            r"(?is)saturated.*atomic profile",
        ):
            normalize_llvm_cov_export(raw_export, self.source_root)

    def test_reports_are_path_normalized_and_equal_across_build_directories(
        self,
    ) -> None:
        baseline = _load_json(self.report_a / "coverage.json")
        candidate = _load_json(self.report_b / "coverage.json")
        self._compare(
            self.report_a / "coverage.json",
            self.report_b / "coverage.json",
            check=True,
        )
        baseline_identity = copy.deepcopy(baseline["identity"])
        candidate_identity = copy.deepcopy(candidate["identity"])
        baseline_inventory_digest = baseline_identity["execution"].pop(
            "test_inventory_digest"
        )
        candidate_inventory_digest = candidate_identity["execution"].pop(
            "test_inventory_digest"
        )
        self.assertNotEqual(
            baseline_inventory_digest,
            candidate_inventory_digest,
        )
        self.assertEqual(baseline_identity, candidate_identity)

        for key in (
            "covered_lines",
            "covered_functions",
            "covered_regions",
            "covered_branch_arms",
        ):
            self.assertEqual(_covered_set(baseline, key), _covered_set(candidate, key))

        forbidden = (str(self.root), str(self.build_a), str(self.build_b))
        normalized_identity = "\n".join(
            _recursive_strings(
                {
                    "coverage": {
                        key: baseline["coverage"][key]
                        for key in (
                            "covered_lines",
                            "covered_functions",
                            "covered_regions",
                            "covered_branch_arms",
                        )
                    },
                }
            )
        )
        for path in forbidden:
            self.assertNotIn(path, normalized_identity)
        self.assertTrue(
            all(
                identity.startswith(("src/", "methods/"))
                for key in (
                    "covered_lines",
                    "covered_functions",
                    "covered_regions",
                    "covered_branch_arms",
                )
                for identity in _covered_set(baseline, key)
            )
        )

    def test_missing_registered_binary_fails_closed(self) -> None:
        binary = self.build_a / "bin" / "BetaTests"
        hidden = binary.with_suffix(".hidden")
        binary.rename(hidden)
        try:
            self._assert_collection_fails(
                self.build_a,
                "failure-missing-binary",
                r"(?is)BetaTests.*(?:requires exactly one|missing|executable)|"
                r"(?:requires exactly one|missing).*BetaTests",
            )
        finally:
            hidden.rename(binary)

    def test_uninstrumented_registered_binary_fails_closed(self) -> None:
        build_dir = self.root / "build-uninstrumented"
        self._configure_and_build(build_dir, include_uninstrumented=True)
        self._assert_collection_fails(
            build_dir,
            "failure-uninstrumented",
            r"(?is)PlainTests.*(?:instrument|coverage|profile)|"
            r"(?:instrument|coverage|profile).*(?:PlainTests|src/plain\.cpp)",
        )

    def test_instrumented_binary_without_profile_fails_closed(self) -> None:
        build_dir = self.root / "build-no-profile"
        self._configure_and_build(build_dir, include_no_profile=True)
        self._assert_collection_fails(
            build_dir,
            "failure-no-profile",
            r"(?is)NoProfileTests.*(?:missing|no|zero).*prof|"
            r"(?:missing|no|zero).*prof.*NoProfileTests",
        )

    def test_execution_xml_omitting_requested_case_fails_closed(self) -> None:
        build_dir = self.root / "build-xml-omission"
        self._configure_and_build(build_dir, include_xml_omission=True)
        output_name = "failure-xml-omission"
        output = self.root / output_name
        message = self._assert_collection_fails(
            build_dir,
            output_name,
            r"(?is)LiarTests.*GoogleTest XML execution inventory mismatch.*"
            r"missing=.*Liar\.Requested",
        )
        self.assertIn("extra=[]", message)
        raw_profiles = list((output / "raw" / "LiarTests").glob("*.profraw"))
        self.assertTrue(raw_profiles, "lying producer did not emit a raw profile")
        self.assertTrue(all(path.stat().st_size > 0 for path in raw_profiles))
        result = output / "gtest-results" / "LiarTests.xml"
        self.assertTrue(result.is_file(), result)
        self.assertEqual(list(ET.parse(result).iterfind(".//testcase")), [])

    def test_omitted_cpu_aggregate_member_fails_closed(self) -> None:
        aggregate = (
            self.build_a / "test-inventories" / "IntrinsicCpuTests.txt"
        )
        original = aggregate.read_text(encoding="utf-8")
        aggregate.write_text("AlphaTests\n", encoding="utf-8")
        try:
            self._assert_collection_fails(
                self.build_a,
                "failure-omitted-aggregate-member",
                r"(?is)BetaTests.*(?:omit|missing|aggregate|select)|"
                r"(?:omit|missing|aggregate|select).*BetaTests",
            )
        finally:
            aggregate.write_text(original, encoding="utf-8")

    def test_zero_selected_cases_fails_closed(self) -> None:
        ctest_file = self.build_a / "CTestTestfile.cmake"
        original = ctest_file.read_text(encoding="utf-8")
        ctest_file.write_text(
            "# Deliberately empty for the zero-selection regression.\n",
            encoding="utf-8",
        )
        try:
            self._assert_collection_fails(
                self.build_a,
                "failure-zero-cases",
                r"(?is)(?:zero|no) (?:selected |enabled )?(?:CTest |test )?cases|"
                r"(?:selected|mapped).*0|no mapped CTest registrations",
            )
        finally:
            ctest_file.write_text(original, encoding="utf-8")

    def test_corrupt_raw_profile_merge_fails_closed(self) -> None:
        wrapper = self.root / "corrupt-profdata.py"
        _write(
            wrapper,
            f"""
            #!{sys.executable}
            import os
            from pathlib import Path
            import sys

            raw_profiles = [
                Path(argument)
                for argument in sys.argv[1:]
                if argument.endswith(".profraw")
            ]
            if raw_profiles:
                raw_profiles[0].write_bytes(b"corrupt profile payload")
            os.execv(
                {self.llvm_profdata!r},
                [{self.llvm_profdata!r}, *sys.argv[1:]],
            )
            """,
        )
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
        self._assert_collection_fails(
            self.build_a,
            "failure-corrupt-profile",
            r"(?is)(?:corrupt|malformed|invalid).*prof|"
            r"prof.*(?:corrupt|malformed|invalid)",
            llvm_profdata=str(wrapper),
        )

    def test_production_digest_drift_fails_refactor_parity(self) -> None:
        report = _load_json(self.report_b / "coverage.json")
        self.assertIn("digest", report["identity"]["production"])
        report["identity"]["production"]["digest"] = "0" * 64
        candidate = self._write_candidate("production-drift", report)
        result = self._compare(
            self.report_a / "coverage.json",
            candidate,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)production.*(?:digest|drift|differ)|"
            r"(?:digest|drift|differ).*production",
        )

    def test_production_build_input_drift_fails_refactor_parity(self) -> None:
        report = _load_json(self.report_b / "coverage.json")
        build_inputs = report["identity"]["production_build_inputs"]
        self.assertIn("digest", build_inputs)
        build_inputs["digest"] = "0" * 64
        candidate = self._write_candidate("production-build-input-drift", report)
        result = self._compare(
            self.report_a / "coverage.json",
            candidate,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)test-only refactor identity mismatch.*"
            r"production_build_inputs.*digest",
        )

    def test_test_target_topology_drift_preserves_refactor_parity(self) -> None:
        baseline = _load_json(self.report_a / "coverage.json")
        candidate = _load_json(self.report_renamed_targets / "coverage.json")
        baseline_inventory = _load_json(self.report_a / "test-inventory.json")
        candidate_inventory = _load_json(
            self.report_renamed_targets / "test-inventory.json"
        )
        baseline_targets = {
            target["name"] for target in baseline_inventory["targets"]
        }
        candidate_targets = {
            target["name"] for target in candidate_inventory["targets"]
        }
        self.assertNotEqual(baseline_targets, candidate_targets)
        self.assertNotEqual(
            baseline["identity"]["execution"]["working_directory_digest"],
            candidate["identity"]["execution"]["working_directory_digest"],
        )
        self.assertEqual(
            baseline["identity"]["execution"]["case_working_directory_digest"],
            candidate["identity"]["execution"]["case_working_directory_digest"],
        )
        result = self._compare(
            self.report_a / "coverage.json",
            self.report_renamed_targets / "coverage.json",
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("lost_regions=0 lost_branch_arms=0", result.stdout)

    def test_test_working_directory_drift_fails_refactor_parity(self) -> None:
        result = self._compare(
            self.report_a / "coverage.json",
            self.report_moved_working_directory / "coverage.json",
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)test-only refactor identity mismatch.*"
            r"execution.*case_working_directory_digest",
        )

    def test_manual_working_directory_drift_fails_refactor_parity(self) -> None:
        result = self._compare(
            self.report_a / "coverage.json",
            self.report_moved_manual_working_directory / "coverage.json",
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)test-only refactor identity mismatch.*"
            r"execution.*case_working_directory_digest",
        )

    def test_missing_case_working_directory_identity_fails_closed(self) -> None:
        report = _load_json(self.report_b / "coverage.json")
        del report["identity"]["execution"]["case_working_directory_digest"]
        candidate = self._write_candidate(
            "missing-case-working-directory-identity",
            report,
        )
        result = self._compare(
            self.report_a / "coverage.json",
            candidate,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)execution identity omits.*case_working_directory_digest",
        )

    def test_execution_environment_drift_fails_refactor_parity(self) -> None:
        report = _load_json(self.report_b / "coverage.json")
        execution = report["identity"]["execution"]
        self.assertIn("common_ctest_environment_digest", execution)
        execution["common_ctest_environment_digest"] = "0" * 64
        candidate = self._write_candidate("execution-environment-drift", report)
        result = self._compare(
            self.report_a / "coverage.json",
            candidate,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)test-only refactor identity mismatch.*"
            r"execution.*common_ctest_environment_digest",
        )

    def test_declared_test_cohort_transition_preserves_source_coverage(self) -> None:
        baseline, candidate, manifest = self._transition_fixture(
            "valid-cohort-transition"
        )
        result = self._compare_transition(
            baseline,
            candidate,
            manifest,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("lost_regions=0 lost_branch_arms=0", result.stdout)

    def test_test_cohort_transition_rejects_undeclared_population_change(
        self,
    ) -> None:
        baseline, candidate, manifest = self._transition_fixture(
            "undeclared-cohort-addition"
        )
        transition = _load_json(manifest)
        transition["added_fast_sentinels"] = []
        manifest.write_text(
            json.dumps(transition, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        result = self._compare_transition(
            baseline,
            candidate,
            manifest,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)(?:population|sentinel|addition|extra|undeclared)",
        )

    def test_test_cohort_transition_inventory_proof_fails_closed(self) -> None:
        for scenario in ("digest", "ctest-name-binding", "report-binding"):
            with self.subTest(scenario=scenario):
                baseline, candidate, manifest = self._transition_fixture(
                    f"cohort-inventory-{scenario}"
                )
                if scenario == "digest":
                    inventory_path = candidate.parent / "test-inventory.json"
                    inventory = _load_json(inventory_path)
                    inventory["digest"] = "0" * 64
                    inventory_path.write_text(
                        json.dumps(inventory, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                elif scenario == "ctest-name-binding":
                    inventory_path = candidate.parent / "test-inventory.json"
                    inventory = _load_json(inventory_path)
                    inventory["targets"][0]["cases"][0]["ctest_name"] = (
                        "Renamed.WithoutReportRebind"
                    )
                    self._refresh_inventory_digest(inventory)
                    inventory_path.write_text(
                        json.dumps(inventory, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                else:
                    report = _load_json(candidate)
                    report["identity"]["execution"][
                        "case_working_directory_record_count"
                    ] += 1
                    candidate.write_text(
                        json.dumps(report, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                result = self._compare_transition(
                    baseline,
                    candidate,
                    manifest,
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertRegex(
                    result.stdout,
                    r"(?is)(?:inventory|digest|binding|record_count|case count)",
                )

    def test_test_cohort_transition_rejects_parallel_producers(self) -> None:
        baseline, candidate, manifest = self._transition_fixture(
            "cohort-parallel-producers"
        )
        report = _load_json(candidate)
        report["identity"]["execution"]["producer_jobs"] = 2
        candidate.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        result = self._compare_transition(
            baseline,
            candidate,
            manifest,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(
            result.stdout,
            r"(?is)(?:serialize|producer_jobs=1|parallel)",
        )

    def test_test_cohort_transition_rejects_common_working_directory_drift(
        self,
    ) -> None:
        baseline, candidate, manifest = self._transition_fixture(
            "cohort-working-directory-drift"
        )
        inventory_path = candidate.parent / "test-inventory.json"
        inventory = _load_json(inventory_path)
        moved_target = next(
            target
            for target in inventory["targets"]
            if target["name"] == "AlphaSlowTests"
        )
        moved_target["working_directory_identity"] = "$BUILD/moved"
        self._refresh_inventory_digest(inventory)
        report = _load_json(candidate)
        self._refresh_transition_execution(report, inventory)
        self._write_transition_bundle(
            candidate.parent.name,
            report,
            inventory,
        )
        result = self._compare_transition(
            baseline,
            candidate,
            manifest,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(result.stdout, r"(?is)working.?director")

    def test_test_cohort_transition_rejects_non_slow_label_drift(self) -> None:
        for scenario, labels in (
            ("missing-slow", ["core", "unit"]),
            ("extra-label", ["core", "regression", "slow", "unit"]),
        ):
            with self.subTest(scenario=scenario):
                baseline, candidate, manifest = self._transition_fixture(
                    f"cohort-label-drift-{scenario}"
                )
                inventory_path = candidate.parent / "test-inventory.json"
                inventory = _load_json(inventory_path)
                moved_target = next(
                    target
                    for target in inventory["targets"]
                    if target["name"] == "AlphaSlowTests"
                )
                moved_target["labels"] = labels
                moved_target["cases"][0]["labels"] = labels
                self._refresh_inventory_digest(inventory)
                inventory_path.write_text(
                    json.dumps(inventory, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                report = _load_json(candidate)
                report["identity"]["execution"]["test_inventory_digest"] = (
                    inventory["digest"]
                )
                candidate.write_text(
                    json.dumps(report, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                result = self._compare_transition(
                    baseline,
                    candidate,
                    manifest,
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertRegex(result.stdout, r"(?is)label")

    def test_test_cohort_transition_rejects_covered_region_loss(self) -> None:
        baseline, candidate, manifest = self._transition_fixture(
            "cohort-region-loss"
        )
        report = _load_json(candidate)
        regions = _covered_set(report, "covered_regions")
        self.assertTrue(regions)
        lost = regions.pop()
        candidate.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        result = self._compare_transition(
            baseline,
            candidate,
            manifest,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(lost, result.stdout)
        self.assertRegex(result.stdout, r"(?is)lost.*region|region.*lost")

    def test_lost_covered_region_fails_despite_rising_total(self) -> None:
        report = _load_json(self.report_b / "coverage.json")
        regions = _covered_set(report, "covered_regions")
        self.assertGreaterEqual(len(regions), 2)
        lost = regions.pop()
        self._inflate_aggregate_totals(report)
        candidate = self._write_candidate("lost-region", report)
        result = self._compare(
            self.report_a / "coverage.json",
            candidate,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(lost, result.stdout)
        self.assertRegex(result.stdout, r"(?i)lost.*region|region.*lost")

    def test_each_lost_branch_arm_fails_despite_rising_total(self) -> None:
        baseline = _load_json(self.report_b / "coverage.json")
        branches = _covered_set(baseline, "covered_branch_arms")
        for arm in ("true", "false"):
            matching = [identity for identity in branches if identity.endswith(f":{arm}")]
            self.assertTrue(matching, f"fixture did not cover a {arm} branch arm")
            report = copy.deepcopy(baseline)
            report["coverage"]["covered_branch_arms"].remove(matching[0])
            self._inflate_aggregate_totals(report)
            candidate = self._write_candidate(f"lost-{arm}-branch", report)
            with self.subTest(arm=arm):
                result = self._compare(
                    self.report_a / "coverage.json",
                    candidate,
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(matching[0], result.stdout)
                self.assertRegex(
                    result.stdout,
                    r"(?i)lost.*branch|branch.*lost",
                )

    @staticmethod
    def _inflate_aggregate_totals(report: dict[str, Any]) -> None:
        def inflate(value: Any) -> None:
            if isinstance(value, dict):
                if {"count", "covered", "percent"}.issubset(value):
                    value["covered"] = value["count"]
                    value["percent"] = 100.0
                for child in value.values():
                    inflate(child)
            elif isinstance(value, list):
                for child in value:
                    inflate(child)

        inflate(report["coverage"].get("raw_totals", {}))


if __name__ == "__main__":
    unittest.main(verbosity=2)
