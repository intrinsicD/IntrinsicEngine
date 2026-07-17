#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from collections import Counter
from pathlib import Path
from typing import Mapping, Sequence

from source_coverage import (
    COVERAGE_COHORTS,
    COVERAGE_SCHEMA,
    CoverageError,
    EXECUTION_IDENTITY_SCHEMA,
    changed_line_coverage,
    exclusion_identity,
    load_cpu_test_inventory,
    normalize_llvm_cov_export,
    production_build_input_digest,
    production_source_digest,
    read_cmake_cache,
    semantic_compile_command_digest,
    write_json,
)


DIAGNOSTICS_SCHEMA = "intrinsic.cpu-source-coverage-diagnostics/v1"
_VERSION_RE = re.compile(r"\b(?:clang|LLVM) version\s+(?P<major>\d+)", re.I)
_GTEST_SUITE_RE = re.compile(r"^(?P<suite>\S+)\.\s*(?:#.*)?$")
_GTEST_COMMENT_RE = re.compile(r"\s+#.*$")


def _bounded_default_jobs() -> int:
    return max(1, min(4, os.cpu_count() or 1))


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect fail-closed Clang source coverage for a declared CPU "
            "test aggregate and label identity."
        )
    )
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--preset", default="ci-coverage-cpu")
    parser.add_argument(
        "--cohort",
        choices=tuple(COVERAGE_COHORTS),
        default="cpu",
        help="named aggregate/label identity to collect (default: cpu)",
    )
    parser.add_argument("--jobs", type=int, default=_bounded_default_jobs())
    parser.add_argument("--diff-base")
    parser.add_argument("--reconciler", type=Path)
    parser.add_argument("--llvm-cov", type=Path)
    parser.add_argument("--llvm-profdata", type=Path)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    arguments = parser.parse_args(argv)
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    if not arguments.preset:
        parser.error("--preset must not be empty")
    return arguments


def _prepare_output(path: Path) -> Path:
    path = path.resolve()
    if path.exists():
        if not path.is_dir():
            raise CoverageError(f"coverage output is not a directory: {path}")
        if any(path.iterdir()):
            raise CoverageError(
                f"coverage output must be absent or empty, got nonempty {path}"
            )
    else:
        path.mkdir(parents=True)
    return path


def _cache_bool(cache: Mapping[str, str], key: str) -> bool | None:
    value = cache.get(key)
    if value is None:
        return None
    normalized = value.upper()
    if normalized in {"1", "ON", "TRUE", "YES"}:
        return True
    if normalized in {"0", "OFF", "FALSE", "NO"}:
        return False
    raise CoverageError(f"CMake cache entry {key} is not Boolean: {value!r}")


def _require_coverage_build(cache: Mapping[str, str]) -> dict[str, object]:
    expected = {
        "BUILD_TESTING": True,
        "INTRINSIC_BUILD_BENCHMARKS": False,
        "INTRINSIC_BUILD_SANDBOX": False,
        "INTRINSIC_BUILD_TESTS": True,
        "INTRINSIC_ENABLE_CUDA": False,
        "INTRINSIC_ENABLE_SANITIZERS": False,
        "INTRINSIC_ENABLE_SOURCE_COVERAGE": True,
        "INTRINSIC_HEADLESS_NO_GLFW": False,
        "INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN": False,
    }
    actual: dict[str, bool] = {}
    for key, expected_value in expected.items():
        value = _cache_bool(cache, key)
        if value is None:
            raise CoverageError(f"coverage build omits required CMake cache key {key}")
        if value != expected_value:
            raise CoverageError(
                f"coverage build requires {key}={str(expected_value).upper()}, "
                f"got {cache[key]!r}"
            )
        actual[key] = value
    expected_strings = {
        "CMAKE_BUILD_TYPE": "Debug",
        "EXTRINSIC_BACKEND": "Vulkan",
        "EXTRINSIC_PLATFORM": "Linux",
        "INTRINSIC_PLATFORM_BACKEND": "Glfw",
        "INTRINSIC_PLATFORM_BACKEND_SELECTED": "Glfw",
        "INTRINSIC_SANITIZER_IDENTITY": "none",
    }
    for key, expected_value in expected_strings.items():
        value = cache.get(key)
        if value != expected_value:
            raise CoverageError(
                f"coverage build requires {key}={expected_value!r}, got {value!r}"
            )
    return {
        "build_type": expected_strings["CMAKE_BUILD_TYPE"],
        "options": actual,
    }


