#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Mapping, Sequence


CASE_BASELINE_PATH = Path(__file__).with_name("Test.TestGateRouting.baseline.tsv")
REPO_ROOT = Path(__file__).resolve().parents[3]


class ReconciliationError(RuntimeError):
    pass


@dataclass(frozen=True)
class SourceOwner:
    source: str
    object_library: str
    target: str


EXCLUDED_FROM_CPU = frozenset({"gpu", "vulkan", "slow", "flaky-quarantine"})
REQUIRED_GTEST_ENVIRONMENT = frozenset(
    {
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:detect_leaks=0",
        "ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0",
    }
)
ALLOWED_TEST_LABELS = frozenset(
    {
        "assets",
        "benchmark",
        "build",
        "contract",
        "core",
        "ecs",
        "flaky-quarantine",
        "geometry",
        "glfw",
        "gpu",
        "graphics",
        "headless",
        "integration",
        "physics",
        "platform",
        "regression",
        "runtime",
        "slo",
        "slow",
        "unit",
        "vulkan",
    }
)

CPU_SOURCE_OWNERS = {
    "Test.AssetLoadPipeline.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
    "Test.AssetService.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
    "Test.CoreFrameGraphParallel.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
    "Test.CoreGraphInterfaces.cpp": (
        "RuntimeContractTestObjs",
        "IntrinsicRuntimeContractTests",
    ),
    "Test.CoreGraphStress.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
    "Test.CoreProcess.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
    "Test.GpuWorldAndCulling.cpp": (
        "GraphicsIntegrationCpuTestObjs",
        "IntrinsicGraphicsIntegrationCpuTests",
    ),
    "Test.RenderGraphLegacy.cpp": (
        "GraphicsIntegrationCpuTestObjs",
        "IntrinsicGraphicsIntegrationCpuTests",
    ),
    "Test.RendererLegacy.cpp": (
        "GraphicsIntegrationCpuTestObjs",
        "IntrinsicGraphicsIntegrationCpuTests",
    ),
    "Test.RuntimeEngineLayering.cpp": (
        "RuntimeContractTestObjs",
        "IntrinsicRuntimeContractTests",
    ),
    "Test.RuntimeFrameLoopContract.cpp": (
        "RuntimeGraphicsCpuTestObjs",
        "IntrinsicRuntimeGraphicsCpuTests",
    ),
    "Test.RuntimeStreamingExecutor.cpp": (
        "RuntimeIntegrationCpuTestObjs",
        "IntrinsicRuntimeIntegrationTests",
    ),
}
CPU_RECLASSIFIED_SOURCES = frozenset(CPU_SOURCE_OWNERS)
RHI_MANAGER_SOURCES = frozenset(
    {
        "Test.RHI.BufferManager.cpp",
        "Test.RHI.SamplerManager.cpp",
        "Test.RHI.TextureManager.cpp",
    }
)
READBACK_SOURCE = "Test.GpuReadbackJobGpuSmoke.cpp"
READBACK_TARGET = "IntrinsicRuntimeGpuReadbackSmokeTests"
FRAME_LOOP_SOURCE = "Test.RuntimeFrameLoopContract.cpp"
GROUPED_PURE_CTEST_TARGETS = frozenset(
    {
        "IntrinsicGeometryTests",
        "IntrinsicGeometryMethodTests",
        "IntrinsicGraphicsBufferTransferTests",
        "IntrinsicPhysicsMethodTests",
        "IntrinsicPhysicsWorldTests",
    }
)
GEOMETRY_IO_SOURCE = "tests/unit/geometry/Test.GeometryIO.cpp"
GEOMETRY_IO_OWNER = SourceOwner(
    GEOMETRY_IO_SOURCE,
    "GeometryIoTestObjs",
    "IntrinsicGeometryIoTests",
)
MANUAL_CTEST_TARGETS = frozenset(
    {
        "IntrinsicBenchmarkSmoke",
        "IntrinsicGlfwLifecycleLsanProcess",
        "IntrinsicKMeansGpuBenchmarkSmoke",
    }
)
AFFECTED_DEDICATED_TARGETS = frozenset(
    {
        "IntrinsicGraphicsIntegrationCpuTests",
        "IntrinsicGraphicsUnitTests",
        "IntrinsicRuntimeGpuReadbackSmokeTests",
        "IntrinsicRuntimeIntegrationTests",
    }
)
AFFECTED_SHARED_SUITES = {
    "IntrinsicRuntimeContractTests": frozenset(
        {"CoreGraphInterfaces", "RuntimeEngineLayering"}
    ),
    "IntrinsicRuntimeGraphicsCpuTests": frozenset({"RuntimeFrameLoopContract"}),
}
AFFECTED_TARGET_CASE_COUNTS = {
    "IntrinsicGraphicsIntegrationCpuTests": 74,
    "IntrinsicGraphicsUnitTests": 20,
    "IntrinsicRuntimeContractTests": 25,
    "IntrinsicRuntimeGpuReadbackSmokeTests": 1,
    "IntrinsicRuntimeGraphicsCpuTests": 9,
    "IntrinsicRuntimeIntegrationTests": 104,
}
EXPECTED_AFFECTED_CASE_COUNT = 233

AggregatePredicate = Callable[[frozenset[str]], bool]


def _all_targets(_labels: frozenset[str]) -> bool:
    return True


def _pr_fast(labels: frozenset[str]) -> bool:
    return bool(labels.intersection({"unit", "contract"})) and not bool(
        labels.intersection(EXCLUDED_FROM_CPU)
    )


def _cpu(labels: frozenset[str]) -> bool:
    return not bool(labels.intersection(EXCLUDED_FROM_CPU))


def _cpu_coverage(labels: frozenset[str]) -> bool:
    return not bool(
        labels.intersection(
            {"benchmark", "slo", "gpu", "vulkan", "flaky-quarantine"}
        )
    )


def _cpu_slow(labels: frozenset[str]) -> bool:
    return "slow" in labels and not bool(
        labels.intersection(
            {"benchmark", "slo", "gpu", "vulkan", "flaky-quarantine"}
        )
    )


def _gpu_vulkan(labels: frozenset[str]) -> bool:
    return {"gpu", "vulkan"}.issubset(labels) and not bool(
        labels.intersection({"slow", "flaky-quarantine"})
    )


