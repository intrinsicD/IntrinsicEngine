#!/usr/bin/env python3
from __future__ import annotations

import csv
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import Iterable, Mapping, Sequence


COVERAGE_SCHEMA = "intrinsic.cpu-source-coverage/v2"
TEST_INVENTORY_SCHEMA = "intrinsic.cpu-test-inventory/v2"
EXECUTION_IDENTITY_SCHEMA = "intrinsic.cpu-source-coverage-execution/v2"
AGGREGATE = "IntrinsicCpuTests"
PRODUCTION_ROOTS = ("src", "methods")
PRODUCTION_SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".inl",
        ".cppm",
        ".ixx",
    }
)
EXCLUDED_PATH_SEGMENTS = frozenset(
    {
        "build",
        "external",
        "fixture",
        "fixtures",
        "generated",
        "test",
        "tests",
        "testdata",
        "third_party",
    }
)
EXCLUDED_CPU_LABELS = ("flaky-quarantine", "gpu", "slow", "vulkan")
IDENTITY_FIELDS = (
    "production",
    "production_build_inputs",
    "compile_commands",
    "compiler",
    "tools",
    "preset",
    "build",
    "backend",
    "exclusions",
    "execution",
)
TEST_REFACTOR_MUTABLE_EXECUTION_FIELDS = frozenset(
    {
        # The v1 diagnostic is keyed by executable target name. The v2
        # case_working_directory_digest below preserves the semantic check.
        "working_directory_digest",
    }
)
REQUIRED_EXECUTION_IDENTITY_FIELDS = frozenset(
    {
        "aggregate",
        "case_working_directory_digest",
        "case_working_directory_record_count",
        "common_ctest_environment_digest",
        "discovery_profile_pattern",
        "excluded_labels",
        "gtest_result_format",
        "mode",
        "profile_pattern",
        "schema",
        "working_directory_digest",
    }
)


class CoverageError(RuntimeError):
    pass


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def read_cmake_cache(build_dir: Path) -> dict[str, str]:
    path = build_dir / "CMakeCache.txt"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CoverageError(f"cannot read CMake cache {path}: {error}") from error

    cache: dict[str, str] = {}
    for line in lines:
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key, separator, _type = key_and_type.partition(":")
        if not separator or not key:
            continue
        cache[key] = value
    return cache


def _read_tsv(path: Path, fields: Sequence[str]) -> list[dict[str, str]]:
    try:
        handle = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read test registry {path}: {error}") from error
    with handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != list(fields):
            raise CoverageError(
                f"{path}: expected TSV header {list(fields)!r}, "
                f"got {reader.fieldnames!r}"
            )
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, start=2):
            if None in row:
                raise CoverageError(f"{path}:{line_number}: extra TSV fields")
            if any(not row.get(field) for field in fields):
                raise CoverageError(
                    f"{path}:{line_number}: registry fields must not be empty"
                )
            rows.append({field: row[field] for field in fields})
    return rows


def _read_registered_targets(path: Path) -> dict[str, tuple[str, ...]]:
    targets: dict[str, tuple[str, ...]] = {}
    for row in _read_tsv(path, ("target", "labels")):
        target = row["target"]
        labels = tuple(row["labels"].split(","))
        if target in targets:
            raise CoverageError(f"{path}: duplicate target {target!r}")
        if not labels or "" in labels or len(labels) != len(set(labels)):
            raise CoverageError(f"{path}: {target!r} has duplicate or empty labels")
        if labels != tuple(sorted(labels)):
            raise CoverageError(f"{path}: {target!r} labels are not sorted")
        targets[target] = labels
    if not targets:
        raise CoverageError(f"{path}: target registry is empty")
    return targets