def _version_identity(executable: Path, kind: str) -> dict[str, object]:
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise CoverageError(f"{kind} is not executable: {executable}")
    try:
        result = subprocess.run(
            [str(executable), "--version"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise CoverageError(f"cannot inspect {kind} {executable}: {error}") from error
    if result.returncode != 0:
        raise CoverageError(
            f"{kind} --version failed with exit {result.returncode}:\n{result.stdout}"
        )
    match = _VERSION_RE.search(result.stdout)
    if not match:
        raise CoverageError(f"cannot determine {kind} major from:\n{result.stdout}")
    return {
        "major": int(match.group("major")),
        "path": str(executable.resolve()),
        "version": result.stdout.strip(),
    }


def _resolve_tool(
    name: str, explicit: Path | None, compiler_major: int
) -> tuple[Path, dict[str, object]]:
    if explicit is not None:
        path = explicit.resolve()
    else:
        discovered = shutil.which(f"{name}-{compiler_major}")
        if discovered is None:
            discovered = shutil.which(name)
        if discovered is None:
            raise CoverageError(
                f"cannot find {name}-{compiler_major}; install matching LLVM tools "
                f"or pass --{name.replace('_', '-')}"
            )
        path = Path(discovered).resolve()
    identity = _version_identity(path, name)
    if identity["major"] != compiler_major:
        raise CoverageError(
            f"{name} major {identity['major']} does not match compiler major "
            f"{compiler_major}: {path}"
        )
    return path, identity


def _compiler_and_tools(
    cache: Mapping[str, str],
    llvm_cov_argument: Path | None,
    llvm_profdata_argument: Path | None,
) -> tuple[dict[str, object], Path, Path, dict[str, object]]:
    compiler_value = cache.get("CMAKE_CXX_COMPILER")
    if not compiler_value:
        raise CoverageError("CMake cache omits CMAKE_CXX_COMPILER")
    compiler = _version_identity(Path(compiler_value).resolve(), "Clang compiler")
    if compiler["major"] < 20:
        raise CoverageError(
            f"source coverage requires Clang 20 or newer, got {compiler['major']}"
        )
    llvm_cov, llvm_cov_identity = _resolve_tool(
        "llvm-cov", llvm_cov_argument, int(compiler["major"])
    )
    llvm_profdata, llvm_profdata_identity = _resolve_tool(
        "llvm-profdata", llvm_profdata_argument, int(compiler["major"])
    )
    tools = {
        "llvm_cov": llvm_cov_identity,
        "llvm_profdata": llvm_profdata_identity,
    }
    return compiler, llvm_cov, llvm_profdata, tools


def _safe_target_names(targets: Sequence[Mapping[str, object]]) -> dict[str, str]:
    result: dict[str, str] = {}
    owners: dict[str, str] = {}
    for target in targets:
        name = str(target["name"])
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        if not safe:
            raise CoverageError(f"cannot derive profile prefix for target {name!r}")
        previous = owners.get(safe)
        if previous is not None:
            raise CoverageError(
                f"profile prefix collision between {previous!r} and {name!r}"
            )
        owners[safe] = name
        result[name] = safe
    return result


def _environment(entries: Sequence[object]) -> dict[str, str]:
    environment = os.environ.copy()
    for key in (
        "GTEST_FILTER",
        "GTEST_OUTPUT",
        "GTEST_SHARD_INDEX",
        "GTEST_SHARD_STATUS_FILE",
        "GTEST_TOTAL_SHARDS",
    ):
        environment.pop(key, None)
    for raw_entry in entries:
        if not isinstance(raw_entry, str) or "=" not in raw_entry:
            raise CoverageError(f"invalid common CTest environment {raw_entry!r}")
        key, _separator, value = raw_entry.partition("=")
        environment[key] = value
    environment["GTEST_COLOR"] = "no"
    return environment


def _run_logged(
    command: Sequence[str],
    log_path: Path,
    *,
    environment: Mapping[str, str],
    cwd: Path | None = None,
    timeout: int = 900,
) -> None:
    try:
        result = subprocess.run(
            list(command),
            cwd=cwd,
            env=dict(environment),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        partial_output = error.stdout or ""
        if isinstance(partial_output, bytes):
            partial_output = partial_output.decode("utf-8", errors="replace")
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(
            f"$ {shlex.join(command)}\n{partial_output}"
            f"\nTIMEOUT after {timeout} seconds\n",
            encoding="utf-8",
        )
        raise CoverageError(
            f"command timed out after {timeout} seconds; see {log_path}: "
            f"{shlex.join(command)}"
        ) from error
    except (OSError, subprocess.SubprocessError) as error:
        raise CoverageError(f"cannot run {shlex.join(command)}: {error}") from error
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(f"$ {shlex.join(command)}\n{result.stdout}", encoding="utf-8")
    if result.returncode != 0:
        raise CoverageError(
            f"command failed with exit {result.returncode}; see {log_path}: "
            f"{shlex.join(command)}"
        )


def _parse_gtest_listing(output: str, target: str) -> tuple[str, ...]:
    suite: str | None = None
    cases: list[str] = []
    for line_number, line in enumerate(output.splitlines(), start=1):
        if not line.strip():
            continue
        if line.startswith("  "):
            if suite is None:
                raise CoverageError(
                    f"{target}: GoogleTest case precedes suite at line {line_number}"
                )
            case = _GTEST_COMMENT_RE.sub("", line.strip())
            if not case or any(character.isspace() for character in case):
                raise CoverageError(
                    f"{target}: malformed GoogleTest case at line {line_number}"
                )
            cases.append(f"{suite}.{case}")
            continue
        match = _GTEST_SUITE_RE.fullmatch(line)
        if match:
            suite = match.group("suite")
    if not cases:
        raise CoverageError(f"{target}: --gtest_list_tests selected zero cases")
    duplicates = sorted(case for case, count in Counter(cases).items() if count > 1)
    if duplicates:
        raise CoverageError(f"{target}: duplicate listed cases {duplicates!r}")
    return tuple(cases)


def _profiles_for(target: str, directory: Path) -> list[Path]:
    profiles = sorted(directory.glob("*.profraw"))
    if not profiles:
        raise CoverageError(
            f"target {target!r} emitted no raw profile shards under {directory}"
        )
    empty = [path for path in profiles if path.stat().st_size == 0]
    if empty:
        raise CoverageError(
            f"target {target!r} emitted empty raw profiles: "
            f"{[str(path) for path in empty]!r}"
        )
    return profiles


def _probe_gtest_target(
    target: Mapping[str, object],
    *,
    prefix: str,
    output: Path,
    common_environment: Mapping[str, str],
) -> list[Path]:
    executable = Path(str(target["executable"]))
    probe_raw_dir = output / "probes/raw" / prefix
    probe_raw_dir.mkdir(parents=True, exist_ok=True)
    profile_pattern = probe_raw_dir / "%m-%p.profraw"
    environment = dict(common_environment)
    environment["LLVM_PROFILE_FILE"] = str(profile_pattern)
    log_path = output / "logs" / f"probe-{prefix}.log"
    _run_logged(
        [str(executable), "--gtest_list_tests"],
        log_path,
        environment=environment,
        cwd=Path(str(target["working_directory"])),
    )
    output_text = log_path.read_text(encoding="utf-8").split("\n", 1)[-1]
    listed = frozenset(_parse_gtest_listing(output_text, str(target["name"])))
    expected = frozenset(
        str(case["gtest_filter"])
        for case in target["cases"]  # type: ignore[index]
    )
    if listed != expected:
        raise CoverageError(
            f"{target['name']}: probe/list differs from exact CTest inventory; "
            f"missing={sorted(expected - listed)!r}, "
            f"extra={sorted(listed - expected)!r}"
        )
    return _profiles_for(str(target["name"]), probe_raw_dir)


def _parse_gtest_execution_xml(
    path: Path, target: str, expected_cases: Sequence[str]
) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise CoverageError(
            f"{target}: GoogleTest execution did not create nonempty XML {path}"
        )
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise CoverageError(
            f"{target}: GoogleTest execution XML is malformed: {error}"
        ) from error
    if root.tag != "testsuites":
        raise CoverageError(
            f"{target}: GoogleTest execution XML root must be 'testsuites', "
            f"got {root.tag!r}"
        )

    actual: list[str] = []
    for testcase in root.iter("testcase"):
        classname = testcase.get("classname")
        name = testcase.get("name")
        if not classname or not name:
            raise CoverageError(
                f"{target}: GoogleTest XML testcase requires classname and name"
            )
        status = testcase.get("status")
        result = testcase.get("result")
        if status != "run" or result not in {"completed", "skipped"}:
            raise CoverageError(
                f"{target}: GoogleTest XML testcase {classname}.{name} was not "
                f"executed: status={status!r}, result={result!r}"
            )
        actual.append(f"{classname}.{name}")
    duplicates = sorted(case for case, count in Counter(actual).items() if count > 1)
    if duplicates:
        raise CoverageError(
            f"{target}: GoogleTest XML repeats testcase identities {duplicates!r}"
        )
    actual_cases = frozenset(actual)
    expected = frozenset(expected_cases)
    missing = sorted(expected - actual_cases)
    extra = sorted(actual_cases - expected)
    if missing or extra:
        raise CoverageError(
            f"{target}: GoogleTest XML execution inventory mismatch; "
            f"missing={missing!r}, extra={extra!r}"
        )


def _execute_gtest_target(
    target: Mapping[str, object],
    *,
    prefix: str,
    output: Path,
    common_environment: Mapping[str, str],
) -> tuple[list[Path], Path]:
    executable = Path(str(target["executable"]))
    enabled = [
        str(case["gtest_filter"])
        for case in target["cases"]  # type: ignore[index]
        if not bool(case["disabled"])
    ]
    if not enabled:
        raise CoverageError(f"{target['name']}: zero enabled GoogleTest cases")
    raw_dir = output / "raw" / prefix
    raw_dir.mkdir(parents=True, exist_ok=True)
    profile_pattern = raw_dir / "%m-%p.profraw"
    environment = dict(common_environment)
    environment["LLVM_PROFILE_FILE"] = str(profile_pattern)
    result_xml = output / "gtest-results" / f"{prefix}.xml"
    if result_xml.exists():
        raise CoverageError(
            f"{target['name']}: stale GoogleTest execution XML exists: {result_xml}"
        )
    _run_logged(
        [
            str(executable),
            f"--gtest_filter={':'.join(enabled)}",
            "--gtest_also_run_disabled_tests",
            f"--gtest_output=xml:{result_xml}",
        ],
        output / "logs" / f"execute-{prefix}.log",
        environment=environment,
        cwd=Path(str(target["working_directory"])),
    )
    _parse_gtest_execution_xml(result_xml, str(target["name"]), enabled)
    return _profiles_for(str(target["name"]), raw_dir), result_xml


def _execute_manual_target(
    target: Mapping[str, object],
    *,
    prefix: str,
    build_dir: Path,
    output: Path,
    common_environment: Mapping[str, str],
) -> list[Path]:
    names = [
        str(test["ctest_name"])
        for test in target["ctest_tests"]  # type: ignore[index]
    ]
    if not names:
        raise CoverageError(f"{target['name']}: zero manual CTest registrations")
    selector = "^(" + "|".join(re.escape(name) for name in names) + ")$"
    raw_dir = output / "raw" / prefix
    raw_dir.mkdir(parents=True, exist_ok=True)
    environment = dict(common_environment)
    environment["LLVM_PROFILE_FILE"] = str(raw_dir / "%m-%p.profraw")
    _run_logged(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "--no-tests=error",
            "-R",
            selector,
        ],
        output / "logs" / f"execute-{prefix}.log",
        environment=environment,
    )
    return _profiles_for(str(target["name"]), raw_dir)


def _merge_profiles(
    llvm_profdata: Path,
    profiles: Sequence[Path],
    merged: Path,
    log_path: Path,
) -> None:
    if not profiles:
        raise CoverageError(f"cannot create {merged}: zero input profiles")
    command = [
        str(llvm_profdata),
        "merge",
        "--failure-mode=any",
        "--sparse",
        "--output",
        str(merged),
        *(str(path) for path in profiles),
    ]
    _run_logged(command, log_path, environment=os.environ)
    if not merged.is_file() or merged.stat().st_size == 0:
        raise CoverageError(f"llvm-profdata did not create nonempty {merged}")


def _export_coverage(
    llvm_cov: Path,
    objects: Sequence[Path],
    profile: Path,
    destination: Path,
    log_path: Path,
    *,
    repo_root: Path | None = None,
    summary_only: bool = False,
) -> dict[str, object]:
    if not objects:
        raise CoverageError("llvm-cov export received zero objects")
    command = [
        str(llvm_cov),
        "export",
        f"--instr-profile={profile}",
    ]
    if summary_only:
        command.append("--summary-only")
    if repo_root is not None:
        root = re.escape(str(repo_root.resolve()))
        command.append(
            f"--ignore-filename-regex=^{root}/"
            r"(src|methods)/(.*/)?"
            r"(build|external|fixture|fixtures|generated|test|tests|"
            r"testdata|third_party)(/|$)"
        )
    command.append(str(objects[0]))
    command.extend(f"--object={path}" for path in objects[1:])
    try:
        with destination.open("w", encoding="utf-8") as output_handle:
            result = subprocess.run(
                command,
                text=True,
                stdout=output_handle,
                stderr=subprocess.PIPE,
                check=False,
                timeout=1800,
            )
    except (OSError, subprocess.SubprocessError) as error:
        raise CoverageError(f"cannot run llvm-cov export: {error}") from error
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(f"$ {shlex.join(command)}\n{result.stderr}", encoding="utf-8")
    if result.returncode != 0:
        raise CoverageError(
            f"llvm-cov export failed with exit {result.returncode}; see {log_path}"
        )
    try:
        document = json.loads(destination.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(f"llvm-cov produced invalid JSON {destination}: {error}")
    if not isinstance(document, dict):
        raise CoverageError(f"llvm-cov export {destination} is not an object")
    return document


def _verify_target_profile(
    target: Mapping[str, object],
    profiles: Sequence[Path],
    *,
    prefix: str,
    output: Path,
    llvm_cov: Path,
    llvm_profdata: Path,
) -> None:
    profdata = output / "probes/profdata" / f"{prefix}.profdata"
    _merge_profiles(
        llvm_profdata,
        profiles,
        profdata,
        output / "logs" / f"probe-merge-{prefix}.log",
    )
    document = _export_coverage(
        llvm_cov,
        [Path(str(target["executable"]))],
        profdata,
        output / "probes" / f"{prefix}.json",
        output / "logs" / f"probe-export-{prefix}.log",
        summary_only=True,
    )
    data = document.get("data")
    if (
        not isinstance(data, list)
        or len(data) != 1
        or not isinstance(data[0], dict)
        or not data[0].get("files")
    ):
        raise CoverageError(
            f"{target['name']}: executable has no readable coverage mappings"
        )


def _relative_paths(paths: Sequence[Path], output: Path) -> list[str]:
    return [path.relative_to(output).as_posix() for path in sorted(paths)]


def collect(arguments: argparse.Namespace) -> dict[str, object]:
    repo_root = arguments.repo_root.resolve()
    build_dir = arguments.build_dir.resolve()
    if not build_dir.is_dir():
        raise CoverageError(f"coverage build directory does not exist: {build_dir}")
    output = _prepare_output(arguments.output)
    cohort = COVERAGE_COHORTS[arguments.cohort]
    for directory in (
        output / "discovery-profiles",
        output / "gtest-results",
        output / "logs",
        output / "merged",
        output / "probes/raw",
        output / "probes/profdata",
        output / "raw",
    ):
        directory.mkdir(parents=True, exist_ok=True)

    cache = read_cmake_cache(build_dir)
    build_identity = _require_coverage_build(cache)
    compiler, llvm_cov, llvm_profdata, tools = _compiler_and_tools(
        cache, arguments.llvm_cov, arguments.llvm_profdata
    )
    inventory = load_cpu_test_inventory(
        build_dir,
        repo_root=repo_root,
        reconciler=arguments.reconciler,
        reconciler_log=output / "logs/reconciler.log",
        discovery_profile_dir=output / "discovery-profiles",
        cohort=cohort,
    )
    discovery_profiles = sorted((output / "discovery-profiles").glob("*.profraw"))
    empty_discovery_profiles = [
        path for path in discovery_profiles if path.stat().st_size == 0
    ]
    if empty_discovery_profiles:
        raise CoverageError(
            "test discovery emitted empty raw profiles: "
            f"{[str(path) for path in empty_discovery_profiles]!r}"
        )
    write_json(output / "test-inventory.json", inventory)
    targets = inventory["targets"]
    assert isinstance(targets, list)
    prefixes = _safe_target_names(targets)
    common_environment = _environment(inventory["common_environment"])  # type: ignore[arg-type]

    probe_profiles: dict[str, list[Path]] = {}
    gtest_targets = [target for target in targets if target["kind"] == "gtest"]
    manual_targets = [target for target in targets if target["kind"] == "manual"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
        futures = {
            executor.submit(
                _probe_gtest_target,
                target,
                prefix=prefixes[str(target["name"])],
                output=output,
                common_environment=common_environment,
            ): target
            for target in gtest_targets
        }
        for future in concurrent.futures.as_completed(futures):
            target = futures[future]
            probe_profiles[str(target["name"])] = future.result()

    execution_profiles: dict[str, list[Path]] = {}
    gtest_result_xml: dict[str, Path] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
        futures = {
            executor.submit(
                _execute_gtest_target,
                target,
                prefix=prefixes[str(target["name"])],
                output=output,
                common_environment=common_environment,
            ): target
            for target in gtest_targets
        }
        for future in concurrent.futures.as_completed(futures):
            target = futures[future]
            profiles, result_xml = future.result()
            name = str(target["name"])
            execution_profiles[name] = profiles
            gtest_result_xml[name] = result_xml

    for target in manual_targets:
        name = str(target["name"])
        profiles = _execute_manual_target(
            target,
            prefix=prefixes[name],
            build_dir=build_dir,
            output=output,
            common_environment=common_environment,
        )
        execution_profiles[name] = profiles
        probe_profiles[name] = profiles

    profile_records: list[dict[str, object]] = []
    for target in targets:
        name = str(target["name"])
        target_probe_profiles = probe_profiles.get(name)
        target_execution_profiles = execution_profiles.get(name)
        if not target_probe_profiles or not target_execution_profiles:
            raise CoverageError(f"{name}: profile proof is incomplete")
        _verify_target_profile(
            target,
            target_probe_profiles,
            prefix=prefixes[name],
            output=output,
            llvm_cov=llvm_cov,
            llvm_profdata=llvm_profdata,
        )
        profile_records.append(
            {
                "execution_raw_profiles": _relative_paths(
                    target_execution_profiles, output
                ),
                "kind": target["kind"],
                "probe_raw_profiles": _relative_paths(target_probe_profiles, output),
                "gtest_result_xml": (
                    gtest_result_xml[name].relative_to(output).as_posix()
                    if name in gtest_result_xml
                    else None
                ),
                "target": name,
            }
        )

    all_execution_profiles = sorted(
        profile for profiles in execution_profiles.values() for profile in profiles
    )
    merged_profile = output / "merged/coverage.profdata"
    _merge_profiles(
        llvm_profdata,
        all_execution_profiles,
        merged_profile,
        output / "logs/merge.log",
    )
    objects = sorted(Path(str(target["executable"])) for target in targets)
    if len(objects) != len(set(objects)):
        raise CoverageError("canonical CPU target registry repeats an executable")
    raw_export_path = output / "llvm-cov-export.json"
    raw_export = _export_coverage(
        llvm_cov,
        objects,
        merged_profile,
        raw_export_path,
        output / "logs/llvm-cov-export.log",
        repo_root=repo_root,
    )
    normalized = normalize_llvm_cov_export(raw_export, repo_root)

    production = production_source_digest(repo_root)
    production_build_inputs = production_build_input_digest(repo_root)
    compile_commands = semantic_compile_command_digest(build_dir, repo_root)
    exclusions = exclusion_identity()
    backend = {
        "extrinsic_backend": cache.get("EXTRINSIC_BACKEND", "<unset>"),
        "extrinsic_platform": cache.get("EXTRINSIC_PLATFORM", "<unset>"),
        "headless_no_glfw": cache.get("INTRINSIC_HEADLESS_NO_GLFW", "<unset>"),
        "platform_backend": cache.get("INTRINSIC_PLATFORM_BACKEND", "<unset>"),
        "selected_platform_backend": cache.get(
            "INTRINSIC_PLATFORM_BACKEND_SELECTED", "<unset>"
        ),
    }
    environment_entries = inventory["common_environment"]
    environment_digest = hashlib.sha256(
        json.dumps(environment_entries, separators=(",", ":"), sort_keys=True).encode(
            "utf-8"
        )
    ).hexdigest()
    working_directory_digest = hashlib.sha256(
        json.dumps(
            {
                str(target["name"]): target["working_directory_identity"]
                for target in gtest_targets
            },
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    ).hexdigest()
    case_working_directories: list[dict[str, str]] = []
    for target in targets:
        if target["kind"] == "gtest":
            working_directory = target["working_directory_identity"]
            if not isinstance(working_directory, str):
                raise CoverageError(
                    f"{target['name']}: missing GoogleTest working-directory identity"
                )
            for case in target["cases"]:
                if not case["disabled"]:
                    case_working_directories.append(
                        {
                            "kind": "gtest",
                            "name": str(case["gtest_filter"]),
                            "working_directory": working_directory,
                        }
                    )
        else:
            for test in target["ctest_tests"]:
                working_directory = test.get("working_directory_identity")
                if not isinstance(working_directory, str):
                    raise CoverageError(
                        f"{target['name']}: missing manual CTest "
                        "working-directory identity"
                    )
                case_working_directories.append(
                    {
                        "kind": "manual",
                        "name": str(test["ctest_name"]),
                        "working_directory": working_directory,
                    }
                )
    case_working_directories.sort(
        key=lambda record: (record["kind"], record["name"])
    )
    if not case_working_directories:
        raise CoverageError("execution identity selected zero test cases")
    case_working_directory_digest = hashlib.sha256(
        json.dumps(
            case_working_directories,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    ).hexdigest()
    execution_identity = {
        "aggregate": cohort.aggregate,
        "case_working_directory_digest": case_working_directory_digest,
        "case_working_directory_record_count": len(case_working_directories),
        "common_ctest_environment_digest": environment_digest,
        "discovery_profile_pattern": "discovery-profiles/%m-%p.profraw",
        "excluded_labels": list(cohort.excluded_labels),
        "gtest_result_format": "xml",
        "mode": "per-executable-enabled-gtest-plus-manual-ctest",
        "profile_pattern": "target/%m-%p.profraw",
        "schema": EXECUTION_IDENTITY_SCHEMA,
        "working_directory_digest": working_directory_digest,
    }
    report: dict[str, object] = {
        "artifacts": {
            "diagnostics": "diagnostics.json",
            "discovery_profiles": "discovery-profiles",
            "gtest_results": "gtest-results",
            "merged_profile": "merged/coverage.profdata",
            "raw_export": "llvm-cov-export.json",
            "test_inventory": "test-inventory.json",
        },
        "coverage": normalized,
        "diff": None,
        "identity": {
            "backend": backend,
            "build": build_identity,
            "compile_commands": compile_commands,
            "compiler": compiler,
            "exclusions": exclusions,
            "execution": execution_identity,
            "preset": arguments.preset,
            "production": production,
            "production_build_inputs": production_build_inputs,
            "tools": tools,
        },
        "objects": [str(path) for path in objects],
        "profiles": {
            "discovery_raw_profile_count": len(discovery_profiles),
            "discovery_raw_profiles": _relative_paths(discovery_profiles, output),
            "execution_raw_profile_count": len(all_execution_profiles),
            "targets": profile_records,
        },
        "schema": COVERAGE_SCHEMA,
    }
    if arguments.diff_base:
        report["diff"] = changed_line_coverage(
            repo_root,
            arguments.diff_base,
            normalized["covered_lines"],  # type: ignore[arg-type]
        )
    write_json(output / "coverage.json", report)
    diagnostics = {
        "coverage_report": "coverage.json",
        "discovery_raw_profile_count": len(discovery_profiles),
        "execution_raw_profile_count": len(all_execution_profiles),
        "gtest_result_xml_count": len(gtest_result_xml),
        "object_count": len(objects),
        "schema": DIAGNOSTICS_SCHEMA,
        "status": "ok",
        "target_count": len(targets),
    }
    write_json(output / "diagnostics.json", diagnostics)
    return diagnostics


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    output: Path | None = None
    diagnostics_allowed = False
    try:
        output = arguments.output.resolve()
        diagnostics_allowed = not output.exists() or (
            output.is_dir() and not any(output.iterdir())
        )
        diagnostics = collect(arguments)
    except (CoverageError, OSError, subprocess.SubprocessError) as error:
        if diagnostics_allowed and output is not None and output.is_dir():
            try:
                write_json(
                    output / "diagnostics.json",
                    {
                        "error": str(error),
                        "schema": DIAGNOSTICS_SCHEMA,
                        "status": "failed",
                    },
                )
            except OSError:
                pass
        print(f"CPU source coverage: error: {error}", file=sys.stderr)
        return 1
    print(
        "CPU source coverage: ok: "
        f"targets={diagnostics['target_count']} "
        f"objects={diagnostics['object_count']} "
        f"raw_profiles={diagnostics['execution_raw_profile_count']} "
        f"report={output / 'coverage.json'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
