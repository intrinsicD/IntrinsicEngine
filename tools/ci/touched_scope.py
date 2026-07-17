#!/usr/bin/env python3
"""Plan and execute conservative verification for an exact changed-file scope."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Sequence


ROUTE_SCHEMA = "intrinsic.touched-scope-route/v2"
DEFAULT_EXCLUDE_LABELS = ("gpu", "vulkan", "slow", "flaky-quarantine")
DEFAULT_EXCLUDE_REGEX = "|".join(DEFAULT_EXCLUDE_LABELS)
TARGET_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.+-]*\Z")
LABEL_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.+-]*\Z")
FOCUSED_SMOKE_ENABLED = False
SMOKE_BUDGET = {
    "declared_on": "2026-07-17",
    "minimum_hosted_samples": 5,
    "maximum_incremental_command_percent": 5.0,
    "maximum_incremental_build_test_p95_seconds": 60.0,
}

KERNEL_CONVERGENCE_TRACKED_PATHS = {
    "src/runtime/Runtime.Engine.cppm",
    "tools/repo/check_kernel_convergence.py",
    "tools/repo/kernel_convergence_policy.json",
    "tests/regression/tooling/Test.CheckKernelConvergence.py",
}

CI_TOOL_REGRESSION_SCRIPTS: dict[str, tuple[str, ...]] = {
    "aggregate_gate_timing.py": (
        "Test.CiTiming.py",
        "Test_BenchmarkResultValidator.py",
    ),
    "ccache_ci.py": ("Test.CcacheWorkflow.py",),
    "ccache_module_invalidation_probe.py": (
        "Test.CcacheModuleInvalidationProbe.py",
        "Test.CcacheWorkflow.py",
    ),
    "check_prerequisites.py": ("Test_CiPrerequisiteGuards.py",),
    "check_workflow_names.py": ("Test.WorkflowConcurrency.py",),
    "compare_source_coverage.py": ("Test.SourceCoverage.py",),
    "cpu_test_selection.py": (
        "Test.CpuTestSelection.py",
        "Test.SanitizerPresets.py",
    ),
    "run_source_coverage.py": ("Test.SourceCoverage.py",),
    "source_coverage.py": ("Test.SourceCoverage.py",),
    "time_command.py": ("Test.CiTiming.py",),
    "touched_scope.py": ("Test.TouchedScope.py",),
    "validate_gate_timing_baseline.py": ("Test.CiTiming.py",),
}
DEFAULT_CI_TOOL_REGRESSION_SCRIPTS = (
    "Test.TouchedScope.py",
    "Test.WorkflowConcurrency.py",
    "Test.CiTiming.py",
    "Test.CcacheWorkflow.py",
    "Test_CiPrerequisiteGuards.py",
)

LAYER_SCOPES: tuple[tuple[str, tuple[str, ...], tuple[str, ...]], ...] = (
    (
        "src/geometry/",
        ("geometry",),
        ("IntrinsicGeometryTests", "IntrinsicGeometryIoTests"),
    ),
    ("src/assets/", ("assets",), ("IntrinsicAssetUnitTests",)),
    (
        "src/ecs/",
        ("ecs",),
        ("IntrinsicECSTests", "IntrinsicEcsContractTests"),
    ),
    (
        "src/physics/",
        ("physics",),
        ("IntrinsicPhysicsMethodTests", "IntrinsicPhysicsWorldTests"),
    ),
    ("src/platform/", ("platform",), ("IntrinsicPlatformTests",)),
    (
        "src/runtime/",
        ("runtime",),
        ("IntrinsicRuntimeContractTests", "IntrinsicRuntimeUnitTests"),
    ),
    (
        "src/graphics/rhi/",
        ("graphics",),
        ("IntrinsicGraphicsRhiCpuUnitTests", "IntrinsicGraphicsContractTests"),
    ),
    (
        "src/graphics/assets/",
        ("graphics",),
        ("IntrinsicGraphicsAssetsUnitTests", "IntrinsicGraphicsContractTests"),
    ),
    (
        "src/graphics/renderer/",
        ("graphics",),
        (
            "IntrinsicGraphicsRendererCpuUnitTests",
            "IntrinsicGraphicsContractTests",
            "IntrinsicGraphicsContractCpuTests",
        ),
    ),
    (
        "src/graphics/framegraph/",
        ("graphics",),
        (
            "IntrinsicGraphicsRendererCpuUnitTests",
            "IntrinsicGraphicsContractCpuTests",
        ),
    ),
)

TEST_SCOPES: tuple[tuple[str, tuple[str, ...], tuple[str, ...]], ...] = (
    (
        "tests/unit/geometry/Test.GeometryIO.cpp",
        ("geometry",),
        ("IntrinsicGeometryIoTests",),
    ),
    ("tests/unit/geometry/", ("geometry",), ("IntrinsicGeometryTests",)),
    (
        "tests/integration/geometry/",
        ("geometry",),
        ("IntrinsicGeometryTests",),
    ),
    (
        "tests/regression/numerical/",
        ("geometry",),
        ("IntrinsicGeometryTests",),
    ),
    (
        "tests/unit/core/",
        ("core",),
        ("IntrinsicCoreTests", "IntrinsicCoreWrapperUnitTests"),
    ),
    ("tests/unit/assets/", ("assets",), ("IntrinsicAssetUnitTests",)),
    ("tests/unit/ecs/", ("ecs",), ("IntrinsicECSTests",)),
    (
        "tests/contract/ecs/",
        ("ecs",),
        ("IntrinsicEcsContractTests",),
    ),
    (
        "tests/unit/physics/",
        ("physics",),
        ("IntrinsicPhysicsMethodTests", "IntrinsicPhysicsWorldTests"),
    ),
    (
        "tests/contract/physics/",
        ("physics",),
        ("IntrinsicPhysicsWorldTests",),
    ),
    (
        "tests/unit/platform/",
        ("platform",),
        ("IntrinsicPlatformTests",),
    ),
    (
        "tests/contract/platform/",
        ("platform",),
        ("IntrinsicPlatformTests",),
    ),
    (
        "tests/unit/graphics/",
        ("graphics",),
        (
            "IntrinsicGraphicsRendererCpuUnitTests",
            "IntrinsicGraphicsRhiCpuUnitTests",
        ),
    ),
    (
        "tests/contract/graphics/",
        ("graphics",),
        ("IntrinsicGraphicsContractTests", "IntrinsicGraphicsContractCpuTests"),
    ),
    (
        "tests/contract/runtime/",
        ("runtime",),
        ("IntrinsicRuntimeContractTests", "IntrinsicRuntimeUnitTests"),
    ),
    (
        "tests/integration/runtime/",
        ("runtime",),
        ("IntrinsicRuntimeGraphicsCpuTests",),
    ),
)

BROAD_SOURCE_SUFFIXES = {
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ixx",
    ".cppm",
}


class RouteError(RuntimeError):
    """A route cannot be trusted or executed."""


class DiffError(RouteError):
    """The base/head change set could not be computed exactly."""


@dataclass(frozen=True)
class ChangeRecord:
    status: str
    path: str
    old_path: str | None = None


@dataclass(frozen=True)
class Command:
    argv: tuple[str, ...]
    reason: str

    def shell_text(self) -> str:
        return shlex.join(self.argv)


def _sha256_text(lines: Iterable[str]) -> str:
    digest = hashlib.sha256()
    for line in lines:
        digest.update(line.encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def normalize_changed_path(path: str) -> str:
    """Return a safe repository-relative POSIX path."""

    normalized = path
    while normalized.startswith("./"):
        normalized = normalized[2:]
    candidate = PurePosixPath(normalized)
    if (
        not normalized
        or candidate.is_absolute()
        or any(part in {"", ".", ".."} for part in candidate.parts)
        or candidate.as_posix() != normalized
    ):
        raise DiffError(f"invalid repository-relative changed path: {path!r}")
    return normalized


def parse_name_status_z(payload: bytes) -> list[ChangeRecord]:
    """Parse `git diff --name-status -z` without line or shell ambiguity."""

    if not payload:
        return []
    if not payload.endswith(b"\0"):
        raise DiffError("git name-status output is not NUL terminated")
    raw_tokens = payload.split(b"\0")
    raw_tokens.pop()
    try:
        tokens = [token.decode("utf-8", errors="strict") for token in raw_tokens]
    except UnicodeDecodeError as exc:
        raise DiffError(f"git name-status output is not valid UTF-8: {exc}") from exc

    records: list[ChangeRecord] = []
    index = 0
    while index < len(tokens):
        status = tokens[index]
        index += 1
        if re.fullmatch(r"[A-Z][0-9]*", status) is None:
            raise DiffError(f"malformed git change status: {status!r}")
        kind = status[0]
        path_count = 2 if kind in {"R", "C"} else 1
        if index + path_count > len(tokens):
            raise DiffError(f"git status {status!r} is missing path fields")
        if kind in {"R", "C"}:
            old_path = normalize_changed_path(tokens[index])
            path = normalize_changed_path(tokens[index + 1])
            index += 2
            records.append(ChangeRecord(status=status, path=path, old_path=old_path))
        else:
            path = normalize_changed_path(tokens[index])
            index += 1
            records.append(ChangeRecord(status=status, path=path))
    return records


def collect_change_records(
    root: Path,
    base_ref: str,
    head_ref: str,
) -> tuple[str, list[ChangeRecord]]:
    merge_base_command = [
        "git",
        "merge-base",
        "--all",
        base_ref,
        head_ref,
    ]
    try:
        merge_base_result = subprocess.run(
            merge_base_command,
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        raise DiffError(f"could not execute git merge-base: {exc}") from exc
    if merge_base_result.returncode != 0:
        stderr = merge_base_result.stderr.strip()
        raise DiffError(
            f"git merge-base failed for {base_ref!r} and {head_ref!r}: "
            f"{stderr or f'exit {merge_base_result.returncode}'}"
        )
    merge_bases = [
        line.strip()
        for line in merge_base_result.stdout.splitlines()
        if line.strip()
    ]
    if (
        len(merge_bases) != 1
        or re.fullmatch(r"[0-9a-fA-F]{40}|[0-9a-fA-F]{64}", merge_bases[0])
        is None
    ):
        raise DiffError(
            "git merge-base did not resolve exactly one commit for "
            f"{base_ref!r} and {head_ref!r}"
        )
    merge_base = merge_bases[0]

    command = [
        "git",
        "diff",
        "--name-status",
        "-z",
        "--find-renames",
        merge_base,
        head_ref,
        "--",
    ]
    try:
        result = subprocess.run(
            command,
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as exc:
        raise DiffError(f"could not execute git diff: {exc}") from exc
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise DiffError(
            f"git diff failed for {base_ref!r}..{head_ref!r}: "
            f"{stderr or f'exit {result.returncode}'}"
        )
    return merge_base, parse_name_status_z(result.stdout)


def _reason(
    reasons: list[dict[str, str]],
    code: str,
    message: str,
    path: str = "",
) -> None:
    record = {"code": code, "message": message}
    if path:
        record["path"] = path
    reasons.append(record)


def _new_route(
    *,
    base_ref: str,
    head_ref: str,
    merge_base: str | None,
    diff_status: str,
    records: Sequence[ChangeRecord],
) -> dict[str, Any]:
    return {
        "schema": ROUTE_SCHEMA,
        "stage": "planned",
        "route": "structural",
        "needs_cpp": False,
        "diff": {
            "base_ref": base_ref,
            "head_ref": head_ref,
            "merge_base": merge_base,
            "status": diff_status,
        },
        "change_records": [asdict(record) for record in records],
        "changed_files": sorted({record.path for record in records}),
        "owner_labels": [],
        "anchor_targets": [],
        "structural_checks": [],
        "reasons": [],
        "smoke": {
            "focused_enabled": FOCUSED_SMOKE_ENABLED,
            "policy": (
                "focused-and-broad"
                if FOCUSED_SMOKE_ENABLED
                else "broad-only-pending-budget"
            ),
            "budget": dict(SMOKE_BUDGET),
        },
        "actions": [],
    }


def _is_build_or_dependency_input(path: str) -> bool:
    name = PurePosixPath(path).name
    return (
        path in {"CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "vcpkg-configuration.json"}
        or path.startswith(("cmake/", "tools/vcpkg/", "external/vcpkg/", "third_party/"))
        or name == "CMakeLists.txt"
        or name.endswith(".cmake")
        or path.startswith(".github/actions/")
    )


def _add_known_scope(
    path: str,
    prefix: str,
    owners: Sequence[str],
    anchors: Sequence[str],
    owner_labels: set[str],
    anchor_targets: set[str],
    structural: set[str],
    reasons: list[dict[str, str]],
) -> None:
    owner_labels.update(owners)
    anchor_targets.update(anchors)
    structural.add("layering")
    _reason(
        reasons,
        "known-owner",
        f"matched {prefix}; selected owner label(s) {', '.join(owners)}",
        path,
    )


def analyze_change_records(
    records: Sequence[ChangeRecord],
    *,
    base_ref: str = "origin/main",
    head_ref: str = "HEAD",
    merge_base: str | None = None,
    diff_status: str = "ok",
) -> dict[str, Any]:
    route = _new_route(
        base_ref=base_ref,
        head_ref=head_ref,
        merge_base=merge_base,
        diff_status=diff_status,
        records=records,
    )
    reasons = route["reasons"]
    owner_labels: set[str] = set()
    anchor_targets: set[str] = set()
    structural: set[str] = set()
    broad = False

    if not records:
        route["route"] = "broad"
        route["needs_cpp"] = True
        _reason(
            reasons,
            "zero-change-set",
            "exact change computation selected zero files; using broad feedback",
        )
        return route

    for record in records:
        path = record.path
        kind = record.status[0]
        matched = False

        if kind not in {"A", "M"}:
            broad = True
            _reason(
                reasons,
                "ambiguous-change-status",
                f"status {record.status} is not safe for narrow routing",
                path,
            )

        if path in KERNEL_CONVERGENCE_TRACKED_PATHS:
            structural.add("kernel_convergence")
            _reason(
                reasons,
                "kernel-convergence",
                "selected the live kernel-convergence checker and its regression",
                path,
            )
            matched = True

        if _is_build_or_dependency_input(path):
            broad = True
            structural.add("test_layout")
            _reason(
                reasons,
                "build-or-dependency-input",
                "build, toolchain, preset, or dependency input requires broad feedback",
                path,
            )
            continue

        if path.startswith(".github/workflows/"):
            structural.update(
                {
                    "docs",
                    "task_policy",
                    "workflow_names",
                    "workflow_regression_tests",
                }
            )
            _reason(
                reasons,
                "workflow",
                "workflow change selected workflow, task-policy, and documentation checks",
                path,
            )
            matched = True

        if path.startswith("docs/") or path.endswith(".md"):
            structural.add("docs")
            if path.startswith("docs/agent/"):
                structural.update({"task_state", "skills_sync"})
            if path.startswith("docs/reports/"):
                structural.add("session_brief")
            _reason(
                reasons,
                "documentation",
                "documentation change selected structural documentation checks",
                path,
            )
            matched = True

        if path.startswith("tasks/"):
            structural.update({"task_policy", "task_state", "session_brief"})
            if path.startswith("tasks/templates/"):
                structural.add("skills_sync")
            _reason(
                reasons,
                "task-record",
                "task change selected task policy, state-link, and session checks",
                path,
            )
            matched = True

        if path.startswith("tools/agents/"):
            structural.update(
                {"task_policy", "task_state", "skills_sync", "session_brief"}
            )
            _reason(
                reasons,
                "agent-tooling",
                "agent tooling change selected task and skill checks",
                path,
            )
            matched = True
        elif path.startswith("tools/docs/"):
            structural.add("docs")
            _reason(
                reasons,
                "docs-tooling",
                "documentation tooling change selected link checks",
                path,
            )
            matched = True
        elif path == "tools/repo/check_layering.py":
            structural.update(
                {"layering", "layering_regression_tests", "test_layout"}
            )
            _reason(
                reasons,
                "layering-tooling",
                "layering checker change selected live and synthetic checks",
                path,
            )
            matched = True
        elif path in KERNEL_CONVERGENCE_TRACKED_PATHS:
            pass
        elif path.startswith("tools/repo/"):
            structural.update({"layering", "test_layout"})
            _reason(
                reasons,
                "repository-tooling",
                "repository tooling change selected structural checks",
                path,
            )
            matched = True
        elif path == "tools/analysis/compile_hotspots.py":
            structural.add("tooling_test:Test.CompileHotspots.py")
            _reason(
                reasons,
                "analysis-tooling",
                "compile-hotspot analyzer change selected its synthetic regression",
                path,
            )
            matched = True
        elif path.startswith("tools/ci/"):
            if not path.endswith(".md"):
                scripts = CI_TOOL_REGRESSION_SCRIPTS.get(
                    PurePosixPath(path).name,
                    DEFAULT_CI_TOOL_REGRESSION_SCRIPTS,
                )
                structural.update(
                    f"tooling_test:{script}" for script in scripts
                )
                if PurePosixPath(path).name == "check_workflow_names.py":
                    structural.add("workflow_names")
            _reason(
                reasons,
                "ci-tooling",
                "CI tooling change selected its owning policy regressions",
                path,
            )
            matched = True

        suffix = PurePosixPath(path).suffix
        if (
            suffix in BROAD_SOURCE_SUFFIXES
            and path.startswith(("src/", "tests/", "methods/", "benchmarks/"))
        ):
            broad = True
            structural.add("layering" if path.startswith("src/") else "test_layout")
            _reason(
                reasons,
                "dependency-graph-source",
                "module interface or header dependency impact is not narrowed",
                path,
            )
            continue

        if path.startswith("src/core/"):
            broad = True
            structural.add("layering")
            _reason(
                reasons,
                "foundational-core",
                "core is foundational and requires broad feedback",
                path,
            )
            continue

        if path.startswith("src/graphics/vulkan/"):
            broad = True
            structural.add("layering")
            _reason(
                reasons,
                "backend-source",
                "Vulkan source is outside the Null/headless narrow graph",
                path,
            )
            continue

        for prefix, owners, anchors in LAYER_SCOPES:
            if path.startswith(prefix):
                _add_known_scope(
                    path,
                    prefix,
                    owners,
                    anchors,
                    owner_labels,
                    anchor_targets,
                    structural,
                    reasons,
                )
                matched = True
                break

        if path.startswith("src/") and not matched:
            broad = True
            structural.add("layering")
            _reason(
                reasons,
                "unknown-source",
                "source path has no conservative narrow ownership mapping",
                path,
            )
            continue

        if path == "tests/regression/tooling/Test.CheckKernelConvergence.py":
            matched = True
        elif path.startswith("tests/regression/tooling/") and path.endswith(".py"):
            if path == "tests/regression/tooling/Test.CheckLayering.py":
                structural.add("layering_regression_tests")
            elif path == "tests/regression/tooling/Test.WorkflowConcurrency.py":
                structural.add("workflow_regression_tests")
            else:
                structural.add(
                    f"tooling_test:{PurePosixPath(path).name}"
                )
            _reason(
                reasons,
                "tooling-regression",
                "tooling regression change selected the matching structural suite",
                path,
            )
            matched = True
        elif path == (
            "tests/regression/tooling/Test.TestGateRouting.baseline.tsv"
        ):
            structural.add("tooling_test:Test.TestGateRouting.py")
            _reason(
                reasons,
                "tooling-regression",
                "routing baseline change selected its synthetic reconciliation suite",
                path,
            )
            matched = True
        elif path.startswith("tests/"):
            structural.add("test_layout")
            for prefix, owners, anchors in TEST_SCOPES:
                if path.startswith(prefix):
                    owner_labels.update(owners)
                    anchor_targets.update(anchors)
                    _reason(
                        reasons,
                        "known-test-owner",
                        f"matched {prefix}; selected owner label(s) {', '.join(owners)}",
                        path,
                    )
                    matched = True
                    break
            if path.startswith("tests/contract/repo/layering_fixtures/"):
                structural.add("layering_regression_tests")
                matched = True
            if not matched:
                broad = True
                _reason(
                    reasons,
                    "unknown-test-source",
                    "test source has no conservative narrow ownership mapping",
                    path,
                )
            continue

        if path.startswith("assets/shaders/"):
            structural.add("shader_outputs")
            owner_labels.add("graphics")
            anchor_targets.add("IntrinsicGraphicsContractTests")
            _reason(
                reasons,
                "shader",
                "shader change selected graphics owners and output checks",
                path,
            )
            matched = True

        if not matched:
            broad = True
            _reason(
                reasons,
                "unknown-path",
                "path has no conservative structural or source mapping",
                path,
            )

    route["owner_labels"] = sorted(owner_labels)
    route["anchor_targets"] = sorted(anchor_targets)
    route["structural_checks"] = sorted(structural)
    if broad:
        route["route"] = "broad"
        route["needs_cpp"] = True
    elif owner_labels:
        route["route"] = "focused"
        route["needs_cpp"] = True
    else:
        route["route"] = "structural"
        route["needs_cpp"] = False
    return route


def broad_fallback_plan(
    message: str,
    *,
    base_ref: str,
    head_ref: str,
) -> dict[str, Any]:
    route = _new_route(
        base_ref=base_ref,
        head_ref=head_ref,
        merge_base=None,
        diff_status="error",
        records=[],
    )
    route["route"] = "broad"
    route["needs_cpp"] = True
    _reason(route["reasons"], "diff-error", message)
    return route


def _validate_route(route: Any) -> dict[str, Any]:
    if not isinstance(route, dict) or route.get("schema") != ROUTE_SCHEMA:
        raise RouteError(f"route JSON must use schema {ROUTE_SCHEMA}")
    if route.get("route") not in {"structural", "focused", "broad"}:
        raise RouteError("route JSON has an invalid route")
    if not isinstance(route.get("needs_cpp"), bool):
        raise RouteError("route JSON needs_cpp must be boolean")
    for field in ("reasons", "structural_checks", "owner_labels", "anchor_targets"):
        if not isinstance(route.get(field), list):
            raise RouteError(f"route JSON field {field} must be an array")
    return route


def read_route(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise RouteError(f"route JSON does not exist: {path}") from exc
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RouteError(f"could not read route JSON {path}: {exc}") from exc
    return _validate_route(payload)


def write_route(path: Path, route: dict[str, Any]) -> None:
    _validate_route(route)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(route, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def _write_github_outputs(path: str | None, values: dict[str, object]) -> None:
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as output:
        for key, value in values.items():
            if isinstance(value, bool):
                encoded = "true" if value else "false"
            else:
                encoded = str(value)
            if "\n" in encoded or "\r" in encoded:
                raise RouteError(f"GitHub output {key} contains a newline")
            output.write(f"{key}={encoded}\n")


def _append_summary(path: str | None, title: str, lines: Sequence[str]) -> None:
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as summary:
        summary.write(f"### {title}\n\n")
        for line in lines:
            summary.write(f"- {line}\n")
        summary.write("\n")


def _bounded_summary_json(values: Sequence[Any], *, limit: int = 50) -> str:
    preview = list(values[:limit])
    rendered = json.dumps(preview, ensure_ascii=True, separators=(",", ":"))
    omitted = len(values) - len(preview)
    if omitted:
        rendered += f" ({omitted} more in route artifact)"
    return rendered


def _record_action(
    route: dict[str, Any],
    *,
    name: str,
    status: str,
    elapsed_seconds: float,
    details: dict[str, Any] | None = None,
) -> None:
    entry: dict[str, Any] = {
        "name": name,
        "status": status,
        "elapsed_seconds": round(elapsed_seconds, 6),
    }
    if details:
        entry["details"] = details
    route.setdefault("actions", []).append(entry)


def structural_commands(root_arg: str, checks: Sequence[str]) -> list[Command]:
    selected = set(checks)
    commands: list[Command] = []
    for check in sorted(selected):
        prefix = "tooling_test:"
        if not check.startswith(prefix):
            continue
        script = check.removeprefix(prefix)
        if (
            PurePosixPath(script).name != script
            or re.fullmatch(r"Test[._][A-Za-z0-9_.]+\.py", script) is None
        ):
            raise RouteError(f"invalid tooling regression selection: {check!r}")
        argv = ("python3", f"tests/regression/tooling/{script}")
        if script == "Test.TestGateRouting.py":
            argv = (*argv, "--self-test")
        commands.append(Command(argv, f"{script} regression"))
    if "touched_scope_tests" in selected:
        commands.append(
            Command(
                ("python3", "tests/regression/tooling/Test.TouchedScope.py"),
                "touched-scope regressions",
            )
        )
    if "layering_regression_tests" in selected:
        commands.append(
            Command(
                ("python3", "tests/regression/tooling/Test.CheckLayering.py"),
                "layering checker regressions",
            )
        )
    if "kernel_convergence" in selected:
        commands.extend(
            (
                Command(
                    (
                        "python3",
                        "tests/regression/tooling/Test.CheckKernelConvergence.py",
                    ),
                    "kernel-convergence regressions",
                ),
                Command(
                    (
                        "python3",
                        "tools/repo/check_kernel_convergence.py",
                        "--root",
                        root_arg,
                        "--strict",
                    ),
                    "kernel-convergence contract",
                ),
            )
        )
    if "workflow_regression_tests" in selected:
        for script in (
            "Test.WorkflowConcurrency.py",
            "Test.CcacheWorkflow.py",
            "Test.CiTiming.py",
        ):
            commands.append(
                Command(
                    ("python3", f"tests/regression/tooling/{script}"),
                "workflow policy regressions",
            )
        )
    if "workflow_names" in selected:
        commands.append(
            Command(
                (
                    "python3",
                    "tools/ci/check_workflow_names.py",
                    "--root",
                    ".github/workflows",
                    "--strict",
                ),
                "workflow naming contract",
            )
        )
    if "layering" in selected:
        commands.append(
            Command(
                (
                    "python3",
                    "tools/repo/check_layering.py",
                    "--root",
                    "src",
                    "--strict",
                ),
                "layering contract",
            )
        )
    if "test_layout" in selected:
        commands.append(
            Command(
                (
                    "python3",
                    "tools/repo/check_test_layout.py",
                    "--root",
                    root_arg,
                    "--strict",
                ),
                "test layout contract",
            )
        )
    if "docs" in selected:
        commands.append(
            Command(
                ("python3", "tools/docs/check_doc_links.py", "--root", root_arg),
                "documentation links",
            )
        )
    if "task_policy" in selected:
        commands.append(
            Command(
                (
                    "python3",
                    "tools/agents/check_task_policy.py",
                    "--root",
                    root_arg,
                    "--strict",
                ),
                "task policy",
            )
        )
    if "task_state" in selected:
        commands.append(
            Command(
                (
                    "python3",
                    "tools/agents/check_task_state_links.py",
                    "--root",
                    root_arg,
                    "--strict",
                ),
                "task-state links",
            )
        )
    if "shader_outputs" in selected:
        commands.append(
            Command(
                ("python3", "tools/repo/check_shader_outputs.py", "--root", root_arg),
                "shader output policy",
            )
        )
    if "skills_sync" in selected:
        commands.append(
            Command(
                ("python3", "tools/agents/sync_skills.py", "--check"),
                "agent skill mirror sync",
            )
        )
    if "session_brief" in selected:
        commands.append(
            Command(
                ("python3", "tools/agents/generate_session_brief.py", "--check"),
                "session brief freshness",
            )
        )
    deduplicated: list[Command] = []
    seen: set[tuple[str, ...]] = set()
    for command in commands:
        if command.argv not in seen:
            seen.add(command.argv)
            deduplicated.append(command)
    return deduplicated


def _run_command(
    argv: Sequence[str],
    *,
    cwd: Path,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(argv),
        cwd=cwd,
        check=False,
        text=True,
        capture_output=capture_output,
    )


def _load_registry(build_dir: Path) -> dict[str, frozenset[str]]:
    path = build_dir / "test-inventories" / "RegisteredTestTargets.tsv"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, OSError, UnicodeError) as exc:
        raise RouteError(f"could not read configured test registry {path}: {exc}") from exc
    if not lines or lines[0] != "target\tlabels":
        raise RouteError(f"configured test registry has an invalid header: {path}")
    registry: dict[str, frozenset[str]] = {}
    for line_number, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != 2:
            raise RouteError(f"{path}:{line_number}: expected target and labels")
        target, encoded_labels = fields
        labels = encoded_labels.split(",")
        if TARGET_NAME_RE.fullmatch(target) is None:
            raise RouteError(f"{path}:{line_number}: invalid target {target!r}")
        if target in registry:
            raise RouteError(f"{path}:{line_number}: duplicate target {target!r}")
        if (
            not labels
            or any(LABEL_RE.fullmatch(label) is None for label in labels)
            or len(labels) != len(set(labels))
        ):
            raise RouteError(f"{path}:{line_number}: invalid labels for {target}")
        registry[target] = frozenset(labels)
    if not registry:
        raise RouteError(f"configured test registry is empty: {path}")
    return registry


def _load_aggregate(build_dir: Path, name: str) -> tuple[str, ...]:
    path = build_dir / "test-inventories" / f"{name}.txt"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, OSError, UnicodeError) as exc:
        raise RouteError(f"could not read aggregate inventory {path}: {exc}") from exc
    if not lines or any(not line for line in lines):
        raise RouteError(f"aggregate inventory is empty or malformed: {path}")
    if len(lines) != len(set(lines)):
        raise RouteError(f"aggregate inventory contains duplicate targets: {path}")
    if any(TARGET_NAME_RE.fullmatch(target) is None for target in lines):
        raise RouteError(f"aggregate inventory contains an invalid target: {path}")
    return tuple(lines)


def _reconcile_aggregates(
    registry: dict[str, frozenset[str]],
    pr_fast: Sequence[str],
    pr_smoke: Sequence[str],
) -> None:
    registry_targets = set(registry)
    unknown = (set(pr_fast) | set(pr_smoke)) - registry_targets
    if unknown:
        raise RouteError(
            "aggregate inventory references undeclared target(s): "
            + ", ".join(sorted(unknown))
        )
    excluded = set(DEFAULT_EXCLUDE_LABELS)
    expected_fast = {
        target
        for target, labels in registry.items()
        if labels.intersection({"unit", "contract"}) and not labels.intersection(excluded)
    }
    expected_smoke = {
        target
        for target, labels in registry.items()
        if {"integration", "runtime", "graphics"}.issubset(labels)
        and not labels.intersection(excluded)
    }
    if set(pr_fast) != expected_fast:
        raise RouteError(
            "IntrinsicPrFastTests inventory does not match configured label predicates"
        )
    if set(pr_smoke) != expected_smoke:
        raise RouteError(
            "IntrinsicPrSmokeTests inventory does not match configured label predicates"
        )
    overlap = set(pr_fast).intersection(pr_smoke)
    if overlap:
        raise RouteError(
            "PR-fast and PR-smoke producer inventories overlap: "
            + ", ".join(sorted(overlap))
        )


def finalize_route(route: dict[str, Any], build_dir: Path) -> dict[str, Any]:
    route = _validate_route(route)
    if not route["needs_cpp"]:
        raise RouteError("structural-only route cannot be finalized as C++ work")
    registry = _load_registry(build_dir)
    pr_fast = _load_aggregate(build_dir, "IntrinsicPrFastTests")
    pr_smoke = _load_aggregate(build_dir, "IntrinsicPrSmokeTests")
    _reconcile_aggregates(registry, pr_fast, pr_smoke)

    excluded = set(DEFAULT_EXCLUDE_LABELS)
    route_name = str(route["route"])
    build_batches: list[dict[str, Any]]
    selected_targets: set[str]
    if route_name == "focused":
        owners = set(str(label) for label in route["owner_labels"])
        owner_targets = {
            target
            for target, labels in registry.items()
            if labels.intersection(owners) and not labels.intersection(excluded)
        }
        selected_targets = owner_targets - set(pr_smoke)
        admitted_targets = set(selected_targets)
        if FOCUSED_SMOKE_ENABLED:
            admitted_targets.update(pr_smoke)
        missing_anchors = set(route["anchor_targets"]) - set(registry)
        invalid_anchors = set(route["anchor_targets"]) - admitted_targets
        if not admitted_targets or missing_anchors or invalid_anchors:
            route["route"] = "broad"
            route_name = "broad"
            _reason(
                route["reasons"],
                "configured-anchor-mismatch",
                "focused anchors are absent or inconsistent in the fresh registry; "
                "widened to broad feedback",
            )
        else:
            build_batches = []
            if selected_targets:
                build_batches.append(
                    {
                        "name": "focused-owner",
                        "targets": sorted(selected_targets),
                        "producer_targets": sorted(selected_targets),
                    }
                )
            if FOCUSED_SMOKE_ENABLED:
                if pr_smoke:
                    build_batches.append(
                        {
                            "name": "pr-smoke",
                            "targets": ["IntrinsicPrSmokeTests"],
                            "producer_targets": list(pr_smoke),
                        }
                    )
                selected_targets.update(pr_smoke)
    if route_name == "broad":
        selected_targets = set(pr_fast).union(pr_smoke)
        build_batches = [
            {
                "name": "pr-fast",
                "targets": ["IntrinsicPrFastTests"],
                "producer_targets": list(pr_fast),
            },
            {
                "name": "pr-smoke",
                "targets": ["IntrinsicPrSmokeTests"],
                "producer_targets": list(pr_smoke),
            },
        ]

    selected = sorted(selected_targets)
    inventory_dir = build_dir / "ci-routing"
    inventory_dir.mkdir(parents=True, exist_ok=True)
    target_inventory = inventory_dir / "selected-test-targets.txt"
    target_inventory.write_text("\n".join(selected) + "\n", encoding="utf-8")
    registry_lines = [
        f"{target}\t{','.join(sorted(labels))}"
        for target, labels in sorted(registry.items())
    ]
    route["stage"] = "finalized"
    route["finalization"] = {
        "build_dir": build_dir.as_posix(),
        "registry_target_count": len(registry),
        "registry_digest": _sha256_text(registry_lines),
        "pr_fast_target_count": len(pr_fast),
        "pr_smoke_target_count": len(pr_smoke),
        "selected_target_count": len(selected),
        "selected_target_digest": _sha256_text(selected),
        "selected_target_inventory": target_inventory.as_posix(),
        "selected_targets": selected,
        "build_batches": build_batches,
    }
    return route


def _ninja_commands(
    build_dir: Path,
    targets: Sequence[str],
    *,
    root: Path,
) -> set[str]:
    result = _run_command(
        ("ninja", "-C", str(build_dir), "-t", "commands", *targets),
        cwd=root,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RouteError(
            f"could not enumerate Ninja commands for {', '.join(targets)}: "
            f"{result.stderr.strip() or f'exit {result.returncode}'}"
        )
    commands = {line for line in result.stdout.splitlines() if line.strip()}
    if not commands:
        raise RouteError(
            f"Ninja command closure is empty for target(s): {', '.join(targets)}"
        )
    return commands


def execute_build(
    route: dict[str, Any],
    *,
    root: Path,
    build_dir: Path,
) -> tuple[int, dict[str, Any]]:
    finalization = route.get("finalization")
    if route.get("stage") != "finalized" or not isinstance(finalization, dict):
        raise RouteError("build action requires a finalized route")
    batches = finalization.get("build_batches")
    if not isinstance(batches, list) or not batches:
        raise RouteError("finalized route contains no build batches")

    prior_commands: set[str] = set()
    batch_results: list[dict[str, Any]] = []
    returncode = 0
    for batch in batches:
        if not isinstance(batch, dict) or not isinstance(batch.get("targets"), list):
            raise RouteError("finalized route has a malformed build batch")
        targets = [str(target) for target in batch["targets"]]
        commands = _ninja_commands(build_dir, targets, root=root)
        started = time.monotonic()
        result = _run_command(
            ("cmake", "--build", str(build_dir), "--target", *targets),
            cwd=root,
        )
        elapsed = time.monotonic() - started
        incremental = commands - prior_commands
        batch_results.append(
            {
                "name": str(batch.get("name", "")),
                "targets": targets,
                "command_edge_count": len(commands),
                "incremental_command_edge_count": len(incremental),
                "elapsed_seconds": round(elapsed, 6),
                "returncode": result.returncode,
            }
        )
        prior_commands.update(commands)
        if result.returncode != 0:
            returncode = result.returncode
            break
    route["build"] = {
        "ninja_edge_count": len(prior_commands),
        "batches": batch_results,
    }
    if returncode == 0:
        route["stage"] = "built"
    return returncode, route


def _require_selected_binaries(route: dict[str, Any], build_dir: Path) -> None:
    finalization = route.get("finalization")
    if not isinstance(finalization, dict):
        raise RouteError("test action requires finalized producer metadata")
    targets = finalization.get("selected_targets")
    if not isinstance(targets, list) or not targets:
        raise RouteError("selected producer inventory is empty")
    missing = [
        str(build_dir / "bin" / str(target))
        for target in targets
        if not (build_dir / "bin" / str(target)).is_file()
        or not os.access(build_dir / "bin" / str(target), os.X_OK)
    ]
    if missing:
        raise RouteError(
            "selected producer binary/binaries are missing: " + ", ".join(missing)
        )


def _ctest_names(
    build_dir: Path,
    selector: Sequence[str],
    *,
    root: Path,
    expected_targets: Sequence[str],
) -> list[str]:
    result = _run_command(
        (
            "ctest",
            "--test-dir",
            str(build_dir),
            "--show-only=json-v1",
            *selector,
        ),
        cwd=root,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RouteError(
            f"CTest inventory failed for selector {shlex.join(selector)}: "
            f"{result.stderr.strip() or f'exit {result.returncode}'}"
        )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RouteError(f"CTest inventory JSON is malformed: {exc}") from exc
    tests = payload.get("tests") if isinstance(payload, dict) else None
    if not isinstance(tests, list):
        raise RouteError("CTest inventory JSON has no tests array")
    expected_binaries = {
        (build_dir / "bin" / target).resolve(): target
        for target in expected_targets
    }
    if not expected_binaries:
        raise RouteError("CTest batch has no expected producer targets")
    names: list[str] = []
    observed_targets: set[str] = set()
    for test in tests:
        name = test.get("name") if isinstance(test, dict) else None
        if not isinstance(name, str) or not name:
            raise RouteError("CTest inventory contains an invalid test name")
        command = test.get("command")
        if (
            not isinstance(command, list)
            or not command
            or not isinstance(command[0], str)
        ):
            continue
        producer = expected_binaries.get(Path(command[0]).resolve())
        if producer is None:
            continue
        names.append(name)
        observed_targets.add(producer)
    missing_targets = set(expected_targets) - observed_targets
    if missing_targets:
        raise RouteError(
            "CTest selector did not discover selected producer target(s): "
            + ", ".join(sorted(missing_targets))
        )
    if not names:
        raise RouteError(
            f"CTest selector selected zero tests: {shlex.join(selector)}"
        )
    if len(names) != len(set(names)):
        raise RouteError("CTest selector produced duplicate test names")
    return names


def _test_batches(route: dict[str, Any]) -> list[dict[str, Any]]:
    finalization = route.get("finalization")
    if not isinstance(finalization, dict):
        raise RouteError("test route has no finalized producer metadata")
    raw_build_batches = finalization.get("build_batches")
    if not isinstance(raw_build_batches, list):
        raise RouteError("test route has malformed build-batch metadata")
    producer_batches: dict[str, list[str]] = {}
    for batch in raw_build_batches:
        if not isinstance(batch, dict):
            raise RouteError("test route has a malformed producer batch")
        name = batch.get("name")
        producers = batch.get("producer_targets")
        if (
            not isinstance(name, str)
            or not isinstance(producers, list)
            or not producers
            or any(not isinstance(target, str) for target in producers)
        ):
            raise RouteError("test route has a malformed producer inventory")
        producer_batches[name] = producers

    exclude = ["-LE", DEFAULT_EXCLUDE_REGEX]
    if route["route"] == "broad":
        return [
            {
                "name": "pr-fast",
                "selector": ["-L", "unit|contract", *exclude],
                "producer_targets": producer_batches.get("pr-fast", []),
            },
            {
                "name": "pr-smoke",
                "selector": [
                    "-L",
                    "integration",
                    "-L",
                    "runtime",
                    "-L",
                    "graphics",
                    *exclude,
                ],
                "producer_targets": producer_batches.get("pr-smoke", []),
            },
        ]
    owners = [re.escape(str(label)) for label in route["owner_labels"]]
    if not owners:
        raise RouteError("focused test route has no owner labels")
    batches = [
        {
            "name": "focused-owner",
            "selector": [
                "-L",
                "|".join(sorted(owners)),
                *exclude,
            ],
            "producer_targets": producer_batches.get("focused-owner", []),
        }
    ]
    if FOCUSED_SMOKE_ENABLED:
        batches.append(
            {
                "name": "pr-smoke",
                "selector": [
                    "-L",
                    "integration",
                    "-L",
                    "runtime",
                    "-L",
                    "graphics",
                    *exclude,
                ],
                "producer_targets": producer_batches.get("pr-smoke", []),
            }
        )
    return batches


def _exact_name_regexes(
    names: Sequence[str],
    *,
    maximum_length: int = 8_000,
) -> list[str]:
    regexes: list[str] = []
    alternatives: list[str] = []
    current_length = 3
    for name in names:
        escaped = re.escape(name)
        added_length = len(escaped.encode("utf-8")) + (
            1 if alternatives else 0
        )
        if alternatives and current_length + added_length > maximum_length:
            regexes.append("^(" + "|".join(alternatives) + ")$")
            alternatives = []
            current_length = 3
            added_length = len(escaped)
        if current_length + added_length > maximum_length:
            raise RouteError(f"CTest name is too long for exact selection: {name!r}")
        alternatives.append(escaped)
        current_length += added_length
    if alternatives:
        regexes.append("^(" + "|".join(alternatives) + ")$")
    if not regexes:
        raise RouteError("cannot build an exact CTest selector for zero names")
    return regexes


def execute_tests(
    route: dict[str, Any],
    *,
    root: Path,
    build_dir: Path,
    timeout: int,
    jobs: int,
) -> tuple[int, dict[str, Any]]:
    if route.get("stage") != "built":
        raise RouteError("test action requires a successfully built route")
    _require_selected_binaries(route, build_dir)
    selected_names: set[str] = set()
    batch_results: list[dict[str, Any]] = []
    returncode = 0
    for batch in _test_batches(route):
        selector = [str(value) for value in batch["selector"]]
        producer_targets = [
            str(value) for value in batch["producer_targets"]
        ]
        raw_names = _ctest_names(
            build_dir,
            selector,
            root=root,
            expected_targets=producer_targets,
        )
        names = [name for name in raw_names if name not in selected_names]
        if len(names) != len(raw_names):
            overlap = sorted(set(raw_names).intersection(selected_names))
            raise RouteError(
                "CTest batches selected duplicate case(s): "
                + ", ".join(overlap)
            )
        selected_names.update(names)
        started = time.monotonic()
        exact_regexes = _exact_name_regexes(names)
        batch_returncode = 0
        for exact in exact_regexes:
            result = _run_command(
                (
                    "ctest",
                    "--test-dir",
                    str(build_dir),
                    "--output-on-failure",
                    *selector,
                    "-R",
                    exact,
                    "--no-tests=error",
                    "--timeout",
                    str(timeout),
                    f"-j{jobs}",
                ),
                cwd=root,
            )
            if result.returncode != 0:
                batch_returncode = result.returncode
                break
        elapsed = time.monotonic() - started
        batch_results.append(
            {
                "name": str(batch["name"]),
                "selector": selector,
                "selected_test_count": len(names),
                "selected_producer_count": len(producer_targets),
                "exact_invocation_count": len(exact_regexes),
                "covered_by_prior_batch": len(raw_names) - len(names),
                "elapsed_seconds": round(elapsed, 6),
                "returncode": batch_returncode,
            }
        )
        if batch_returncode != 0:
            returncode = batch_returncode
            break
    if not selected_names:
        raise RouteError("all configured CTest batches selected zero unique tests")
    inventory_dir = build_dir / "ci-routing"
    inventory_dir.mkdir(parents=True, exist_ok=True)
    inventory_path = inventory_dir / "selected-ctest-tests.txt"
    ordered_names = sorted(selected_names)
    inventory_path.write_text("\n".join(ordered_names) + "\n", encoding="utf-8")
    route["test"] = {
        "selected_test_count": len(ordered_names),
        "selected_test_digest": _sha256_text(ordered_names),
        "selected_test_inventory": inventory_path.as_posix(),
        "batches": batch_results,
    }
    if returncode == 0:
        route["stage"] = "tested"
    return returncode, route


def print_plan(route: dict[str, Any], args: argparse.Namespace) -> None:
    print("Touched-scope verification plan")
    print(f"Route: {route['route']}")
    print(f"C++ setup required: {'yes' if route['needs_cpp'] else 'no'}")
    print("Changed files:")
    for path in route["changed_files"]:
        print(f"  - {path}")
    if not route["changed_files"]:
        print("  - <none; fail-closed broad fallback>")
    print("Selection notes:")
    for reason in route["reasons"]:
        path = f"{reason.get('path')}: " if reason.get("path") else ""
        print(f"  - {path}{reason['message']}")
    print("Commands:")
    for command in structural_commands(args.root, route["structural_checks"]):
        print(f"# {command.reason}")
        print(command.shell_text())
    if route["needs_cpp"]:
        print("# configure staged C++ feedback")
        print(shlex.join(("cmake", "--preset", args.preset, "--fresh")))
        print(
            "# finalize/build/test commands are derived from the configured "
            "test registry"
        )
    print(
        "Note: touched-scope feedback is not a replacement for full "
        "CPU/sanitizer/capability gates."
    )


def _plan_from_args(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    started = time.monotonic()
    if args.changed_file:
        try:
            records = [
                ChangeRecord("M", normalize_changed_path(path))
                for path in args.changed_file
            ]
            route = analyze_change_records(
                records,
                base_ref=args.base_ref,
                head_ref=args.head_ref,
                diff_status="explicit",
            )
        except DiffError as exc:
            route = broad_fallback_plan(
                str(exc),
                base_ref=args.base_ref,
                head_ref=args.head_ref,
            )
    else:
        try:
            merge_base, records = collect_change_records(
                root,
                args.base_ref,
                args.head_ref,
            )
            route = analyze_change_records(
                records,
                base_ref=args.base_ref,
                head_ref=args.head_ref,
                merge_base=merge_base,
            )
        except (DiffError, OSError, ValueError) as exc:
            route = broad_fallback_plan(
                str(exc),
                base_ref=args.base_ref,
                head_ref=args.head_ref,
            )
    route["planning"] = {
        "elapsed_seconds": round(time.monotonic() - started, 6),
    }
    return route


def _route_path(args: argparse.Namespace) -> Path:
    value = args.output or args.plan
    if value:
        return Path(value)
    return Path(args.preset_build_dir) / "ci-routing" / "route.json"


def _action_plan(args: argparse.Namespace, root: Path) -> int:
    route = _plan_from_args(args, root)
    route_path = _route_path(args)
    write_route(route_path, route)
    _write_github_outputs(
        args.github_output,
        {
            "needs_cpp": route["needs_cpp"],
            "route": route["route"],
            "smoke_policy": route["smoke"]["policy"],
        },
    )
    _append_summary(
        args.step_summary,
        "Touched-scope plan",
        (
            f"route: `{route['route']}`",
            f"broad fallback: `{str(route['route'] == 'broad').lower()}`",
            f"C++ setup required: `{str(route['needs_cpp']).lower()}`",
            f"merge base: `{route['diff'].get('merge_base') or 'unavailable'}`",
            "changed files: "
            + _bounded_summary_json(route["changed_files"]),
            "owner labels: "
            + _bounded_summary_json(route["owner_labels"]),
            "selection reasons: "
            + _bounded_summary_json(route["reasons"]),
        ),
    )
    if args.print_only or (not args.run and args.action == "plan" and not args.output):
        print_plan(route, args)
    return 0


def _action_structural(args: argparse.Namespace, root: Path) -> int:
    route_path = _route_path(args)
    route = read_route(route_path)
    commands = structural_commands(args.root, route["structural_checks"])
    started = time.monotonic()
    for command in commands:
        print(f"[touched_scope] {command.reason}: {command.shell_text()}", flush=True)
        result = _run_command(command.argv, cwd=root)
        if result.returncode != 0:
            _record_action(
                route,
                name="structural",
                status="failed",
                elapsed_seconds=time.monotonic() - started,
                details={"command_count": len(commands), "returncode": result.returncode},
            )
            write_route(route_path, route)
            return result.returncode
    _record_action(
        route,
        name="structural",
        status="passed",
        elapsed_seconds=time.monotonic() - started,
        details={"command_count": len(commands)},
    )
    write_route(route_path, route)
    _append_summary(
        args.step_summary,
        "Touched-scope structural checks",
        (f"commands passed: `{len(commands)}`",),
    )
    return 0


def _action_finalize(args: argparse.Namespace, root: Path) -> int:
    del root
    route_path = _route_path(args)
    route = read_route(route_path)
    started = time.monotonic()
    try:
        route = finalize_route(route, Path(args.build_dir))
    except RouteError as exc:
        _record_action(
            route,
            name="finalize",
            status="failed",
            elapsed_seconds=time.monotonic() - started,
            details={"error": str(exc)},
        )
        write_route(route_path, route)
        print(f"touched_scope: error: {exc}", file=sys.stderr)
        return 2
    _record_action(
        route,
        name="finalize",
        status="passed",
        elapsed_seconds=time.monotonic() - started,
        details={
            "selected_target_count": route["finalization"]["selected_target_count"]
        },
    )
    write_route(route_path, route)
    _write_github_outputs(
        args.github_output,
        {
            "route": route["route"],
            "selected_target_count": route["finalization"]["selected_target_count"],
        },
    )
    _append_summary(
        args.step_summary,
        "Touched-scope registry reconciliation",
        (
            f"final route: `{route['route']}`",
            f"broad fallback: `{str(route['route'] == 'broad').lower()}`",
            "owner labels: "
            + _bounded_summary_json(route["owner_labels"]),
            "selected producers: "
            + _bounded_summary_json(
                route["finalization"]["selected_targets"]
            ),
        ),
    )
    return 0


def _action_build(args: argparse.Namespace, root: Path) -> int:
    route_path = _route_path(args)
    route = read_route(route_path)
    started = time.monotonic()
    try:
        returncode, route = execute_build(
            route,
            root=root,
            build_dir=Path(args.build_dir),
        )
    except RouteError as exc:
        _record_action(
            route,
            name="build",
            status="failed",
            elapsed_seconds=time.monotonic() - started,
            details={"error": str(exc)},
        )
        write_route(route_path, route)
        print(f"touched_scope: error: {exc}", file=sys.stderr)
        return 2
    _record_action(
        route,
        name="build",
        status="passed" if returncode == 0 else "failed",
        elapsed_seconds=time.monotonic() - started,
        details={"returncode": returncode},
    )
    write_route(route_path, route)
    _write_github_outputs(
        args.github_output,
        {"ninja_edge_count": route["build"]["ninja_edge_count"]},
    )
    _append_summary(
        args.step_summary,
        "Touched-scope build",
        (f"unique Ninja commands: `{route['build']['ninja_edge_count']}`",),
    )
    return returncode


def _action_test(args: argparse.Namespace, root: Path) -> int:
    route_path = _route_path(args)
    route = read_route(route_path)
    started = time.monotonic()
    try:
        returncode, route = execute_tests(
            route,
            root=root,
            build_dir=Path(args.build_dir),
            timeout=args.timeout,
            jobs=args.jobs,
        )
    except RouteError as exc:
        _record_action(
            route,
            name="test",
            status="failed",
            elapsed_seconds=time.monotonic() - started,
            details={"error": str(exc)},
        )
        write_route(route_path, route)
        print(f"touched_scope: error: {exc}", file=sys.stderr)
        return 2
    _record_action(
        route,
        name="test",
        status="passed" if returncode == 0 else "failed",
        elapsed_seconds=time.monotonic() - started,
        details={"returncode": returncode},
    )
    write_route(route_path, route)
    _write_github_outputs(
        args.github_output,
        {"selected_test_count": route["test"]["selected_test_count"]},
    )
    _append_summary(
        args.step_summary,
        "Touched-scope tests",
        (f"unique CTest entries: `{route['test']['selected_test_count']}`",),
    )
    return returncode


def _legacy_run(args: argparse.Namespace, root: Path) -> int:
    route_path = _route_path(args)
    result = _action_plan(args, root)
    if result:
        return result
    result = _action_structural(args, root)
    if result:
        return result
    route = read_route(route_path)
    if not route["needs_cpp"]:
        return 0
    configure = _run_command(
        ("cmake", "--preset", args.preset, "--fresh"),
        cwd=root,
    )
    if configure.returncode != 0:
        return configure.returncode
    for action in (_action_finalize, _action_build, _action_test):
        result = action(args, root)
        if result:
            return result
    return 0


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="Repository root")
    parser.add_argument(
        "--action",
        choices=("plan", "finalize", "structural", "build", "test"),
        default="plan",
    )
    parser.add_argument("--base-ref", default="origin/main")
    parser.add_argument("--head-ref", default="HEAD")
    parser.add_argument("--changed-file", action="append", default=[])
    parser.add_argument("--output", help="Route JSON output for plan action")
    parser.add_argument("--plan", help="Existing route JSON for later actions")
    parser.add_argument("--build-dir", default="build/ci-fast")
    parser.add_argument("--preset", default="ci-fast")
    parser.add_argument("--preset-build-dir", default="build/ci-fast")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(os.cpu_count() or 1, 1),
    )
    parser.add_argument("--github-output")
    parser.add_argument("--step-summary")
    parser.add_argument("--print", dest="print_only", action="store_true")
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args(argv)
    if args.print_only and args.run:
        parser.error("choose only one of --print or --run")
    if args.action != "plan" and not args.plan:
        parser.error(f"--action {args.action} requires --plan")
    if args.jobs <= 0 or args.timeout <= 0:
        parser.error("--jobs and --timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve()
    if args.run:
        return _legacy_run(args, root)
    actions = {
        "plan": _action_plan,
        "structural": _action_structural,
        "finalize": _action_finalize,
        "build": _action_build,
        "test": _action_test,
    }
    return int(actions[args.action](args, root))


if __name__ == "__main__":
    raise SystemExit(main())
