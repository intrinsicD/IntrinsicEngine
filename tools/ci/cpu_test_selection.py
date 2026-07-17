#!/usr/bin/env python3
"""Capture and compare the canonical exclusion-only CPU logical selection."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Mapping, Sequence


REPORT_SCHEMA = "intrinsic.cpu-test-selection/v1"
PARITY_SCHEMA = "intrinsic.cpu-test-selection-parity/v1"
AGGREGATE = "IntrinsicCpuTests"
EXCLUDED_LABELS = ("flaky-quarantine", "gpu", "slow", "vulkan")
SANITIZER_IDENTITIES = ("none", "asan", "ubsan", "asan-ubsan")
MODE_IDENTITIES = {
    "none": "none",
    "address": "asan",
    "undefined": "ubsan",
    "address,undefined": "asan-ubsan",
}
_GTEST_SUITE_RE = re.compile(r"^(?P<suite>\S+)\.\s*(?:#.*)?$")
_GTEST_COMMENT_RE = re.compile(r"\s+#.*$")


class SelectionError(RuntimeError):
    pass


def _canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_json(value)).hexdigest()


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def _read_cache(build_dir: Path) -> dict[str, str]:
    path = build_dir / "CMakeCache.txt"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise SelectionError(
            f"cannot read configured CMake cache {path}: {error}"
        ) from error

    entries: dict[str, str] = {}
    for line_number, line in enumerate(lines, start=1):
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        key, separator, _kind = declaration.partition(":")
        if not separator or not key:
            continue
        if key in entries:
            raise SelectionError(f"{path}:{line_number}: duplicate cache key {key!r}")
        entries[key] = value
    return entries


def _cache_bool(cache: Mapping[str, str], key: str) -> bool:
    if key not in cache:
        raise SelectionError(f"configured CMake cache omits required key {key}")
    normalized = cache[key].upper()
    if normalized in {"1", "ON", "TRUE", "YES", "Y"}:
        return True
    if normalized in {"0", "OFF", "FALSE", "NO", "N"}:
        return False
    raise SelectionError(f"CMake cache entry {key} is not Boolean: {cache[key]!r}")


def _sanitizer_identity(cache: Mapping[str, str], expected_sanitizer: str) -> str:
    identity = cache.get("INTRINSIC_SANITIZER_IDENTITY")
    if identity not in SANITIZER_IDENTITIES:
        raise SelectionError(
            "configured CMake cache must resolve INTRINSIC_SANITIZER_IDENTITY "
            f"to one of {list(SANITIZER_IDENTITIES)!r}, got {identity!r}"
        )
    if identity != expected_sanitizer:
        raise SelectionError(
            f"configured sanitizer identity is {identity!r}, "
            f"expected {expected_sanitizer!r}"
        )

    mode = cache.get("INTRINSIC_SANITIZER_MODE")
    if mode is None:
        raise SelectionError(
            "configured CMake cache omits required key INTRINSIC_SANITIZER_MODE"
        )
    mode_identity = MODE_IDENTITIES.get(mode)
    if mode_identity != identity:
        raise SelectionError(
            f"sanitizer mode {mode!r} resolves to {mode_identity!r}, "
            f"not configured identity {identity!r}"
        )

    expected_address = identity in {"asan", "asan-ubsan"}
    expected_undefined = identity in {"ubsan", "asan-ubsan"}
    expected_enabled = identity != "none"
    actual = {
        "INTRINSIC_ENABLE_SANITIZERS": _cache_bool(
            cache, "INTRINSIC_ENABLE_SANITIZERS"
        ),
        "INTRINSIC_SANITIZER_HAS_ADDRESS": _cache_bool(
            cache, "INTRINSIC_SANITIZER_HAS_ADDRESS"
        ),
        "INTRINSIC_SANITIZER_HAS_UNDEFINED": _cache_bool(
            cache, "INTRINSIC_SANITIZER_HAS_UNDEFINED"
        ),
    }
    expected = {
        "INTRINSIC_ENABLE_SANITIZERS": expected_enabled,
        "INTRINSIC_SANITIZER_HAS_ADDRESS": expected_address,
        "INTRINSIC_SANITIZER_HAS_UNDEFINED": expected_undefined,
    }
    disagreements = {
        key: {"expected": expected[key], "actual": actual[key]}
        for key in expected
        if actual[key] != expected[key]
    }
    if disagreements:
        raise SelectionError(
            f"configured sanitizer cache identity is inconsistent: {disagreements!r}"
        )
    return identity


def _read_tsv(path: Path) -> dict[str, tuple[str, ...]]:
    try:
        handle = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise SelectionError(f"cannot read test registry {path}: {error}") from error

    targets: dict[str, tuple[str, ...]] = {}
    with handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != ["target", "labels"]:
            raise SelectionError(
                f"{path}: expected TSV header ['target', 'labels'], "
                f"got {reader.fieldnames!r}"
            )
        previous_target: str | None = None
        for line_number, row in enumerate(reader, start=2):
            if None in row or not row.get("target") or not row.get("labels"):
                raise SelectionError(f"{path}:{line_number}: malformed registry row")
            target = row["target"]
            labels = tuple(row["labels"].split(","))
            if target in targets:
                raise SelectionError(
                    f"{path}:{line_number}: duplicate target {target!r}"
                )
            if previous_target is not None and target <= previous_target:
                raise SelectionError(f"{path}: registered targets are not sorted")
            if (
                not labels
                or "" in labels
                or labels != tuple(sorted(labels))
                or len(labels) != len(set(labels))
            ):
                raise SelectionError(
                    f"{path}:{line_number}: labels for {target!r} "
                    "must be nonempty, unique, and sorted"
                )
            targets[target] = labels
            previous_target = target
    if not targets:
        raise SelectionError(f"{path}: registered target inventory is empty")
    return targets


def _read_aggregate(path: Path) -> tuple[str, ...]:
    try:
        members = tuple(
            line.strip()
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
    except OSError as error:
        raise SelectionError(
            f"cannot read aggregate inventory {path}: {error}"
        ) from error
    if not members:
        raise SelectionError(f"{path}: aggregate selected zero producers")
    duplicates = sorted(name for name, count in Counter(members).items() if count > 1)
    if duplicates:
        raise SelectionError(f"{path}: duplicate aggregate producers {duplicates!r}")
    if members != tuple(sorted(members)):
        raise SelectionError(f"{path}: aggregate producers are not sorted")
    return members


def _ctest_document(build_dir: Path) -> dict[str, object]:
    command = ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"]
    try:
        result = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=300,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise SelectionError(f"cannot run {shlex.join(command)}: {error}") from error
    if result.returncode != 0:
        raise SelectionError(
            f"CTest inventory failed with exit {result.returncode}:\n{result.stdout}"
        )
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise SelectionError(f"CTest inventory is invalid JSON: {error}") from error
    if not isinstance(document, dict) or not isinstance(document.get("tests"), list):
        raise SelectionError("CTest JSON does not contain a tests array")
    return document


def _property_records(
    test: Mapping[str, object], name: str
) -> list[Mapping[str, object]]:
    properties = test.get("properties", [])
    if not isinstance(properties, list):
        raise SelectionError(f"CTest test {test.get('name')!r} has bad properties")
    matches: list[Mapping[str, object]] = []
    for record in properties:
        if not isinstance(record, dict):
            raise SelectionError(
                f"CTest test {test.get('name')!r} has a malformed property"
            )
        if record.get("name") == name:
            matches.append(record)
    return matches


def _labels(test: Mapping[str, object]) -> tuple[str, ...]:
    records = _property_records(test, "LABELS")
    if len(records) != 1:
        raise SelectionError(
            f"CTest test {test.get('name')!r} must have exactly one LABELS property"
        )
    raw_value = records[0].get("value")
    values = raw_value if isinstance(raw_value, list) else [raw_value]
    if any(not isinstance(value, str) or not value for value in values):
        raise SelectionError(f"CTest test {test.get('name')!r} has invalid labels")
    labels = tuple(sorted(values))
    if len(labels) != len(set(labels)):
        raise SelectionError(f"CTest test {test.get('name')!r} repeats a label")
    return labels


def _disabled(test: Mapping[str, object]) -> bool:
    records = _property_records(test, "DISABLED")
    if not records:
        return False
    if len(records) != 1:
        raise SelectionError(f"CTest test {test.get('name')!r} repeats DISABLED")
    value = records[0].get("value")
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
    raise SelectionError(
        f"CTest test {test.get('name')!r} has invalid DISABLED value {value!r}"
    )


def _argument_path(argument: str) -> Path | None:
    candidate = argument.partition("=")[2] if "=" in argument else argument
    if not candidate:
        return None
    try:
        return Path(os.path.realpath(os.path.abspath(candidate)))
    except (OSError, ValueError):
        return None


def _producer_for_command(
    command: Sequence[str], binaries: Mapping[Path, str], test_name: str
) -> str | None:
    matches = {
        binaries[path]
        for argument in command
        if (path := _argument_path(argument)) in binaries
    }
    if len(matches) > 1:
        raise SelectionError(
            f"CTest test {test_name!r} maps to multiple registered producers: "
            f"{sorted(matches)!r}"
        )
    return next(iter(matches), None)


def _parse_gtest_listing(output: str, producer: str) -> tuple[str, ...]:
    suite: str | None = None
    cases: list[str] = []
    for line_number, line in enumerate(output.splitlines(), start=1):
        if not line.strip():
            continue
        if line.startswith("  "):
            if suite is None:
                raise SelectionError(
                    f"{producer}: GoogleTest case appears before a suite "
                    f"at output line {line_number}"
                )
            test = _GTEST_COMMENT_RE.sub("", line.strip())
            if not test or any(character.isspace() for character in test):
                raise SelectionError(
                    f"{producer}: malformed GoogleTest case at output line "
                    f"{line_number}: {line!r}"
                )
            cases.append(f"{suite}.{test}")
            continue

        match = _GTEST_SUITE_RE.fullmatch(line)
        if match:
            suite = match.group("suite")
        else:
            suite = None

    duplicates = sorted(case for case, count in Counter(cases).items() if count > 1)
    if duplicates:
        raise SelectionError(
            f"{producer}: duplicate expanded GoogleTest names {duplicates!r}"
        )
    if not cases:
        raise SelectionError(f"{producer}: --gtest_list_tests returned no cases")
    return tuple(cases)


def _run_gtest_listing(binary: Path, producer: str) -> tuple[str, ...]:
    command = [str(binary), "--gtest_list_tests"]
    environment = os.environ.copy()
    environment["GTEST_COLOR"] = "no"
    try:
        result = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=60,
            env=environment,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise SelectionError(
            f"cannot run {producer} --gtest_list_tests: {error}"
        ) from error
    if result.returncode != 0:
        raise SelectionError(
            f"{producer}: --gtest_list_tests failed with exit "
            f"{result.returncode}:\n{result.stdout}"
        )
    return _parse_gtest_listing(result.stdout, producer)


def _gtest_case_disabled(name: str) -> bool:
    suite, separator, test = name.partition(".")
    if not separator or not suite or not test:
        raise SelectionError(f"malformed raw GoogleTest identity {name!r}")
    suite_parts = suite.split("/")
    test_name = test.split("/", maxsplit=1)[0]
    return any(part.startswith("DISABLED_") for part in suite_parts) or (
        test_name.startswith("DISABLED_")
    )


def _direct_producer(
    command: Sequence[str], binaries: Mapping[Path, str]
) -> str | None:
    if not command:
        return None
    path = _argument_path(command[0])
    return binaries.get(path) if path is not None else None


def _validate_discovered_command(
    command: Sequence[str],
    producer: str,
    binaries: Mapping[Path, str],
    test_name: str,
) -> tuple[Path, str]:
    filters = [
        argument for argument in command if argument.startswith("--gtest_filter=")
    ]
    disabled_switches = [
        argument
        for argument in command
        if argument == "--gtest_also_run_disabled_tests"
    ]
    if (
        _direct_producer(command, binaries) != producer
        or len(filters) != 1
        or not filters[0].removeprefix("--gtest_filter=")
        or disabled_switches != ["--gtest_also_run_disabled_tests"]
        or list(command)
        != [
            command[0],
            filters[0],
            "--gtest_also_run_disabled_tests",
        ]
    ):
        raise SelectionError(
            f"CTest test {test_name!r} is not a canonical discovered "
            f"GoogleTest registration for {producer!r}"
        )
    return Path(command[0]).resolve(), filters[0].removeprefix("--gtest_filter=")


def _validate_grouped_command(
    command: Sequence[str],
    producer: str,
    binaries: Mapping[Path, str],
    test_name: str,
    build_dir: Path,
) -> Path:
    expected_name = f"{producer}.Grouped"
    expected_output = (
        f"--gtest_output=xml:{build_dir}/reports/grouped-ctest/gtest/{producer}.xml"
    )
    if (
        test_name != expected_name
        or _direct_producer(command, binaries) != producer
        or list(command) != [command[0], "--gtest_filter=*", expected_output]
    ):
        raise SelectionError(
            f"CTest test {test_name!r} is not the canonical grouped "
            f"GoogleTest wrapper for {producer!r}"
        )
    return Path(command[0]).resolve()


def _capture(
    build_dir: Path, preset: str, expected_sanitizer: str
) -> dict[str, object]:
    build_dir = build_dir.resolve()
    if not build_dir.is_dir():
        raise SelectionError(f"configured build directory is missing: {build_dir}")
    if not preset or Path(preset).is_absolute():
        raise SelectionError("--preset must be a nonempty preset name, not a path")

    cache = _read_cache(build_dir)
    sanitizer = _sanitizer_identity(cache, expected_sanitizer)
    inventory_dir = build_dir / "test-inventories"
    registered = _read_tsv(inventory_dir / "RegisteredTestTargets.tsv")
    members = _read_aggregate(inventory_dir / f"{AGGREGATE}.txt")

    unknown = sorted(set(members) - registered.keys())
    expected_members = {
        target
        for target, labels in registered.items()
        if not set(labels).intersection(EXCLUDED_LABELS)
    }
    missing = sorted(expected_members - set(members))
    extra = sorted(set(members) - expected_members)
    if unknown or missing or extra:
        raise SelectionError(
            f"{AGGREGATE} disagrees with the canonical exclusion-only predicate: "
            f"unknown={unknown!r}, missing={missing!r}, extra={extra!r}"
        )

    binaries: dict[Path, str] = {}
    for target in registered:
        for suffix in ("", ".exe"):
            path = Path(
                os.path.realpath(
                    os.path.abspath(build_dir / "bin" / f"{target}{suffix}")
                )
            )
            previous = binaries.get(path)
            if previous is not None and previous != target:
                raise SelectionError(
                    f"registered producer paths collide: {previous!r} and {target!r}"
                )
            binaries[path] = target

    document = _ctest_document(build_dir)
    seen_names: set[str] = set()
    selected_tests: list[dict[str, object]] = []
    producer_case_counts = Counter[str]()
    representations: dict[str, set[str]] = {}
    discovered_cases: dict[str, dict[str, bool]] = {}
    discovered_binaries: dict[str, Path] = {}
    for raw_test in document["tests"]:
        if not isinstance(raw_test, dict):
            raise SelectionError("CTest JSON contains a malformed test record")
        name = raw_test.get("name")
        if not isinstance(name, str) or not name:
            raise SelectionError("CTest JSON contains a test without a name")
        if name in seen_names:
            raise SelectionError(f"CTest inventory repeats test name {name!r}")
        seen_names.add(name)

        labels = _labels(raw_test)
        raw_command = raw_test.get("command")
        command: list[str] | None = None
        if isinstance(raw_command, list) and all(
            isinstance(argument, str) for argument in raw_command
        ):
            command = raw_command
        producer = (
            _producer_for_command(command, binaries, name)
            if command is not None
            else None
        )
        if producer is not None and labels != registered[producer]:
            raise SelectionError(
                f"CTest test {name!r} labels differ from producer {producer!r}: "
                f"expected={registered[producer]!r}, actual={labels!r}"
            )

        if set(labels).intersection(EXCLUDED_LABELS):
            continue
        if command is None:
            raise SelectionError(f"selected CTest test {name!r} has no valid command")
        if producer is None:
            raise SelectionError(
                f"selected CTest test {name!r} does not map to a registered producer"
            )
        if producer not in members:
            raise SelectionError(
                f"selected CTest test {name!r} maps outside {AGGREGATE}: {producer!r}"
            )

        gtest_arguments = [
            argument for argument in command if argument.startswith("--gtest_")
        ]
        if name.endswith(".Grouped"):
            binary = _validate_grouped_command(
                command, producer, binaries, name, build_dir
            )
            if _disabled(raw_test):
                raise SelectionError(
                    f"canonical grouped GoogleTest wrapper {name!r} is disabled"
                )
            representations.setdefault(producer, set()).add("grouped")
            listed_cases = _run_gtest_listing(binary, producer)
            for case in listed_cases:
                selected_tests.append(
                    {
                        "disabled": _gtest_case_disabled(case),
                        "labels": list(labels),
                        "name": case,
                        "producer": producer,
                    }
                )
                producer_case_counts[producer] += 1
            continue

        if gtest_arguments:
            binary, case = _validate_discovered_command(
                command, producer, binaries, name
            )
            disabled = _disabled(raw_test)
            expected_disabled = _gtest_case_disabled(case)
            if disabled != expected_disabled:
                raise SelectionError(
                    f"CTest test {name!r} disabled state differs from raw "
                    f"GoogleTest identity {case!r}: expected={expected_disabled!r}, "
                    f"actual={disabled!r}"
                )
            previous_binary = discovered_binaries.setdefault(producer, binary)
            if previous_binary != binary:
                raise SelectionError(
                    f"discovered producer {producer!r} maps to multiple binaries"
                )
            cases = discovered_cases.setdefault(producer, {})
            if case in cases:
                raise SelectionError(
                    f"producer {producer!r} repeats raw GoogleTest identity {case!r}"
                )
            cases[case] = disabled
            representations.setdefault(producer, set()).add("discovered")
            selected_tests.append(
                {
                    "disabled": disabled,
                    "labels": list(labels),
                    "name": case,
                    "producer": producer,
                }
            )
            producer_case_counts[producer] += 1
            continue

        representations.setdefault(producer, set()).add("manual")
        selected_tests.append(
            {
                "disabled": _disabled(raw_test),
                "labels": list(labels),
                "name": name,
                "producer": producer,
            }
        )
        producer_case_counts[producer] += 1

    if not selected_tests:
        raise SelectionError(
            "canonical exclusion-only selector selected zero CTest cases"
        )
    conflicting_representations = {
        producer: sorted(kinds)
        for producer, kinds in representations.items()
        if len(kinds) != 1
    }
    if conflicting_representations:
        raise SelectionError(
            "registered producers mix CTest representations: "
            f"{conflicting_representations!r}"
        )
    for producer, cases in discovered_cases.items():
        listed = set(_run_gtest_listing(discovered_binaries[producer], producer))
        registered_cases = set(cases)
        missing = sorted(listed - registered_cases)
        extra = sorted(registered_cases - listed)
        if missing or extra:
            raise SelectionError(
                f"{producer}: discovered CTest cases disagree with "
                f"--gtest_list_tests: missing={missing!r}, extra={extra!r}"
            )
    producers_without_cases = sorted(set(members) - producer_case_counts.keys())
    if producers_without_cases:
        raise SelectionError(
            f"{AGGREGATE} producers have no selected CTest cases: "
            f"{producers_without_cases!r}"
        )
    selected_tests.sort(
        key=lambda record: (str(record["producer"]), str(record["name"]))
    )
    logical_identities = [
        (str(record["producer"]), str(record["name"])) for record in selected_tests
    ]
    if len(logical_identities) != len(set(logical_identities)):
        duplicates = sorted(
            identity
            for identity, count in Counter(logical_identities).items()
            if count > 1
        )
        raise SelectionError(
            f"canonical CPU selection repeats logical identities: {duplicates!r}"
        )

    producers = [
        {
            "labels": list(registered[target]),
            "name": target,
            "selected_test_count": producer_case_counts[target],
        }
        for target in members
    ]
    normalized = {
        "aggregate": AGGREGATE,
        "excluded_labels": list(EXCLUDED_LABELS),
        "producers": producers,
        "tests": selected_tests,
    }
    report: dict[str, object] = {
        "identity": {
            "preset": preset,
            "sanitizer": sanitizer,
            "selector": {
                "kind": "exclude-labels",
                "labels": list(EXCLUDED_LABELS),
            },
        },
        "schema": REPORT_SCHEMA,
        "selection": {
            "digest": _digest(normalized),
            "normalized": normalized,
        },
        "summary": {
            "disabled_test_count": sum(
                bool(record["disabled"]) for record in selected_tests
            ),
            "producer_count": len(producers),
            "selected_test_count": len(selected_tests),
        },
    }
    if str(build_dir) in _canonical_json(report).decode("ascii"):
        raise SelectionError("normalized CPU selection leaked its absolute build path")
    return report


def _load_report(path: Path) -> dict[str, object]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise SelectionError(
            f"cannot read CPU selection report {path}: {error}"
        ) from error
    except json.JSONDecodeError as error:
        raise SelectionError(
            f"CPU selection report {path} is invalid JSON: {error}"
        ) from error
    if not isinstance(report, dict) or report.get("schema") != REPORT_SCHEMA:
        raise SelectionError(f"{path}: expected schema {REPORT_SCHEMA!r}")

    identity = report.get("identity")
    selection = report.get("selection")
    summary = report.get("summary")
    if not isinstance(identity, dict):
        raise SelectionError(f"{path}: identity must be an object")
    if not isinstance(selection, dict) or not isinstance(
        selection.get("normalized"), dict
    ):
        raise SelectionError(f"{path}: selection.normalized must be an object")
    if not isinstance(summary, dict):
        raise SelectionError(f"{path}: summary must be an object")
    sanitizer = identity.get("sanitizer")
    preset = identity.get("preset")
    selector = identity.get("selector")
    if sanitizer not in SANITIZER_IDENTITIES:
        raise SelectionError(f"{path}: invalid sanitizer identity {sanitizer!r}")
    if not isinstance(preset, str) or not preset or Path(preset).is_absolute():
        raise SelectionError(f"{path}: invalid preset identity {preset!r}")
    if selector != {"kind": "exclude-labels", "labels": list(EXCLUDED_LABELS)}:
        raise SelectionError(f"{path}: selector identity is not canonical")

    normalized = selection["normalized"]
    digest = selection.get("digest")
    if digest != _digest(normalized):
        raise SelectionError(f"{path}: selection digest does not match normalized data")
    producers, tests = _validate_normalized(path, normalized)
    expected_summary = {
        "disabled_test_count": sum(bool(record["disabled"]) for record in tests),
        "producer_count": len(producers),
        "selected_test_count": len(tests),
    }
    if summary != expected_summary:
        raise SelectionError(
            f"{path}: summary disagrees with normalized selection: "
            f"expected={expected_summary!r}, actual={summary!r}"
        )
    return report


def _validate_labels(path: Path, context: str, value: object) -> tuple[str, ...]:
    if not isinstance(value, list) or any(
        not isinstance(label, str) or not label or Path(label).is_absolute()
        for label in value
    ):
        raise SelectionError(f"{path}: {context} has invalid labels")
    labels = tuple(value)
    if labels != tuple(sorted(labels)) or len(labels) != len(set(labels)):
        raise SelectionError(f"{path}: {context} labels must be sorted and unique")
    return labels


def _validate_normalized(
    path: Path, normalized: Mapping[str, object]
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    if (
        set(normalized) != {"aggregate", "excluded_labels", "producers", "tests"}
        or normalized.get("aggregate") != AGGREGATE
        or normalized.get("excluded_labels") != list(EXCLUDED_LABELS)
    ):
        raise SelectionError(f"{path}: normalized selection identity is not canonical")
    raw_producers = normalized.get("producers")
    raw_tests = normalized.get("tests")
    if (
        not isinstance(raw_producers, list)
        or not raw_producers
        or not isinstance(raw_tests, list)
        or not raw_tests
    ):
        raise SelectionError(f"{path}: normalized selection has invalid structure")

    producers: list[dict[str, object]] = []
    producer_names: list[str] = []
    expected_counts: dict[str, int] = {}
    producer_labels: dict[str, tuple[str, ...]] = {}
    for index, record in enumerate(raw_producers):
        context = f"producer record {index}"
        if not isinstance(record, dict) or set(record) != {
            "labels",
            "name",
            "selected_test_count",
        }:
            raise SelectionError(f"{path}: malformed {context}")
        name = record.get("name")
        count = record.get("selected_test_count")
        if (
            not isinstance(name, str)
            or not name
            or Path(name).is_absolute()
            or not isinstance(count, int)
            or isinstance(count, bool)
            or count < 1
        ):
            raise SelectionError(f"{path}: invalid {context} identity or count")
        labels = _validate_labels(path, context, record.get("labels"))
        if set(labels).intersection(EXCLUDED_LABELS):
            raise SelectionError(f"{path}: {context} carries an excluded label")
        producers.append(record)
        producer_names.append(name)
        expected_counts[name] = count
        producer_labels[name] = labels
    if producer_names != sorted(set(producer_names)):
        raise SelectionError(f"{path}: producer records must be sorted and unique")

    tests: list[dict[str, object]] = []
    test_identities: list[tuple[str, str]] = []
    actual_counts = Counter[str]()
    for index, record in enumerate(raw_tests):
        context = f"test record {index}"
        if not isinstance(record, dict) or set(record) != {
            "disabled",
            "labels",
            "name",
            "producer",
        }:
            raise SelectionError(f"{path}: malformed {context}")
        name = record.get("name")
        producer = record.get("producer")
        disabled = record.get("disabled")
        if (
            not isinstance(name, str)
            or not name
            or Path(name).is_absolute()
            or not isinstance(producer, str)
            or producer not in producer_labels
            or not isinstance(disabled, bool)
        ):
            raise SelectionError(f"{path}: invalid {context} identity")
        labels = _validate_labels(path, context, record.get("labels"))
        if labels != producer_labels[producer]:
            raise SelectionError(f"{path}: {context} labels differ from its producer")
        tests.append(record)
        test_identities.append((producer, name))
        actual_counts[producer] += 1
    if test_identities != sorted(set(test_identities)):
        raise SelectionError(
            f"{path}: test records must be sorted and unique by producer/name"
        )
    if dict(actual_counts) != expected_counts:
        raise SelectionError(
            f"{path}: producer selected-test counts disagree with test records"
        )
    return producers, tests


def _selection_names(normalized: Mapping[str, object], key: str) -> set[str]:
    records = normalized.get(key, [])
    if not isinstance(records, list):
        return set()
    return {
        str(record["name"])
        for record in records
        if isinstance(record, dict) and isinstance(record.get("name"), str)
    }


def _selection_test_identities(
    normalized: Mapping[str, object],
) -> set[tuple[str, str]]:
    records = normalized.get("tests", [])
    if not isinstance(records, list):
        return set()
    return {
        (str(record["producer"]), str(record["name"]))
        for record in records
        if isinstance(record, dict)
        and isinstance(record.get("producer"), str)
        and isinstance(record.get("name"), str)
    }


def _render_test_identities(
    identities: set[tuple[str, str]],
) -> list[str]:
    return [f"{producer}:{name}" for producer, name in sorted(identities)]


def _compare(
    report_paths: Sequence[Path], required_sanitizers: Sequence[str]
) -> dict[str, object]:
    if len(report_paths) < 2:
        raise SelectionError("compare requires at least two --report inputs")
    reports = [_load_report(path) for path in report_paths]

    required_counts = Counter(required_sanitizers)
    duplicates = sorted(name for name, count in required_counts.items() if count > 1)
    if duplicates:
        raise SelectionError(f"--require-sanitizer repeats identities {duplicates!r}")
    observed_sanitizers = Counter(
        str(report["identity"]["sanitizer"]) for report in reports
    )
    if required_counts and observed_sanitizers != required_counts:
        raise SelectionError(
            "sanitizer report set does not match required variants: "
            f"required={dict(sorted(required_counts.items()))!r}, "
            f"observed={dict(sorted(observed_sanitizers.items()))!r}"
        )

    baseline = reports[0]["selection"]["normalized"]
    baseline_digest = reports[0]["selection"]["digest"]
    for index, report in enumerate(reports[1:], start=2):
        normalized = report["selection"]["normalized"]
        if normalized == baseline:
            continue
        missing_producers = sorted(
            _selection_names(baseline, "producers")
            - _selection_names(normalized, "producers")
        )
        extra_producers = sorted(
            _selection_names(normalized, "producers")
            - _selection_names(baseline, "producers")
        )
        missing_tests = _render_test_identities(
            _selection_test_identities(baseline)
            - _selection_test_identities(normalized)
        )
        extra_tests = _render_test_identities(
            _selection_test_identities(normalized)
            - _selection_test_identities(baseline)
        )
        raise SelectionError(
            f"report {index} selection differs from report 1: "
            f"missing_producers={missing_producers!r}, "
            f"extra_producers={extra_producers!r}, "
            f"missing_tests={missing_tests!r}, extra_tests={extra_tests!r}"
        )

    variants = [
        {
            "preset": report["identity"]["preset"],
            "sanitizer": report["identity"]["sanitizer"],
        }
        for report in reports
    ]
    return {
        "schema": PARITY_SCHEMA,
        "selection_digest": baseline_digest,
        "summary": {
            "producer_count": reports[0]["summary"]["producer_count"],
            "selected_test_count": reports[0]["summary"]["selected_test_count"],
            "variant_count": len(reports),
        },
        "variants": variants,
    }


def _publish_outputs(values: Mapping[str, object]) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        return
    try:
        with Path(output_path).open("a", encoding="utf-8") as output:
            for key, value in values.items():
                output.write(f"{key}={value}\n")
    except OSError as error:
        raise SelectionError(f"cannot publish GITHUB_OUTPUT values: {error}") from error


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Capture and compare the exact canonical exclusion-only CPU CTest "
            "selection without build-path identity."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture")
    capture.add_argument("--build-dir", required=True, type=Path)
    capture.add_argument("--preset", required=True)
    capture.add_argument(
        "--expected-sanitizer",
        "--sanitizer",
        dest="expected_sanitizer",
        required=True,
        choices=SANITIZER_IDENTITIES,
    )
    capture.add_argument("--output", required=True, type=Path)

    compare = subparsers.add_parser("compare")
    compare.add_argument("--report", required=True, action="append", type=Path)
    compare.add_argument(
        "--require-sanitizer",
        action="append",
        default=[],
        choices=SANITIZER_IDENTITIES,
    )
    compare.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if arguments.command == "capture":
            report = _capture(
                arguments.build_dir,
                arguments.preset,
                arguments.expected_sanitizer,
            )
            _write_json(arguments.output, report)
            _publish_outputs(
                {
                    "preset": report["identity"]["preset"],
                    "producer-count": report["summary"]["producer_count"],
                    "report": arguments.output.resolve(),
                    "sanitizer": report["identity"]["sanitizer"],
                    "selected-test-count": report["summary"]["selected_test_count"],
                    "selection-digest": report["selection"]["digest"],
                }
            )
        else:
            report = _compare(arguments.report, arguments.require_sanitizer)
            _write_json(arguments.output, report)
            _publish_outputs(
                {
                    "report": arguments.output.resolve(),
                    "selected-test-count": report["summary"]["selected_test_count"],
                    "selection-digest": report["selection_digest"],
                    "variant-count": report["summary"]["variant_count"],
                }
            )
    except SelectionError as error:
        print(f"BLOCKED: {error}", file=sys.stderr)
        return 3
    if arguments.command == "capture":
        console = {
            "identity": report["identity"],
            "schema": report["schema"],
            "selection_digest": report["selection"]["digest"],
            "summary": report["summary"],
        }
    else:
        console = report
    print(json.dumps(console, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