def _pr_smoke(labels: frozenset[str]) -> bool:
    return {"integration", "runtime", "graphics"}.issubset(labels) and not bool(
        labels.intersection(EXCLUDED_FROM_CPU)
    )


AGGREGATE_PREDICATES: dict[str, AggregatePredicate] = {
    "IntrinsicTests": _all_targets,
    "IntrinsicPrFastTests": _pr_fast,
    "IntrinsicCpuTests": _cpu,
    "IntrinsicCpuCoverageTests": _cpu_coverage,
    "IntrinsicCpuSlowTests": _cpu_slow,
    "IntrinsicGpuVulkanTests": _gpu_vulkan,
    "IntrinsicPrSmokeTests": _pr_smoke,
}


def _read_tsv(path: Path, expected_fields: Sequence[str]) -> list[dict[str, str]]:
    try:
        handle = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise ReconciliationError(f"cannot read registry {path}: {error}") from error

    with handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != list(expected_fields):
            raise ReconciliationError(
                f"{path}: expected TSV header {list(expected_fields)!r}, "
                f"got {reader.fieldnames!r}"
            )
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, start=2):
            if None in row:
                raise ReconciliationError(
                    f"{path}:{line_number}: unexpected extra TSV fields"
                )
            if any(not row[field] for field in expected_fields):
                raise ReconciliationError(
                    f"{path}:{line_number}: registry fields must not be empty"
                )
            rows.append(row)
    return rows


def _read_target_registry(path: Path) -> dict[str, frozenset[str]]:
    targets: dict[str, frozenset[str]] = {}
    for row in _read_tsv(path, ("target", "labels")):
        target = row["target"]
        if target in targets:
            raise ReconciliationError(f"{path}: duplicate target row {target!r}")

        encoded_labels = row["labels"].split(",")
        labels = frozenset(encoded_labels)
        if len(labels) != len(encoded_labels) or "" in labels:
            raise ReconciliationError(
                f"{path}: target {target!r} has duplicate or empty labels"
            )
        unknown = labels - ALLOWED_TEST_LABELS
        if unknown:
            raise ReconciliationError(
                f"{path}: target {target!r} has unknown labels "
                f"{_format_values(unknown)}"
            )
        targets[target] = labels

    if not targets:
        raise ReconciliationError(f"{path}: target registry is empty")
    return targets


def _normalize_source(path: str, registry_path: Path) -> str:
    pure_path = PurePosixPath(path)
    if pure_path.is_absolute() or ".." in pure_path.parts:
        raise ReconciliationError(
            f"{registry_path}: source path must be repository-relative: {path!r}"
        )
    normalized = pure_path.as_posix()
    if normalized != path or normalized in {"", "."}:
        raise ReconciliationError(
            f"{registry_path}: source path is not normalized: {path!r}"
        )
    return normalized


def _read_source_registry(path: Path) -> dict[str, SourceOwner]:
    owners: dict[str, SourceOwner] = {}
    for row in _read_tsv(path, ("source", "object_library", "target")):
        source = _normalize_source(row["source"], path)
        owner = SourceOwner(source, row["object_library"], row["target"])
        previous = owners.get(source)
        if previous is not None:
            raise ReconciliationError(
                f"{path}: duplicate source ownership for {source!r}: "
                f"{previous.object_library}/{previous.target} and "
                f"{owner.object_library}/{owner.target}"
            )
        owners[source] = owner

    if not owners:
        raise ReconciliationError(f"{path}: source registry is empty")
    return owners