def _read_aggregate(path: Path) -> tuple[str, ...]:
    try:
        targets = tuple(
            line.strip()
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
    except OSError as error:
        raise CoverageError(f"cannot read CPU aggregate {path}: {error}") from error
    if not targets:
        raise CoverageError(f"{path}: CPU aggregate selected zero targets")
    duplicates = sorted(name for name, count in Counter(targets).items() if count > 1)
    if duplicates:
        raise CoverageError(f"{path}: duplicate targets {duplicates!r}")
    if targets != tuple(sorted(targets)):
        raise CoverageError(f"{path}: CPU aggregate targets are not sorted")
    return targets


def _run_capture(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            list(command),
            cwd=cwd,
            env=None if env is None else dict(env),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise CoverageError(f"cannot run {shlex.join(command)}: {error}") from error


def _run_reconciler(
    build_dir: Path,
    reconciler: Path,
    log_path: Path | None,
    environment: Mapping[str, str] | None,
) -> None:
    command = (
        [sys.executable, str(reconciler)]
        if reconciler.suffix == ".py"
        else [str(reconciler)]
    )
    command.extend(["--build-dir", str(build_dir), "--aggregate", AGGREGATE])
    result = _run_capture(command, env=environment, timeout=300)
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(
            f"$ {shlex.join(command)}\n{result.stdout}", encoding="utf-8"
        )
    if result.returncode != 0:
        raise CoverageError(
            f"test gate reconciler failed with exit {result.returncode}: "
            f"{reconciler}\n{result.stdout}"
        )


def _ctest_document(
    build_dir: Path, environment: Mapping[str, str] | None
) -> dict[str, object]:
    result = _run_capture(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        env=environment,
        timeout=300,
    )
    if result.returncode != 0:
        raise CoverageError(
            f"CTest inventory failed with exit {result.returncode}:\n{result.stdout}"
        )
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise CoverageError(f"CTest inventory is invalid JSON: {error}") from error
    if not isinstance(document, dict) or not isinstance(document.get("tests"), list):
        raise CoverageError("CTest JSON does not contain a tests array")
    return document


def _property_values(test: Mapping[str, object], name: str) -> list[object]:
    properties = test.get("properties", [])
    if not isinstance(properties, list):
        raise CoverageError(f"CTest test {test.get('name')!r} has bad properties")
    values: list[object] = []
    for record in properties:
        if not isinstance(record, dict):
            raise CoverageError(
                f"CTest test {test.get('name')!r} has a malformed property"
            )
        if record.get("name") != name:
            continue
        value = record.get("value")
        if isinstance(value, list):
            values.extend(value)
        else:
            values.append(value)
    return values


def _labels(test: Mapping[str, object]) -> tuple[str, ...]:
    values = _property_values(test, "LABELS")
    if any(not isinstance(value, str) or not value for value in values):
        raise CoverageError(f"CTest test {test.get('name')!r} has invalid labels")
    labels = tuple(sorted(value for value in values if isinstance(value, str)))
    if len(labels) != len(set(labels)):
        raise CoverageError(f"CTest test {test.get('name')!r} repeats a label")
    return labels


def _environment(test: Mapping[str, object]) -> tuple[str, ...]:
    values = _property_values(test, "ENVIRONMENT")
    if any(not isinstance(value, str) or "=" not in value for value in values):
        raise CoverageError(
            f"CTest test {test.get('name')!r} has invalid environment entries"
        )
    entries = tuple(sorted(value for value in values if isinstance(value, str)))
    keys = [entry.partition("=")[0] for entry in entries]
    if len(keys) != len(set(keys)):
        raise CoverageError(
            f"CTest test {test.get('name')!r} repeats an environment key"
        )
    return entries


def _disabled(test: Mapping[str, object]) -> bool:
    values = _property_values(test, "DISABLED")
    if not values:
        return False
    if len(values) != 1:
        raise CoverageError(f"CTest test {test.get('name')!r} repeats DISABLED")
    value = values[0]
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    if isinstance(value, str):
        normalized = value.upper()
        if normalized in {"1", "ON", "TRUE", "YES"}:
            return True
        if normalized in {"0", "OFF", "FALSE", "NO"}:
            return False
    raise CoverageError(
        f"CTest test {test.get('name')!r} has invalid DISABLED value {value!r}"
    )


def _working_directory(test: Mapping[str, object], build_dir: Path) -> Path:
    values = _property_values(test, "WORKING_DIRECTORY")
    if not values:
        return build_dir
    if len(values) != 1 or not isinstance(values[0], str) or not values[0]:
        raise CoverageError(
            f"CTest test {test.get('name')!r} has invalid WORKING_DIRECTORY"
        )
    directory = Path(values[0])
    if not directory.is_absolute():
        directory = build_dir / directory
    directory = directory.resolve()
    if not directory.is_dir():
        raise CoverageError(
            f"CTest test {test.get('name')!r} working directory is missing: {directory}"
        )
    return directory


def _working_directory_identity(directory: Path, build_dir: Path) -> str:
    try:
        relative = directory.relative_to(build_dir)
    except ValueError as error:
        raise CoverageError(
            f"CPU GoogleTest working directory must be within the build tree: "
            f"{directory}"
        ) from error
    return "$BUILD" if relative == Path() else f"$BUILD/{relative.as_posix()}"


def _resolved(value: str) -> Path:
    return Path(os.path.realpath(os.path.abspath(value)))


def _binary_path(build_dir: Path, target: str) -> Path:
    candidates = (build_dir / "bin" / target, build_dir / "bin" / f"{target}.exe")
    existing = [candidate.resolve() for candidate in candidates if candidate.is_file()]
    if len(existing) != 1:
        rendered = ", ".join(str(candidate) for candidate in candidates)
        raise CoverageError(
            f"CPU target {target!r} requires exactly one built executable; "
            f"checked {rendered}"
        )
    if not os.access(existing[0], os.X_OK):
        raise CoverageError(f"CPU executable is not executable: {existing[0]}")
    return existing[0]


def _argument_path(argument: str) -> Path | None:
    candidate = argument.partition("=")[2] if "=" in argument else argument
    if not candidate:
        return None
    try:
        return _resolved(candidate)
    except (OSError, ValueError):
        return None


def _target_for_command(
    command: Sequence[str], binaries: Mapping[Path, str]
) -> str | None:
    matches: set[str] = set()
    for argument in command:
        path = _argument_path(argument)
        if path in binaries:
            matches.add(binaries[path])
    if len(matches) > 1:
        raise CoverageError(
            f"CTest command maps to multiple CPU executables: {list(command)!r}"
        )
    return next(iter(matches), None)


def load_cpu_test_inventory(
    build_dir: Path,
    *,
    repo_root: Path,
    reconciler: Path | None = None,
    reconciler_log: Path | None = None,
    discovery_profile_dir: Path,
) -> dict[str, object]:
    build_dir = build_dir.resolve()
    repo_root = repo_root.resolve()
    if reconciler is None:
        reconciler = repo_root / "tests/regression/tooling/Test.TestGateRouting.py"
    discovery_profile_dir = discovery_profile_dir.resolve()
    discovery_profile_dir.mkdir(parents=True, exist_ok=True)
    discovery_environment = os.environ.copy()
    discovery_environment["LLVM_PROFILE_FILE"] = str(
        discovery_profile_dir / "%m-%p.profraw"
    )
    _run_reconciler(
        build_dir,
        reconciler.resolve(),
        reconciler_log,
        discovery_environment,
    )

    inventory_dir = build_dir / "test-inventories"
    registered = _read_registered_targets(inventory_dir / "RegisteredTestTargets.tsv")
    members = _read_aggregate(inventory_dir / f"{AGGREGATE}.txt")
    unknown = set(members) - registered.keys()
    if unknown:
        raise CoverageError(
            f"{AGGREGATE} names unregistered targets {sorted(unknown)!r}"
        )
    expected_members = {
        target
        for target, labels in registered.items()
        if not set(labels).intersection(EXCLUDED_CPU_LABELS)
    }
    missing_members = expected_members - set(members)
    extra_members = set(members) - expected_members
    if missing_members or extra_members:
        raise CoverageError(
            f"{AGGREGATE} disagrees with the canonical CPU label predicate: "
            f"missing={sorted(missing_members)!r}, extra={sorted(extra_members)!r}"
        )
    wrongly_selected = {
        target: sorted(set(registered[target]).intersection(EXCLUDED_CPU_LABELS))
        for target in members
        if set(registered[target]).intersection(EXCLUDED_CPU_LABELS)
    }
    if wrongly_selected:
        raise CoverageError(
            f"{AGGREGATE} includes capability/opt-in targets {wrongly_selected!r}"
        )

    target_paths = {target: _binary_path(build_dir, target) for target in members}
    binaries = {path: target for target, path in target_paths.items()}
    document = _ctest_document(build_dir, discovery_environment)
    target_records: dict[str, list[dict[str, object]]] = {
        target: [] for target in members
    }
    ctest_names: set[str] = set()
    for raw_test in document["tests"]:
        if not isinstance(raw_test, dict):
            raise CoverageError("CTest JSON contains a malformed test")
        name = raw_test.get("name")
        command = raw_test.get("command")
        if not isinstance(name, str) or not name:
            raise CoverageError("CTest JSON contains a test without a name")
        if name in ctest_names:
            raise CoverageError(f"CTest inventory repeats test name {name!r}")
        ctest_names.add(name)
        if command is None and name.endswith("_NOT_BUILT"):
            placeholder_target = name.removesuffix("_NOT_BUILT")
            if placeholder_target in members:
                raise CoverageError(
                    f"selected CPU target {placeholder_target!r} has only a "
                    "NOT_BUILT CTest placeholder"
                )
            continue
        if not isinstance(command, list) or any(
            not isinstance(argument, str) for argument in command
        ):
            raise CoverageError(f"CTest test {name!r} has a malformed command")
        target = _target_for_command(command, binaries)
        if target is None:
            continue
        labels = _labels(raw_test)
        if labels != registered[target]:
            raise CoverageError(
                f"CTest test {name!r} labels differ from {target}: "
                f"expected={registered[target]!r}, actual={labels!r}"
            )
        filters = [
            argument.removeprefix("--gtest_filter=")
            for argument in command
            if argument.startswith("--gtest_filter=")
        ]
        is_gtest = (
            _resolved(command[0]) == target_paths[target]
            and "--gtest_also_run_disabled_tests" in command
            and bool(filters)
        )
        if is_gtest and (len(filters) != 1 or not filters[0]):
            raise CoverageError(
                f"CTest GoogleTest registration {name!r} has invalid filter"
            )
        target_records[target].append(
            {
                "ctest_name": name,
                "command": command,
                "disabled": _disabled(raw_test),
                "environment": list(_environment(raw_test)),
                "gtest_filter": filters[0] if is_gtest else None,
                "is_gtest": is_gtest,
                "labels": list(labels),
                "working_directory": str(_working_directory(raw_test, build_dir)),
            }
        )

    targets: list[dict[str, object]] = []
    common_environments: set[tuple[str, ...]] = set()
    global_cases: dict[str, str] = {}
    enabled_case_count = 0
    manual_test_count = 0
    gtest_target_count = 0
    manual_target_count = 0
    for target in members:
        records = target_records[target]
        if not records:
            raise CoverageError(
                f"CPU target {target!r} has no mapped CTest registrations"
            )
        kinds = {bool(record["is_gtest"]) for record in records}
        if len(kinds) != 1:
            raise CoverageError(
                f"CPU target {target!r} mixes GoogleTest and manual registrations"
            )
        is_gtest = kinds == {True}
        cases: list[dict[str, object]] = []
        ctest_tests: list[dict[str, object]] = []
        working_directory: str | None = None
        working_directory_identity: str | None = None
        if is_gtest:
            gtest_target_count += 1
            seen_filters: set[str] = set()
            working_directories = {
                str(record["working_directory"]) for record in records
            }
            if len(working_directories) != 1:
                raise CoverageError(
                    f"CPU GoogleTest target {target!r} has inconsistent CTest "
                    f"working directories: {sorted(working_directories)!r}"
                )
            working_directory = next(iter(working_directories))
            working_directory_identity = _working_directory_identity(
                Path(working_directory), build_dir
            )
            for record in records:
                case = str(record["gtest_filter"])
                if case in seen_filters:
                    raise CoverageError(
                        f"CPU target {target!r} repeats GoogleTest case {case!r}"
                    )
                previous = global_cases.get(case)
                if previous is not None:
                    raise CoverageError(
                        f"GoogleTest case {case!r} belongs to both "
                        f"{previous!r} and {target!r}"
                    )
                seen_filters.add(case)
                global_cases[case] = target
                environment = tuple(str(item) for item in record["environment"])
                common_environments.add(environment)
                disabled = bool(record["disabled"])
                enabled_case_count += not disabled
                cases.append(
                    {
                        "ctest_name": record["ctest_name"],
                        "disabled": disabled,
                        "executable": str(target_paths[target]),
                        "gtest_filter": case,
                        "labels": record["labels"],
                    }
                )
            if not any(not bool(case["disabled"]) for case in cases):
                raise CoverageError(
                    f"CPU GoogleTest target {target!r} has zero enabled cases"
                )
        else:
            manual_target_count += 1
            for record in records:
                if bool(record["disabled"]):
                    raise CoverageError(
                        f"manual CPU CTest producer {record['ctest_name']!r} "
                        "is disabled"
                    )
                manual_test_count += 1
                ctest_tests.append(
                    {
                        "command": record["command"],
                        "ctest_name": record["ctest_name"],
                        "executable": str(target_paths[target]),
                        "labels": record["labels"],
                        "working_directory_identity": _working_directory_identity(
                            Path(str(record["working_directory"])),
                            build_dir,
                        ),
                    }
                )
        targets.append(
            {
                "cases": sorted(cases, key=lambda case: str(case["gtest_filter"])),
                "ctest_tests": sorted(
                    ctest_tests, key=lambda test: str(test["ctest_name"])
                ),
                "executable": str(target_paths[target]),
                "kind": "gtest" if is_gtest else "manual",
                "labels": list(registered[target]),
                "name": target,
                "working_directory": working_directory,
                "working_directory_identity": working_directory_identity,
            }
        )

    if enabled_case_count == 0:
        raise CoverageError(f"{AGGREGATE} selected zero enabled GoogleTest cases")
    if len(common_environments) != 1:
        raise CoverageError(
            "CPU GoogleTest registrations do not share one CTest environment: "
            f"{sorted(common_environments)!r}"
        )
    common_environment = list(next(iter(common_environments)))
    result: dict[str, object] = {
        "aggregate": AGGREGATE,
        "common_environment": common_environment,
        "schema": TEST_INVENTORY_SCHEMA,
        "summary": {
            "ctest_test_count": enabled_case_count + manual_test_count,
            "enabled_gtest_case_count": enabled_case_count,
            "gtest_target_count": gtest_target_count,
            "manual_ctest_test_count": manual_test_count,
            "manual_target_count": manual_target_count,
            "target_count": len(targets),
        },
        "targets": targets,
    }
    result["digest"] = _sha256(_canonical_json(result))
    return result


def _is_production_path(path: PurePosixPath) -> bool:
    if not path.parts or path.parts[0] not in PRODUCTION_ROOTS:
        return False
    return not bool(set(path.parts).intersection(EXCLUDED_PATH_SEGMENTS))


def _normalize_repo_path(path: str | Path, repo_root: Path) -> str | None:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = repo_root / candidate
    try:
        relative = candidate.resolve(strict=False).relative_to(repo_root.resolve())
    except (OSError, ValueError):
        return None
    normalized = PurePosixPath(relative.as_posix())
    return normalized.as_posix() if _is_production_path(normalized) else None


def exclusion_identity() -> dict[str, object]:
    rules: dict[str, object] = {
        "path_segments": sorted(EXCLUDED_PATH_SEGMENTS),
        "production_roots": list(PRODUCTION_ROOTS),
        "suffixes": sorted(PRODUCTION_SUFFIXES),
    }
    rules["digest"] = _sha256(_canonical_json(rules))
    return rules


def production_source_digest(repo_root: Path) -> dict[str, object]:
    repo_root = repo_root.resolve()
    records: list[tuple[str, str]] = []
    for root_name in PRODUCTION_ROOTS:
        root = repo_root / root_name
        if not root.is_dir():
            raise CoverageError(f"production source root is missing: {root}")
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in PRODUCTION_SUFFIXES:
                continue
            normalized = _normalize_repo_path(path, repo_root)
            if normalized is None:
                continue
            try:
                content = path.read_bytes()
            except OSError as error:
                raise CoverageError(f"cannot hash production source {path}: {error}")
            records.append((normalized, _sha256(content)))
    records.sort()
    if not records:
        raise CoverageError("production source identity selected zero C++ files")
    return {
        "digest": _sha256(_canonical_json(records)),
        "file_count": len(records),
        "files": [{"digest": digest, "path": path} for path, digest in records],
    }


def production_build_input_digest(repo_root: Path) -> dict[str, object]:
    repo_root = repo_root.resolve()
    required = repo_root / "CMakeLists.txt"
    if not required.is_file():
        raise CoverageError(f"production build identity requires {required}")

    selected: set[Path] = {required}
    for name in ("CMakePresets.json", "vcpkg.json", "vcpkg-configuration.json"):
        candidate = repo_root / name
        if candidate.is_file():
            selected.add(candidate)
    cmake_root = repo_root / "cmake"
    if cmake_root.is_dir():
        selected.update(
            path
            for path in cmake_root.rglob("*.cmake")
            if path.is_file() and path != cmake_root / "IntrinsicTest.cmake"
        )
    for root_name in PRODUCTION_ROOTS:
        root = repo_root / root_name
        if not root.is_dir():
            continue
        selected.update(
            path
            for path in root.rglob("*.cmake")
            if path.is_file() and _normalize_repo_path(path, repo_root) is not None
        )
        selected.update(
            path
            for path in root.rglob("CMakeLists.txt")
            if path.is_file() and _normalize_repo_path(path, repo_root) is not None
        )

    vcpkg_root = repo_root / "tools/vcpkg"
    tracked_vcpkg_files: list[Path] = []
    git_root = _run_capture(
        ["git", "-C", str(repo_root), "rev-parse", "--show-toplevel"],
        timeout=30,
    )
    if (
        git_root.returncode == 0
        and Path(git_root.stdout.strip()).resolve() == repo_root
    ):
        tracked = _run_capture(
            ["git", "-C", str(repo_root), "ls-files", "-z", "--", "tools/vcpkg"],
            timeout=30,
        )
        if tracked.returncode != 0:
            raise CoverageError(
                f"cannot enumerate checked-in tools/vcpkg inputs:\n{tracked.stdout}"
            )
        tracked_vcpkg_files = [
            repo_root / value for value in tracked.stdout.split("\0") if value
        ]
    elif vcpkg_root.is_dir():
        tracked_vcpkg_files = [path for path in vcpkg_root.rglob("*") if path.is_file()]
    selected.update(tracked_vcpkg_files)

    records: list[tuple[str, str]] = []
    for path in sorted(selected):
        try:
            relative = path.resolve().relative_to(repo_root).as_posix()
            content = path.read_bytes()
        except (OSError, ValueError) as error:
            raise CoverageError(f"cannot hash production build input {path}: {error}")
        records.append((relative, _sha256(content)))
    if not records:
        raise CoverageError("production build identity selected zero inputs")
    return {
        "digest": _sha256(_canonical_json(records)),
        "file_count": len(records),
        "files": [{"digest": digest, "path": path} for path, digest in records],
    }


_STRIP_NEXT_FLAGS = frozenset(
    {"-MF", "-MJ", "-MQ", "-MT", "-o", "--serialize-diagnostics"}
)


def _command_arguments(record: Mapping[str, object]) -> list[str]:
    arguments = record.get("arguments")
    if isinstance(arguments, list) and all(
        isinstance(argument, str) for argument in arguments
    ):
        return list(arguments)
    command = record.get("command")
    if not isinstance(command, str):
        raise CoverageError("compile command has neither string command nor arguments")
    try:
        return shlex.split(command)
    except ValueError as error:
        raise CoverageError(f"cannot parse compile command: {error}") from error


def _replace_roots(value: str, repo_root: Path, build_dir: Path) -> str:
    replacements = sorted(
        (
            (str(build_dir.resolve()), "$BUILD"),
            (str(repo_root.resolve()), "$REPO"),
        ),
        key=lambda item: len(item[0]),
        reverse=True,
    )
    result = value
    for prefix, replacement in replacements:
        result = result.replace(prefix, replacement)
    return result


def _semantic_arguments(
    arguments: Sequence[str], repo_root: Path, build_dir: Path
) -> tuple[str, ...]:
    normalized: list[str] = []
    skip_next = False
    for index, argument in enumerate(arguments):
        if index == 0:
            normalized.append(Path(argument).name)
            continue
        if skip_next:
            skip_next = False
            continue
        if argument in _STRIP_NEXT_FLAGS:
            skip_next = True
            continue
        if any(
            argument.startswith(prefix) for prefix in ("-MF", "-MJ", "-MQ", "-MT", "-o")
        ):
            continue
        normalized.append(_replace_roots(argument, repo_root, build_dir))
    if skip_next:
        raise CoverageError("compile command ends with an output-only flag")
    return tuple(normalized)


def semantic_compile_command_digest(
    build_dir: Path, repo_root: Path
) -> dict[str, object]:
    path = build_dir / "compile_commands.json"
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise CoverageError(f"cannot read compilation database {path}: {error}")
    except json.JSONDecodeError as error:
        raise CoverageError(f"invalid compilation database {path}: {error}")
    if not isinstance(document, list):
        raise CoverageError(f"{path}: compilation database must be an array")

    records: list[dict[str, object]] = []
    instrumented_sources: set[str] = set()
    for raw_record in document:
        if not isinstance(raw_record, dict):
            raise CoverageError(f"{path}: malformed compile command record")
        source = raw_record.get("file")
        if not isinstance(source, str):
            raise CoverageError(f"{path}: compile command has no source file")
        normalized_source = _normalize_repo_path(source, repo_root)
        if (
            normalized_source is None
            or Path(normalized_source).suffix.lower() not in PRODUCTION_SUFFIXES
        ):
            continue
        arguments = _semantic_arguments(
            _command_arguments(raw_record), repo_root, build_dir
        )
        if "-fprofile-instr-generate" not in arguments:
            raise CoverageError(
                f"production compile command is not profile-instrumented: "
                f"{normalized_source}"
            )
        if "-fcoverage-mapping" not in arguments:
            raise CoverageError(
                f"production compile command has no coverage mapping: "
                f"{normalized_source}"
            )
        instrumented_sources.add(normalized_source)
        records.append({"arguments": list(arguments), "source": normalized_source})
    records.sort(key=lambda record: _canonical_json(record))
    if not records:
        raise CoverageError(
            "compilation database contains zero instrumented production commands"
        )
    return {
        "command_count": len(records),
        "digest": _sha256(_canonical_json(records)),
        "source_count": len(instrumented_sources),
    }


def _require_number(value: object, context: str) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CoverageError(f"{context}: expected numeric coverage counter")
    return value


def _location_key(path: str, record: Sequence[object], suffix: str = "") -> str:
    if len(record) < 8:
        raise CoverageError(f"{path}: malformed LLVM coverage location {record!r}")
    coordinates = []
    for index in range(4):
        value = record[index]
        if not isinstance(value, int) or isinstance(value, bool):
            raise CoverageError(f"{path}: malformed LLVM coverage coordinate")
        coordinates.append(value)
    file_id = record[5] if suffix == "region" else record[6]
    expanded_id = record[6] if suffix == "region" else record[7]
    kind_index = 7 if suffix == "region" else 8
    kind = record[kind_index] if len(record) > kind_index else 0
    return (
        f"{path}:{coordinates[0]}:{coordinates[1]}:{coordinates[2]}:"
        f"{coordinates[3]}:{file_id}:{expanded_id}:{kind}"
    )


def _covered_lines_from_segments(path: str, raw_segments: object) -> set[str]:
    if not isinstance(raw_segments, list):
        raise CoverageError(f"{path}: LLVM coverage segments must be an array")
    segments: list[Sequence[object]] = []
    for segment in raw_segments:
        if not isinstance(segment, list) or len(segment) < 6:
            raise CoverageError(f"{path}: malformed LLVM coverage segment")
        segments.append(segment)
    covered: set[str] = set()
    for index, segment in enumerate(segments):
        line, column = segment[0], segment[1]
        if not isinstance(line, int) or not isinstance(column, int):
            raise CoverageError(f"{path}: malformed LLVM segment coordinate")
        count = _require_number(segment[2], f"{path}: segment")
        has_count = segment[3]
        is_gap = segment[5]
        if not isinstance(has_count, bool) or not isinstance(is_gap, bool):
            raise CoverageError(f"{path}: malformed LLVM segment flags")
        if not has_count or is_gap or count <= 0:
            continue
        end_line = line
        if index + 1 < len(segments):
            next_line = segments[index + 1][0]
            next_column = segments[index + 1][1]
            if not isinstance(next_line, int) or not isinstance(next_column, int):
                raise CoverageError(f"{path}: malformed LLVM segment coordinate")
            end_line = next_line if next_column > 1 else max(line, next_line - 1)
        covered.update(
            f"{path}:{covered_line}" for covered_line in range(line, end_line + 1)
        )
    return covered


def normalize_llvm_cov_export(
    raw_export: Mapping[str, object], repo_root: Path
) -> dict[str, object]:
    if raw_export.get("type") != "llvm.coverage.json.export":
        raise CoverageError("llvm-cov export has an unexpected document type")
    data = raw_export.get("data")
    if not isinstance(data, list) or len(data) != 1 or not isinstance(data[0], dict):
        raise CoverageError("llvm-cov export must contain exactly one data record")
    payload = data[0]
    raw_files = payload.get("files")
    raw_functions = payload.get("functions")
    if not isinstance(raw_files, list) or not isinstance(raw_functions, list):
        raise CoverageError("llvm-cov export omits files or functions")

    covered_lines: set[str] = set()
    covered_functions: set[str] = set()
    covered_regions: set[str] = set()
    covered_branch_arms: set[str] = set()
    files: dict[str, object] = {}

    for raw_file in raw_files:
        if not isinstance(raw_file, dict):
            raise CoverageError("llvm-cov export contains a malformed file")
        filename = raw_file.get("filename")
        if not isinstance(filename, str):
            raise CoverageError("llvm-cov export file has no filename")
        normalized = _normalize_repo_path(filename, repo_root)
        if normalized is None:
            continue
        if normalized in files:
            raise CoverageError(f"llvm-cov export repeats production file {normalized}")
        summary = raw_file.get("summary")
        if not isinstance(summary, dict):
            raise CoverageError(f"{normalized}: llvm-cov summary is malformed")
        files[normalized] = summary
        covered_lines.update(
            _covered_lines_from_segments(normalized, raw_file.get("segments"))
        )
        branches = raw_file.get("branches", [])
        if not isinstance(branches, list):
            raise CoverageError(f"{normalized}: branches must be an array")
        for branch in branches:
            if not isinstance(branch, list) or len(branch) < 9:
                raise CoverageError(f"{normalized}: malformed branch record")
            true_count = _require_number(branch[4], f"{normalized}: true branch")
            false_count = _require_number(branch[5], f"{normalized}: false branch")
            base = _location_key(normalized, branch, suffix="branch")
            if true_count > 0:
                covered_branch_arms.add(f"{base}:true")
            if false_count > 0:
                covered_branch_arms.add(f"{base}:false")

    for raw_function in raw_functions:
        if not isinstance(raw_function, dict):
            raise CoverageError("llvm-cov export contains a malformed function")
        name = raw_function.get("name")
        filenames = raw_function.get("filenames")
        count = _require_number(raw_function.get("count"), "llvm-cov function counter")
        if (
            not isinstance(name, str)
            or not isinstance(filenames, list)
            or any(not isinstance(filename, str) for filename in filenames)
        ):
            raise CoverageError("llvm-cov export function metadata is malformed")
        normalized_filenames = [
            _normalize_repo_path(str(filename), repo_root) for filename in filenames
        ]
        production_filenames = [
            filename for filename in normalized_filenames if filename is not None
        ]
        if count > 0 and production_filenames:
            covered_functions.add(f"{production_filenames[0]}:{name}")
        regions = raw_function.get("regions")
        if not isinstance(regions, list):
            raise CoverageError(f"llvm-cov function {name!r} has no regions")
        for region in regions:
            if not isinstance(region, list) or len(region) < 8:
                raise CoverageError(f"llvm-cov function {name!r} has a bad region")
            execution_count = _require_number(region[4], f"{name}: region")
            file_id = region[5]
            if (
                not isinstance(file_id, int)
                or isinstance(file_id, bool)
                or file_id < 0
                or file_id >= len(normalized_filenames)
            ):
                raise CoverageError(f"llvm-cov function {name!r} has bad file id")
            normalized = normalized_filenames[file_id]
            if normalized is not None and execution_count > 0:
                covered_regions.add(_location_key(normalized, region, suffix="region"))

    if not files:
        raise CoverageError("llvm-cov export selected zero production files")
    if not covered_regions:
        raise CoverageError("CPU coverage selected zero covered production regions")
    normalized_coverage: dict[str, object] = {
        "covered_branch_arms": sorted(covered_branch_arms),
        "covered_functions": sorted(covered_functions),
        "covered_lines": sorted(covered_lines),
        "covered_regions": sorted(covered_regions),
        "files": [{"path": path, "summary": files[path]} for path in sorted(files)],
        "raw_totals": payload.get("totals", {}),
        "summary": {
            "covered_branch_arm_count": len(covered_branch_arms),
            "covered_function_count": len(covered_functions),
            "covered_line_count": len(covered_lines),
            "covered_region_count": len(covered_regions),
            "production_file_count": len(files),
        },
    }
    return normalized_coverage


def _coverage_set(report: Mapping[str, object], field: str) -> frozenset[str]:
    coverage = report.get("coverage")
    if not isinstance(coverage, dict):
        raise CoverageError("coverage report has no coverage object")
    values = coverage.get(field)
    if not isinstance(values, list) or any(
        not isinstance(value, str) for value in values
    ):
        raise CoverageError(f"coverage report has invalid {field}")
    result = frozenset(values)
    if len(result) != len(values):
        raise CoverageError(f"coverage report repeats entries in {field}")
    return result


def validate_coverage_report(report: Mapping[str, object]) -> None:
    if report.get("schema") != COVERAGE_SCHEMA:
        raise CoverageError(
            f"expected coverage schema {COVERAGE_SCHEMA!r}, "
            f"got {report.get('schema')!r}"
        )
    identity = report.get("identity")
    if not isinstance(identity, dict):
        raise CoverageError("coverage report has no identity object")
    missing = [field for field in IDENTITY_FIELDS if field not in identity]
    if missing:
        raise CoverageError(f"coverage report identity omits {missing!r}")
    execution = identity.get("execution")
    if not isinstance(execution, dict):
        raise CoverageError("coverage report execution identity must be an object")
    missing_execution = sorted(
        REQUIRED_EXECUTION_IDENTITY_FIELDS - execution.keys()
    )
    if missing_execution:
        raise CoverageError(
            f"coverage report execution identity omits {missing_execution!r}"
        )
    if execution.get("schema") != EXECUTION_IDENTITY_SCHEMA:
        raise CoverageError(
            "coverage report execution identity has unsupported schema "
            f"{execution.get('schema')!r}"
        )
    for field in (
        "case_working_directory_digest",
        "common_ctest_environment_digest",
        "working_directory_digest",
    ):
        value = execution.get(field)
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise CoverageError(
                f"coverage report execution identity has invalid {field}"
            )
    record_count = execution.get("case_working_directory_record_count")
    if (
        isinstance(record_count, bool)
        or not isinstance(record_count, int)
        or record_count <= 0
    ):
        raise CoverageError(
            "coverage report execution identity has invalid "
            "case_working_directory_record_count"
        )
    for field in (
        "covered_branch_arms",
        "covered_functions",
        "covered_lines",
        "covered_regions",
    ):
        _coverage_set(report, field)


def _test_refactor_identity(identity: Mapping[str, object]) -> dict[str, object]:
    normalized = dict(identity)
    execution = normalized.get("execution")
    assert isinstance(execution, dict)
    normalized["execution"] = {
        field: value
        for field, value in execution.items()
        if field not in TEST_REFACTOR_MUTABLE_EXECUTION_FIELDS
    }
    return normalized


def compare_test_only_refactor(
    baseline: Mapping[str, object], candidate: Mapping[str, object]
) -> dict[str, object]:
    validate_coverage_report(baseline)
    validate_coverage_report(candidate)
    baseline_identity = baseline["identity"]
    candidate_identity = candidate["identity"]
    assert isinstance(baseline_identity, dict)
    assert isinstance(candidate_identity, dict)
    comparable_baseline_identity = _test_refactor_identity(baseline_identity)
    comparable_candidate_identity = _test_refactor_identity(candidate_identity)
    mismatches = [
        field
        for field in IDENTITY_FIELDS
        if comparable_baseline_identity[field]
        != comparable_candidate_identity[field]
    ]
    if mismatches:
        details = {
            field: {
                "baseline": comparable_baseline_identity[field],
                "candidate": comparable_candidate_identity[field],
            }
            for field in mismatches
        }
        raise CoverageError(
            "test-only refactor identity mismatch: "
            + json.dumps(details, sort_keys=True)
        )

    baseline_regions = _coverage_set(baseline, "covered_regions")
    candidate_regions = _coverage_set(candidate, "covered_regions")
    baseline_branches = _coverage_set(baseline, "covered_branch_arms")
    candidate_branches = _coverage_set(candidate, "covered_branch_arms")
    lost_regions = sorted(baseline_regions - candidate_regions)
    lost_branch_arms = sorted(baseline_branches - candidate_branches)
    result = {
        "gained_branch_arms": sorted(candidate_branches - baseline_branches),
        "gained_regions": sorted(candidate_regions - baseline_regions),
        "lost_branch_arms": lost_branch_arms,
        "lost_regions": lost_regions,
        "mode": "test-only-refactor",
        "status": "ok" if not lost_regions and not lost_branch_arms else "failed",
    }
    if lost_regions or lost_branch_arms:
        raise CoverageError(
            "test-only refactor lost covered production evidence: "
            f"regions={lost_regions[:20]!r}, "
            f"branch_arms={lost_branch_arms[:20]!r}"
        )
    return result


def changed_line_coverage(
    repo_root: Path,
    diff_base: str,
    covered_lines: Iterable[str],
) -> dict[str, object]:
    command = [
        "git",
        "-C",
        str(repo_root),
        "diff",
        "--unified=0",
        "--no-ext-diff",
        diff_base,
        "--",
        *PRODUCTION_ROOTS,
    ]
    result = _run_capture(command, timeout=120)
    if result.returncode != 0:
        raise CoverageError(
            f"cannot compute diff coverage against {diff_base!r}:\n{result.stdout}"
        )
    changed: set[str] = set()
    current_path: str | None = None
    for line in result.stdout.splitlines():
        if line.startswith("+++ "):
            value = line[4:]
            if value == "/dev/null":
                current_path = None
            else:
                value = value.removeprefix("b/")
                normalized = PurePosixPath(value)
                current_path = (
                    normalized.as_posix()
                    if _is_production_path(normalized)
                    and normalized.suffix.lower() in PRODUCTION_SUFFIXES
                    else None
                )
            continue
        if not line.startswith("@@ ") or current_path is None:
            continue
        match = re.search(r"\+(?P<line>\d+)(?:,(?P<count>\d+))?", line)
        if not match:
            raise CoverageError(f"cannot parse git diff hunk {line!r}")
        start = int(match.group("line"))
        count = int(match.group("count") or "1")
        changed.update(
            f"{current_path}:{number}" for number in range(start, start + count)
        )
    covered = set(covered_lines)
    covered_changed = sorted(changed.intersection(covered))
    missing = sorted(changed - covered)
    percentage = (
        None if not changed else round(100.0 * len(covered_changed) / len(changed), 4)
    )
    return {
        "base": diff_base,
        "changed_line_count": len(changed),
        "covered_changed_line_count": len(covered_changed),
        "covered_lines": covered_changed,
        "missing_lines": missing,
        "percentage": percentage,
    }