def _read_aggregate(path: Path) -> frozenset[str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ReconciliationError(f"cannot read aggregate {path}: {error}") from error
    if not lines:
        raise ReconciliationError(f"{path}: aggregate inventory is empty")
    if any(not line.strip() or line != line.strip() for line in lines):
        raise ReconciliationError(f"{path}: aggregate entries must be non-empty names")
    members = frozenset(lines)
    if len(members) != len(lines):
        duplicates = sorted(
            target for target, count in Counter(lines).items() if count > 1
        )
        raise ReconciliationError(
            f"{path}: duplicate aggregate members {_format_values(duplicates)}"
        )
    return members


def _expected_aggregate_members(
    targets: Mapping[str, frozenset[str]], aggregate: str
) -> frozenset[str]:
    predicate = AGGREGATE_PREDICATES.get(aggregate)
    if predicate is None:
        raise ReconciliationError(
            f"unknown aggregate {aggregate!r}; expected one of "
            f"{_format_values(AGGREGATE_PREDICATES)}"
        )
    return frozenset(target for target, labels in targets.items() if predicate(labels))


def _verify_aggregate_membership(
    targets: Mapping[str, frozenset[str]],
    aggregate: str,
    actual_members: frozenset[str],
) -> None:
    expected_members = _expected_aggregate_members(targets, aggregate)
    missing = expected_members - actual_members
    extra = actual_members - expected_members
    if missing or extra:
        raise ReconciliationError(
            f"{aggregate} disagrees with canonical label selection; "
            f"missing={_format_values(missing)}, extra={_format_values(extra)}"
        )


def _owners_by_basename(
    sources: Mapping[str, SourceOwner],
) -> dict[str, list[SourceOwner]]:
    owners: dict[str, list[SourceOwner]] = defaultdict(list)
    for source_owner in sources.values():
        owners[PurePosixPath(source_owner.source).name].append(source_owner)
    return owners


def _require_affected_owner(
    owners: Mapping[str, list[SourceOwner]], basename: str
) -> SourceOwner:
    matches = owners.get(basename, [])
    if len(matches) != 1:
        rendered = [owner.source for owner in matches]
        raise ReconciliationError(
            f"affected source {basename!r} must have exactly one owner; "
            f"found {_format_values(rendered)}"
        )
    return matches[0]


def _validate_affected_contract(
    targets: Mapping[str, frozenset[str]],
    sources: Mapping[str, SourceOwner],
) -> None:
    owners = _owners_by_basename(sources)
    for basename in sorted(CPU_RECLASSIFIED_SOURCES):
        owner = _require_affected_owner(owners, basename)
        expected_object, expected_target = CPU_SOURCE_OWNERS[basename]
        if owner.object_library != expected_object or owner.target != expected_target:
            raise ReconciliationError(
                f"{basename} must be owned by {expected_object}/{expected_target}, "
                f"got {owner.object_library}/{owner.target}"
            )
        labels = targets.get(owner.target)
        if labels is None:
            raise ReconciliationError(
                f"affected source {owner.source!r} names unregistered target "
                f"{owner.target!r}"
            )
        if not _cpu(labels):
            raise ReconciliationError(
                f"CPU source {owner.source!r} routes to {owner.target!r} with "
                f"excluded labels {_format_values(labels.intersection(EXCLUDED_FROM_CPU))}"
            )

    for basename in sorted(RHI_MANAGER_SOURCES):
        owner = _require_affected_owner(owners, basename)
        if (
            owner.object_library != "GraphicsUnitTestObjs"
            or owner.target != "IntrinsicGraphicsUnitTests"
        ):
            raise ReconciliationError(
                f"{basename} must be owned by "
                "GraphicsUnitTestObjs/IntrinsicGraphicsUnitTests, got "
                f"{owner.object_library}/{owner.target}"
            )
        labels = targets.get(owner.target)
        if labels != {"unit", "graphics"}:
            raise ReconciliationError(
                f"{owner.target} must have exactly unit,graphics labels, got "
                f"{_format_values(labels or ())}"
            )

    if READBACK_TARGET not in targets:
        unexpected_readback = owners.get(READBACK_SOURCE, [])
        if unexpected_readback:
            raise ReconciliationError(
                f"{READBACK_SOURCE} is registered without capability target "
                f"{READBACK_TARGET}"
            )
        return

    readback = _require_affected_owner(owners, READBACK_SOURCE)
    if (
        readback.object_library != "RuntimeGpuReadbackSmokeTestObjs"
        or readback.target != READBACK_TARGET
    ):
        raise ReconciliationError(
            f"{READBACK_SOURCE} must be owned by "
            f"RuntimeGpuReadbackSmokeTestObjs/{READBACK_TARGET}, "
            f"got {readback.object_library}/{readback.target}"
        )
    readback_labels = targets.get(readback.target)
    expected_readback_labels = {
        "gpu",
        "vulkan",
        "integration",
        "runtime",
        "graphics",
        "slow",
    }
    if readback_labels != expected_readback_labels:
        raise ReconciliationError(
            f"{readback.target} must have exactly "
            f"{_format_values(expected_readback_labels)} labels, got "
            f"{_format_values(readback_labels or ())}"
        )
    target_sources = [
        owner for owner in sources.values() if owner.target == readback.target
    ]
    if target_sources != [readback]:
        raise ReconciliationError(
            f"{readback.target} must be dedicated to {READBACK_SOURCE}; "
            f"owns {_format_values(owner.source for owner in target_sources)}"
        )


def _validate_grouped_source_contract(
    sources: Mapping[str, SourceOwner],
) -> None:
    actual = sources.get(GEOMETRY_IO_SOURCE)
    if actual != GEOMETRY_IO_OWNER:
        raise ReconciliationError(
            f"{GEOMETRY_IO_SOURCE} must be owned by "
            f"{GEOMETRY_IO_OWNER.object_library}/{GEOMETRY_IO_OWNER.target}, "
            f"got {actual!r}"
        )
    if GEOMETRY_IO_OWNER.target in GROUPED_PURE_CTEST_TARGETS:
        raise ReconciliationError("Geometry IO must remain outside the grouped cohort")


_SUITE_RE = re.compile(r"^(?P<suite>\S+)\.\s*(?:#.*)?$")
_COMMENT_RE = re.compile(r"\s+#.*$")


def _parse_gtest_listing(output: str, target: str) -> tuple[str, ...]:
    suite: str | None = None
    cases: list[str] = []
    for line_number, line in enumerate(output.splitlines(), start=1):
        if not line.strip():
            continue
        if line.startswith("  "):
            if suite is None:
                raise ReconciliationError(
                    f"{target}: GoogleTest case appears before a suite "
                    f"at output line {line_number}"
                )
            test = _COMMENT_RE.sub("", line.strip())
            if not test or any(character.isspace() for character in test):
                raise ReconciliationError(
                    f"{target}: malformed GoogleTest case at output line "
                    f"{line_number}: {line!r}"
                )
            cases.append(f"{suite}.{test}")
            continue

        match = _SUITE_RE.fullmatch(line)
        if match:
            suite = match.group("suite")

    duplicates = sorted(case for case, count in Counter(cases).items() if count > 1)
    if duplicates:
        raise ReconciliationError(
            f"{target}: duplicate expanded GoogleTest names "
            f"{_format_values(duplicates)}"
        )
    if not cases:
        raise ReconciliationError(f"{target}: --gtest_list_tests returned no cases")
    return tuple(cases)


def _read_ctest_document(build_dir: Path) -> dict[str, object]:
    result = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=120,
    )
    if result.returncode != 0:
        raise ReconciliationError(
            f"CTest discovery failed with exit {result.returncode}:\n{result.stdout}"
        )
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ReconciliationError(f"CTest returned invalid JSON: {error}") from error
    if not isinstance(document, dict) or not isinstance(document.get("tests"), list):
        raise ReconciliationError("CTest JSON does not contain a tests array")
    return document


def _cache_bool(build_dir: Path, key: str) -> bool:
    declaration = re.compile(rf"^{re.escape(key)}:BOOL=(ON|OFF)$")
    try:
        lines = (build_dir / "CMakeCache.txt").read_text(
            encoding="utf-8"
        ).splitlines()
    except OSError as error:
        raise ReconciliationError(
            f"cannot read CMake cache under {build_dir}: {error}"
        ) from error
    matches = [
        match.group(1)
        for line in lines
        if (match := declaration.fullmatch(line)) is not None
    ]
    if len(matches) != 1:
        raise ReconciliationError(
            f"CMake cache must declare {key}:BOOL exactly once"
        )
    return matches[0] == "ON"


def _candidate_binary_paths(build_dir: Path, target: str) -> tuple[Path, Path]:
    binary = build_dir / "bin" / target
    return binary, Path(f"{binary}.exe")


def _existing_binary_path(build_dir: Path, target: str) -> Path | None:
    for candidate in _candidate_binary_paths(build_dir, target):
        if candidate.is_file():
            return candidate
    return None


def _binary_path(build_dir: Path, target: str) -> Path:
    binary = _existing_binary_path(build_dir, target)
    if binary is not None:
        return binary
    raise ReconciliationError(
        f"aggregate target {target!r} is not built under {build_dir / 'bin'}"
    )


def _resolved_path(value: str) -> Path:
    return Path(os.path.realpath(os.path.abspath(value)))


def _ctest_property(test: Mapping[str, object], name: str) -> list[object]:
    properties = test.get("properties", [])
    if not isinstance(properties, list):
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has malformed properties"
        )
    values: list[object] = []
    for property_record in properties:
        if not isinstance(property_record, dict):
            raise ReconciliationError(
                f"CTest registration {test.get('name')!r} has malformed properties"
            )
        if property_record.get("name") == name:
            value = property_record.get("value")
            if isinstance(value, list):
                values.extend(value)
            else:
                values.append(value)
    return values


def _ctest_labels(test: Mapping[str, object]) -> frozenset[str]:
    raw_labels = _ctest_property(test, "LABELS")
    invalid_types = [value for value in raw_labels if not isinstance(value, str)]
    if invalid_types:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has non-string labels "
            f"{invalid_types!r}"
        )
    labels = [value for value in raw_labels if isinstance(value, str)]
    unknown = set(labels) - ALLOWED_TEST_LABELS
    if unknown:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has unknown labels "
            f"{_format_values(unknown)}"
        )
    duplicates = sorted(label for label, count in Counter(labels).items() if count > 1)
    if duplicates:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has duplicate labels "
            f"{_format_values(duplicates)}"
        )
    return frozenset(labels)


def _verify_ctest_environment(test: Mapping[str, object]) -> None:
    raw_environment = _ctest_property(test, "ENVIRONMENT")
    invalid_types = [
        value for value in raw_environment if not isinstance(value, str)
    ]
    if invalid_types:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has non-string environment "
            f"entries {invalid_types!r}"
        )
    environment = [
        value for value in raw_environment if isinstance(value, str)
    ]
    malformed = sorted(value for value in environment if "=" not in value)
    if malformed:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has malformed environment "
            f"entries {_format_values(malformed)}"
        )
    duplicate_keys = sorted(
        key
        for key, count in Counter(
            value.partition("=")[0] for value in environment
        ).items()
        if count > 1
    )
    if duplicate_keys:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} has duplicate environment "
            f"keys {_format_values(duplicate_keys)}"
        )
    missing = REQUIRED_GTEST_ENVIRONMENT - set(environment)
    if missing:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} is missing required "
            f"environment entries {_format_values(missing)}"
        )


def _verify_ctest_label_route(
    test: Mapping[str, object],
    target: str,
    target_labels: Mapping[str, frozenset[str]],
    aggregate: str,
    aggregate_members: frozenset[str],
) -> None:
    labels = _ctest_labels(test)
    predicate = AGGREGATE_PREDICATES[aggregate]
    selected_by_ctest_labels = predicate(labels)
    selected_by_registry = target in aggregate_members
    if selected_by_ctest_labels != selected_by_registry:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} routes "
            f"{'into' if selected_by_ctest_labels else 'out of'} {aggregate}, "
            f"but target registry routes {target!r} "
            f"{'into' if selected_by_registry else 'out of'} it"
        )
    expected_labels = target_labels[target]
    if labels != expected_labels:
        raise ReconciliationError(
            f"CTest registration {test.get('name')!r} labels disagree with {target}; "
            f"expected={_format_values(expected_labels)}, "
            f"actual={_format_values(labels)}"
        )


def _verify_grouped_ctest_contract(
    test: Mapping[str, object],
    target: str,
    build_dir: Path,
    command: Sequence[object],
) -> None:
    name = test.get("name")
    expected_name = f"{target}.Grouped"
    if name != expected_name:
        raise ReconciliationError(
            f"grouped CTest registration for {target!r} must be named "
            f"{expected_name!r}, got {name!r}"
        )

    filters = [
        argument
        for argument in command
        if isinstance(argument, str) and argument.startswith("--gtest_filter=")
    ]
    if filters != ["--gtest_filter=*"]:
        raise ReconciliationError(
            f"{expected_name} must select the whole producer exactly once"
        )

    expected_xml = (
        f"--gtest_output=xml:{build_dir}/Testing/Temporary/{expected_name}.xml"
    )
    xml_outputs = [
        argument
        for argument in command
        if isinstance(argument, str)
        and argument.startswith("--gtest_output=xml:")
    ]
    if xml_outputs != [expected_xml]:
        raise ReconciliationError(
            f"{expected_name} must write one canonical GTest XML diagnostic"
        )

    if _ctest_property(test, "TIMEOUT") != [30.0]:
        raise ReconciliationError(
            f"{expected_name} must retain the canonical 30 second timeout"
        )

    working_directories = _ctest_property(test, "WORKING_DIRECTORY")
    expected_working_directory = _resolved_path(str(build_dir / "tests"))
    if (
        len(working_directories) != 1
        or not isinstance(working_directories[0], str)
        or _resolved_path(working_directories[0]) != expected_working_directory
    ):
        raise ReconciliationError(
            f"{expected_name} must run from {expected_working_directory}"
        )


def _registered_gtest_cases_by_target(
    document: Mapping[str, object],
    build_dir: Path,
    source_targets: frozenset[str],
    target_labels: Mapping[str, frozenset[str]],
    aggregate: str,
    aggregate_members: frozenset[str],
    expected_grouped_targets: frozenset[str] = frozenset(),
) -> dict[str, tuple[str, ...]]:
    path_to_target: dict[Path, str] = {}
    for target in source_targets:
        for candidate in _candidate_binary_paths(build_dir, target):
            path_to_target[_resolved_path(str(candidate))] = target

    cases: dict[str, list[str]] = defaultdict(list)
    representations: dict[str, set[str]] = defaultdict(set)
    placeholder_counts: Counter[str] = Counter()
    grouped_counts: Counter[str] = Counter()
    tests = document.get("tests")
    if not isinstance(tests, list):
        raise ReconciliationError("CTest JSON does not contain a tests array")

    for test in tests:
        if not isinstance(test, dict):
            raise ReconciliationError("CTest JSON contains a malformed test record")
        name = test.get("name")
        if not isinstance(name, str):
            raise ReconciliationError("CTest registration has no string name")
        if name.endswith("_NOT_BUILT"):
            target = name[: -len("_NOT_BUILT")]
            if target not in source_targets:
                continue
            _verify_ctest_label_route(
                test,
                target,
                target_labels,
                aggregate,
                aggregate_members,
            )
            representations[target].add("placeholder")
            placeholder_counts[target] += 1
            if target in aggregate_members:
                raise ReconciliationError(
                    f"requested aggregate contains undiscovered target {target!r}"
                )
            continue

        command = test.get("command")
        if not isinstance(command, list) or not command:
            continue
        if not isinstance(command[0], str):
            raise ReconciliationError(
                f"CTest registration {name!r} has a malformed command"
            )
        target = path_to_target.get(_resolved_path(command[0]))
        if target is None:
            continue

        discovered = "--gtest_also_run_disabled_tests" in command
        grouped = name == f"{target}.Grouped"
        if not discovered and not grouped:
            continue
        _verify_ctest_environment(test)
        _verify_ctest_label_route(
            test,
            target,
            target_labels,
            aggregate,
            aggregate_members,
        )
        if grouped:
            _verify_grouped_ctest_contract(test, target, build_dir, command)
            representations[target].add("grouped")
            grouped_counts[target] += 1
            if target in aggregate_members:
                cases[target].extend(
                    _run_gtest_listing(Path(command[0]), target)
                )
            continue

        representations[target].add("discovered")
        filters = [
            argument.removeprefix("--gtest_filter=")
            for argument in command
            if isinstance(argument, str) and argument.startswith("--gtest_filter=")
        ]
        if len(filters) != 1 or not filters[0]:
            raise ReconciliationError(
                f"CTest registration {name!r} for {target} must have one --gtest_filter"
            )
        cases[target].append(filters[0])

    for target in sorted(source_targets):
        kinds = representations.get(target, set())
        if not kinds:
            raise ReconciliationError(
                f"registered GoogleTest target {target!r} has neither discovered "
                "CTest cases nor a NOT_BUILT placeholder"
            )
        if len(kinds) > 1:
            raise ReconciliationError(
                f"registered GoogleTest target {target!r} has conflicting "
                f"CTest representations {_format_values(kinds)}"
            )
        if placeholder_counts[target] > 1:
            raise ReconciliationError(
                f"registered GoogleTest target {target!r} has duplicate "
                "NOT_BUILT placeholders"
            )
        if grouped_counts[target] > 1:
            raise ReconciliationError(
                f"registered GoogleTest target {target!r} has duplicate "
                "grouped CTest registrations"
            )

    actual_grouped_targets = frozenset(grouped_counts)
    if actual_grouped_targets != expected_grouped_targets:
        raise ReconciliationError(
            "grouped CTest cohort mismatch; "
            f"missing={_format_values(expected_grouped_targets - actual_grouped_targets)}, "
            f"extra={_format_values(actual_grouped_targets - expected_grouped_targets)}"
        )

    return {target: tuple(target_cases) for target, target_cases in cases.items()}


def _run_gtest_listing(binary: Path, target: str) -> tuple[str, ...]:
    environment = os.environ.copy()
    environment["GTEST_COLOR"] = "no"
    result = subprocess.run(
        [str(binary), "--gtest_list_tests"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=60,
        env=environment,
    )
    if result.returncode != 0:
        raise ReconciliationError(
            f"{target}: --gtest_list_tests failed with exit "
            f"{result.returncode}:\n{result.stdout}"
        )
    return _parse_gtest_listing(result.stdout, target)


def _verify_case_inventory(
    listed_by_target: Mapping[str, Sequence[str]],
    registered_by_target: Mapping[str, Sequence[str]],
) -> int:
    global_owners: dict[str, str] = {}
    case_count = 0
    for target in sorted(listed_by_target):
        listed = tuple(listed_by_target[target])
        registered = tuple(registered_by_target.get(target, ()))
        duplicate_registrations = sorted(
            case for case, count in Counter(registered).items() if count > 1
        )
        if duplicate_registrations:
            raise ReconciliationError(
                f"{target}: duplicate CTest case registrations "
                f"{_format_values(duplicate_registrations)}"
            )

        listed_set = frozenset(listed)
        registered_set = frozenset(registered)
        missing = listed_set - registered_set
        extra = registered_set - listed_set
        if missing or extra:
            raise ReconciliationError(
                f"{target}: GoogleTest/CTest case mismatch; "
                f"missing={_format_values(missing)}, extra={_format_values(extra)}"
            )

        for case in listed:
            previous_target = global_owners.get(case)
            if previous_target is not None:
                raise ReconciliationError(
                    f"duplicate expanded GoogleTest name {case!r} in "
                    f"{previous_target} and {target}"
                )
            global_owners[case] = target
            case_count += 1
    return case_count


def _format_values(values: Iterable[str], limit: int = 8) -> str:
    ordered = sorted(values)
    rendered = ", ".join(repr(value) for value in ordered[:limit])
    if len(ordered) > limit:
        rendered += f", ... (+{len(ordered) - limit})"
    return f"[{rendered}]"


CaseIdentity = tuple[str, str]


def _read_affected_case_baseline(path: Path) -> frozenset[CaseIdentity]:
    identities: list[CaseIdentity] = []
    for row in _read_tsv(path, ("target", "case")):
        target = row["target"]
        case = row["case"]
        if (
            target not in AFFECTED_TARGET_CASE_COUNTS
            or "." not in case
            or any(character.isspace() for character in case)
        ):
            raise ReconciliationError(
                f"{path}: invalid affected case identity {target!r}/{case!r}"
            )
        identities.append((target, case))

    baseline = frozenset(identities)
    if len(baseline) != len(identities):
        duplicates = sorted(
            identity
            for identity, count in Counter(identities).items()
            if count > 1
        )
        raise ReconciliationError(
            f"{path}: duplicate affected cases "
            f"{_format_case_identities(duplicates)}"
        )
    counts = Counter(target for target, _case in baseline)
    if counts != AFFECTED_TARGET_CASE_COUNTS:
        raise ReconciliationError(
            f"{path}: expected per-target counts {AFFECTED_TARGET_CASE_COUNTS!r}, "
            f"got {dict(counts)!r}"
        )
    if len(baseline) != EXPECTED_AFFECTED_CASE_COUNT:
        raise ReconciliationError(
            f"{path}: expected {EXPECTED_AFFECTED_CASE_COUNT} affected cases, "
            f"got {len(baseline)}"
        )
    return baseline


def _select_affected_cases(
    cases_by_target: Mapping[str, Sequence[str]],
) -> frozenset[CaseIdentity]:
    selected: set[CaseIdentity] = set()
    for target in AFFECTED_DEDICATED_TARGETS:
        selected.update((target, case) for case in cases_by_target.get(target, ()))
    for target, suites in AFFECTED_SHARED_SUITES.items():
        selected.update(
            (target, case)
            for case in cases_by_target.get(target, ())
            if case.partition(".")[0] in suites
        )
    return frozenset(selected)


def _verify_affected_case_baseline(
    expected: frozenset[CaseIdentity],
    actual: frozenset[CaseIdentity],
) -> None:
    missing = expected - actual
    extra = actual - expected
    if missing or extra:
        raise ReconciliationError(
            "BUG-106 affected case baseline mismatch; "
            f"missing={_format_case_identities(missing)}, "
            f"extra={_format_case_identities(extra)}"
        )


def _format_case_identities(
    identities: Iterable[CaseIdentity], limit: int = 8
) -> str:
    return _format_values(
        (f"{target}:{case}" for target, case in identities),
        limit=limit,
    )


def _classify_registered_targets(
    targets: Mapping[str, frozenset[str]],
    sources: Mapping[str, SourceOwner],
) -> frozenset[str]:
    source_targets = frozenset(owner.target for owner in sources.values())
    unknown_source_targets = source_targets - targets.keys()
    if unknown_source_targets:
        raise ReconciliationError(
            "source registry names unregistered targets "
            f"{_format_values(unknown_source_targets)}"
        )
    manual_with_sources = source_targets.intersection(MANUAL_CTEST_TARGETS)
    if manual_with_sources:
        raise ReconciliationError(
            "manual CTest targets unexpectedly own GoogleTest sources "
            f"{_format_values(manual_with_sources)}"
        )
    unclassified = targets.keys() - source_targets - MANUAL_CTEST_TARGETS
    if unclassified:
        raise ReconciliationError(
            "registered targets have no source ownership and are not classified "
            f"as manual CTest producers: {_format_values(unclassified)}"
        )
    return source_targets


def reconcile(build_dir: Path, aggregate: str) -> tuple[int, int, int]:
    build_dir = build_dir.resolve()
    inventory_dir = build_dir / "test-inventories"
    affected_baseline = _read_affected_case_baseline(CASE_BASELINE_PATH)
    targets = _read_target_registry(inventory_dir / "RegisteredTestTargets.tsv")
    sources = _read_source_registry(inventory_dir / "RegisteredTestSources.tsv")
    members = _read_aggregate(inventory_dir / f"{aggregate}.txt")

    _verify_aggregate_membership(targets, aggregate, members)
    source_backed_targets = _classify_registered_targets(targets, sources)
    _validate_affected_contract(targets, sources)
    _validate_grouped_source_contract(sources)

    inspected_targets = sorted(members.intersection(source_backed_targets))
    target_binaries = {
        target: _binary_path(build_dir, target) for target in inspected_targets
    }
    ctest_document = _read_ctest_document(build_dir)
    expected_grouped_targets = (
        GROUPED_PURE_CTEST_TARGETS
        if _cache_bool(build_dir, "INTRINSIC_GROUP_PURE_CTEST")
        else frozenset()
    )
    all_registered_cases = _registered_gtest_cases_by_target(
        ctest_document,
        build_dir,
        source_backed_targets,
        targets,
        aggregate,
        members,
        expected_grouped_targets,
    )
    expected_affected_targets = members & AFFECTED_TARGET_CASE_COUNTS.keys()
    if aggregate == "IntrinsicGpuVulkanTests":
        expected_affected_targets = expected_affected_targets | {
            "IntrinsicRuntimeGpuReadbackSmokeTests"
        }
    expected_discovered_cases = frozenset(
        identity
        for identity in affected_baseline
        if identity[0] in expected_affected_targets
    )
    actual_discovered_cases = _select_affected_cases(
        {
            target: cases
            for target, cases in all_registered_cases.items()
            if target in expected_affected_targets
        }
    )
    _verify_affected_case_baseline(
        expected_discovered_cases,
        actual_discovered_cases,
    )

    # The readback smoke is built explicitly because timing keeps it outside
    # the non-slow GPU aggregate. Reconcile lane-required affected binaries.
    for target in expected_affected_targets:
        binary = _existing_binary_path(build_dir, target)
        if binary is not None:
            target_binaries.setdefault(target, binary)
    registered_cases = {
        target: all_registered_cases.get(target, ()) for target in target_binaries
    }
    listed_cases = {
        target: _run_gtest_listing(binary, target)
        for target, binary in target_binaries.items()
    }
    case_count = _verify_case_inventory(listed_cases, registered_cases)
    return len(target_binaries), case_count, len(sources)


class TestGateRoutingSelfTests(unittest.TestCase):
    def test_aggregate_mismatch_is_rejected(self) -> None:
        targets = {
            "Cpu": frozenset({"unit", "core"}),
            "Gpu": frozenset({"gpu", "vulkan", "graphics"}),
        }
        with self.assertRaisesRegex(ReconciliationError, "missing=.*'Cpu'"):
            _verify_aggregate_membership(
                targets, "IntrinsicCpuTests", frozenset({"Gpu"})
            )

    def test_duplicate_source_ownership_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "RegisteredTestSources.tsv"
            path.write_text(
                "source\tobject_library\ttarget\n"
                "tests/Test.Shared.cpp\tFirstObjs\tFirstTarget\n"
                "tests/Test.Shared.cpp\tSecondObjs\tSecondTarget\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                ReconciliationError, "duplicate source ownership"
            ):
                _read_source_registry(path)

    def test_geometry_io_requires_dedicated_individual_owner(self) -> None:
        _validate_grouped_source_contract(
            {GEOMETRY_IO_SOURCE: GEOMETRY_IO_OWNER}
        )
        with self.assertRaisesRegex(ReconciliationError, "GeometryIoTestObjs"):
            _validate_grouped_source_contract(
                {
                    GEOMETRY_IO_SOURCE: SourceOwner(
                        GEOMETRY_IO_SOURCE,
                        "GeometryTestObjs",
                        "IntrinsicGeometryTests",
                    )
                }
            )

    def test_listing_expands_comments_and_preserves_disabled_names(self) -> None:
        listing = """\
Running main() from gtest_main.cc
TypedSuite/0.  # TypeParam = int
  Parameterized/0  # GetParam() = 7
DISABLED_WholeSuite.
  StillRegistered
OrdinarySuite.
  DISABLED_OneCase
"""
        self.assertEqual(
            _parse_gtest_listing(listing, "FixtureTarget"),
            (
                "TypedSuite/0.Parameterized/0",
                "DISABLED_WholeSuite.StillRegistered",
                "OrdinarySuite.DISABLED_OneCase",
            ),
        )

    def test_duplicate_expanded_name_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            ReconciliationError, "duplicate expanded GoogleTest name"
        ):
            _verify_case_inventory(
                {
                    "AlphaTarget": ("SharedSuite.SharedCase",),
                    "BetaTarget": ("SharedSuite.SharedCase",),
                },
                {
                    "AlphaTarget": ("SharedSuite.SharedCase",),
                    "BetaTarget": ("SharedSuite.SharedCase",),
                },
            )

    def test_missing_ctest_case_is_rejected(self) -> None:
        with self.assertRaisesRegex(ReconciliationError, "missing=.*Second"):
            _verify_case_inventory(
                {"Target": ("Suite.First", "Suite.Second")},
                {"Target": ("Suite.First",)},
            )

    def test_grouped_ctest_configuration_is_narrow_and_default_off(self) -> None:
        root_cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        test_cmake = (REPO_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        option = re.search(
            r"option\(INTRINSIC_GROUP_PURE_CTEST\b(?P<body>.*?)\)",
            root_cmake,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(option)
        assert option is not None
        self.assertRegex(option.group("body"), r"\bOFF\s*$")

        grouped_targets = frozenset(
            match.group("target")
            for match in re.finditer(
                r"intrinsic_grouped_test\(\s*"
                r"(?P<name>Intrinsic\w+\.Grouped)\s+"
                r"(?P<target>Intrinsic\w+)\s+"
                r'FILTER "\*"',
                test_cmake,
            )
        )
        self.assertEqual(grouped_targets, GROUPED_PURE_CTEST_TARGETS)
        self.assertEqual(
            test_cmake.count("${_intrinsic_pure_ctest_registration}"),
            len(GROUPED_PURE_CTEST_TARGETS),
        )

    def test_duplicate_grouped_registration_is_rejected(self) -> None:
        build_dir = Path("/tmp/synthetic-build")
        target = "Target"
        name = f"{target}.Grouped"
        record = {
            "name": name,
            "command": [
                str(build_dir / "bin" / target),
                "--gtest_filter=*",
                (
                    f"--gtest_output=xml:{build_dir}/Testing/Temporary/"
                    f"{name}.xml"
                ),
            ],
            "properties": [
                {
                    "name": "ENVIRONMENT",
                    "value": sorted(REQUIRED_GTEST_ENVIRONMENT),
                },
                {"name": "LABELS", "value": ["gpu", "graphics", "vulkan"]},
                {"name": "TIMEOUT", "value": 30.0},
                {
                    "name": "WORKING_DIRECTORY",
                    "value": str(build_dir / "tests"),
                },
            ],
        }
        with self.assertRaisesRegex(
            ReconciliationError, "duplicate grouped CTest registrations"
        ):
            _registered_gtest_cases_by_target(
                {"tests": [record, record]},
                build_dir,
                frozenset({target}),
                {target: frozenset({"gpu", "graphics", "vulkan"})},
                "IntrinsicCpuTests",
                frozenset(),
                frozenset({target}),
            )

    def test_affected_case_deletion_is_rejected(self) -> None:
        expected = frozenset(
            {
                ("Target", "Suite.First"),
                ("Target", "Suite.Second"),
            }
        )
        with self.assertRaisesRegex(ReconciliationError, "missing=.*Suite.Second"):
            _verify_affected_case_baseline(
                expected,
                frozenset({("Target", "Suite.First")}),
            )

    def test_affected_case_rename_is_rejected(self) -> None:
        expected = frozenset({("Target", "Suite.Original")})
        with self.assertRaisesRegex(
            ReconciliationError,
            "missing=.*Suite.Original.*extra=.*Suite.Renamed",
        ):
            _verify_affected_case_baseline(
                expected,
                frozenset({("Target", "Suite.Renamed")}),
            )

    def test_affected_case_addition_is_rejected(self) -> None:
        expected = frozenset({("Target", "Suite.First")})
        with self.assertRaisesRegex(ReconciliationError, "extra=.*Suite.Added"):
            _verify_affected_case_baseline(
                expected,
                frozenset(
                    {
                        ("Target", "Suite.First"),
                        ("Target", "Suite.Added"),
                    }
                ),
            )

    def test_ctest_extraction_uses_raw_filter_not_display_name(
        self,
    ) -> None:
        build_dir = Path("/tmp/synthetic-build")
        binary = build_dir / "bin" / "Target"
        document = {
            "tests": [
                {
                    "name": "OutsideTarget_NOT_BUILT",
                    "command": [],
                    "properties": [
                        {"name": "LABELS", "value": ["gpu", "graphics", "vulkan"]}
                    ],
                },
                {
                    "name": "Manual.Group",
                    "command": [str(binary), "--gtest_filter=Suite.*"],
                },
                {
                    "name": "PrettyDisplay.DISABLED_Case",
                    "command": [
                        str(binary),
                        "--gtest_filter=Suite.DISABLED_Case",
                        "--gtest_also_run_disabled_tests",
                    ],
                    "properties": [
                        {"name": "LABELS", "value": ["unit", "graphics"]},
                        {
                            "name": "ENVIRONMENT",
                            "value": sorted(REQUIRED_GTEST_ENVIRONMENT),
                        },
                    ],
                },
            ]
        }

        self.assertEqual(
            _registered_gtest_cases_by_target(
                document,
                build_dir,
                frozenset({"Target", "OutsideTarget"}),
                {
                    "Target": frozenset({"unit", "graphics"}),
                    "OutsideTarget": frozenset({"gpu", "graphics", "vulkan"}),
                },
                "IntrinsicCpuTests",
                frozenset({"Target"}),
            ),
            {"Target": ("Suite.DISABLED_Case",)},
        )

    def test_outside_unlabeled_gpu_placeholder_fails_cpu_routing(self) -> None:
        document = {
            "tests": [
                {
                    "name": "GpuTarget_NOT_BUILT",
                    "command": [],
                    "properties": [],
                }
            ]
        }
        with self.assertRaisesRegex(ReconciliationError, "routes into"):
            _registered_gtest_cases_by_target(
                document,
                Path("/tmp/synthetic-build"),
                frozenset({"GpuTarget"}),
                {"GpuTarget": frozenset({"gpu", "graphics", "vulkan"})},
                "IntrinsicCpuTests",
                frozenset(),
            )

    def test_malformed_ctest_labels_fail_closed(self) -> None:
        cases = {
            "unknown": ["unit", "undocumented"],
            "non-string": ["unit", 7],
            "duplicate": ["unit", "unit"],
        }
        for expected, labels in cases.items():
            with (
                self.subTest(expected=expected),
                self.assertRaisesRegex(ReconciliationError, expected),
            ):
                _ctest_labels(
                    {
                        "name": "Suite.Case",
                        "properties": [{"name": "LABELS", "value": labels}],
                    }
                )

    def test_hosted_compound_ctest_label_fails_closed(self) -> None:
        with self.assertRaisesRegex(ReconciliationError, "unknown labels"):
            _ctest_labels(
                {
                    "name": "Suite.Case",
                    "properties": [
                        {
                            "name": "LABELS",
                            "value": [
                                "gpu",
                                "graphics",
                                "vulkan",
                                r"gpu\\;vulkan\\;graphics",
                            ],
                        }
                    ],
                }
            )

    def test_gtest_discovery_has_one_canonical_property_writer(self) -> None:
        cmake_source = (REPO_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        discovery = re.search(
            r"gtest_discover_tests\(\$\{name\}(?P<body>.*?)"
            r"DISCOVERY_MODE PRE_TEST\s*\)",
            cmake_source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(discovery)
        assert discovery is not None
        self.assertNotRegex(
            discovery.group("body"),
            r"(?m)^\s*(?:ENVIRONMENT|LABELS)(?:\s|$)",
        )
        self.assertIn(
            "_intrinsic_label_target}_TESTS} PROPERTIES",
            cmake_source,
        )
        self.assertIn('ENVIRONMENT \\"${_intrinsic_test_env}\\"', cmake_source)
        self.assertIn('LABELS \\"${_intrinsic_label_value}\\"', cmake_source)

    def test_discovered_gtest_environment_fails_closed(self) -> None:
        cases = {
            "missing required": [
                "LSAN_OPTIONS=fast_unwind_on_malloc=0:detect_leaks=0"
            ],
            "non-string": [*sorted(REQUIRED_GTEST_ENVIRONMENT), 7],
            "malformed": [*sorted(REQUIRED_GTEST_ENVIRONMENT), "NO_VALUE"],
            "duplicate environment keys": [
                *sorted(REQUIRED_GTEST_ENVIRONMENT),
                "ASAN_OPTIONS=duplicate",
            ],
        }
        for expected, environment in cases.items():
            with (
                self.subTest(expected=expected),
                self.assertRaisesRegex(ReconciliationError, expected),
            ):
                _verify_ctest_environment(
                    {
                        "name": "Suite.Case",
                        "properties": [
                            {"name": "ENVIRONMENT", "value": environment}
                        ],
                    }
                )

    def test_registered_target_without_sources_must_be_explicitly_manual(
        self,
    ) -> None:
        with self.assertRaisesRegex(ReconciliationError, "not classified as manual"):
            _classify_registered_targets(
                {"LostGoogleTestTarget": frozenset({"unit", "core"})},
                {},
            )
        self.assertEqual(
            _classify_registered_targets(
                {"IntrinsicBenchmarkSmoke": frozenset({"benchmark", "geometry"})},
                {},
            ),
            frozenset(),
        )

    def test_affected_contract_accepts_intended_routing(self) -> None:
        targets = {
            "IntrinsicRuntimeContractTests": frozenset({"contract", "runtime"}),
            "IntrinsicRuntimeIntegrationTests": frozenset({"integration", "runtime"}),
            "IntrinsicGraphicsIntegrationCpuTests": frozenset(
                {"integration", "graphics"}
            ),
            "IntrinsicRuntimeGraphicsCpuTests": frozenset(
                {"integration", "runtime", "graphics"}
            ),
            "IntrinsicGraphicsUnitTests": frozenset({"unit", "graphics"}),
            "IntrinsicRuntimeGpuReadbackSmokeTests": frozenset(
                {"gpu", "vulkan", "integration", "runtime", "graphics", "slow"}
            ),
        }
        sources: dict[str, SourceOwner] = {}
        for basename in CPU_RECLASSIFIED_SOURCES:
            object_library, target = CPU_SOURCE_OWNERS[basename]
            source = f"tests/fixture/{basename}"
            sources[source] = SourceOwner(source, object_library, target)
        for basename in RHI_MANAGER_SOURCES:
            source = f"tests/unit/graphics/{basename}"
            sources[source] = SourceOwner(
                source, "GraphicsUnitTestObjs", "IntrinsicGraphicsUnitTests"
            )
        readback_source = f"tests/integration/runtime/{READBACK_SOURCE}"
        sources[readback_source] = SourceOwner(
            readback_source,
            "RuntimeGpuReadbackSmokeTestObjs",
            "IntrinsicRuntimeGpuReadbackSmokeTests",
        )

        _validate_affected_contract(targets, sources)

        del targets[READBACK_TARGET]
        del sources[readback_source]
        _validate_affected_contract(targets, sources)


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Reconcile canonical test labels, build aggregates, source ownership, "
            "GoogleTest discovery, and CTest registrations."
        )
    )
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--aggregate", choices=sorted(AGGREGATE_PREDICATES))
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run hermetic synthetic regression tests",
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        if arguments.build_dir is not None or arguments.aggregate is not None:
            parser.error("--self-test cannot be combined with live-check arguments")
    elif arguments.build_dir is None or arguments.aggregate is None:
        parser.error("live checking requires --build-dir and --aggregate")
    return arguments


def _run_self_tests() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestGateRoutingSelfTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.self_test:
        return _run_self_tests()

    try:
        target_count, case_count, source_count = reconcile(
            arguments.build_dir, arguments.aggregate
        )
    except (OSError, subprocess.SubprocessError, ReconciliationError) as error:
        print(f"test gate routing: error: {error}", file=sys.stderr)
        return 1
    print(
        "test gate routing: ok: "
        f"aggregate={arguments.aggregate} "
        f"targets={target_count} cases={case_count} sources={source_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
