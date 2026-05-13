#!/usr/bin/env python3
"""Plan or run conservative verification commands for touched repository scope.

This helper is intentionally a fast local/PR aid, not a replacement for the
canonical full CPU-supported gate documented in AGENTS.md.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path


DEFAULT_EXCLUDE_LABELS = "gpu|vulkan|slow|flaky-quarantine"


@dataclass(frozen=True)
class Command:
    argv: tuple[str, ...]
    reason: str

    def shell_text(self) -> str:
        return shlex.join(self.argv)


@dataclass
class Plan:
    changed_files: list[str]
    build_targets: set[str] = field(default_factory=set)
    labels: set[str] = field(default_factory=set)
    structural_checks: set[str] = field(default_factory=set)
    broad_cpu_gate: bool = False
    reasons: list[str] = field(default_factory=list)


LAYER_TARGETS: dict[str, tuple[tuple[str, ...], tuple[str, ...]]] = {
    "src/geometry/": (("IntrinsicGeometryTests",), ("geometry",)),
    "src/assets/": (("IntrinsicAssetUnitTests",), ("assets",)),
    "src/ecs/": (("IntrinsicECSTests", "IntrinsicEcsContractTests"), ("ecs",)),
    "src/platform/": (("IntrinsicPlatformTests",), ("platform",)),
    "src/runtime/": (
        ("IntrinsicRuntimeContractTests", "IntrinsicRuntimeGraphicsCpuTests", "IntrinsicRuntimeSelectionContractTests"),
        ("runtime",),
    ),
    "src/graphics/rhi/": (
        ("IntrinsicGraphicsRhiCpuUnitTests", "IntrinsicGraphicsContractTests"),
        ("graphics",),
    ),
    "src/graphics/assets/": (
        ("IntrinsicGraphicsAssetsUnitTests", "IntrinsicGraphicsContractTests"),
        ("graphics",),
    ),
    "src/graphics/renderer/": (
        (
            "IntrinsicGraphicsRendererCpuUnitTests",
            "IntrinsicGraphicsContractTests",
            "IntrinsicGraphicsContractCpuTests",
        ),
        ("graphics",),
    ),
    "src/graphics/framegraph/": (
        ("IntrinsicGraphicsRendererCpuUnitTests", "IntrinsicGraphicsContractCpuTests"),
        ("graphics",),
    ),
    "src/graphics/vulkan/": (("IntrinsicGraphicsVulkanContractTests",), ("graphics",)),
}


TEST_SCOPE_MAP: tuple[tuple[str, tuple[str, ...], tuple[str, ...]], ...] = (
    ("tests/unit/geometry/", ("IntrinsicGeometryTests",), ("geometry",)),
    ("tests/integration/geometry/", ("IntrinsicGeometryTests",), ("geometry",)),
    ("tests/regression/numerical/", ("IntrinsicGeometryTests",), ("geometry",)),
    ("tests/unit/core/", ("IntrinsicCoreTests", "IntrinsicCoreWrapperUnitTests"), ("core",)),
    ("tests/unit/assets/", ("IntrinsicAssetUnitTests",), ("assets",)),
    ("tests/unit/ecs/", ("IntrinsicECSTests",), ("ecs",)),
    ("tests/contract/ecs/", ("IntrinsicEcsContractTests",), ("ecs",)),
    ("tests/unit/platform/", ("IntrinsicPlatformTests",), ("platform",)),
    ("tests/contract/platform/", ("IntrinsicPlatformTests",), ("platform",)),
    ("tests/unit/graphics/", ("IntrinsicGraphicsRendererCpuUnitTests", "IntrinsicGraphicsRhiCpuUnitTests"), ("graphics",)),
    ("tests/contract/graphics/", ("IntrinsicGraphicsContractTests", "IntrinsicGraphicsContractCpuTests"), ("graphics",)),
    ("tests/contract/runtime/", ("IntrinsicRuntimeContractTests", "IntrinsicRuntimeSelectionContractTests"), ("runtime",)),
    ("tests/integration/runtime/", ("IntrinsicRuntimeGraphicsCpuTests",), ("runtime",)),
)


def run_git_diff_name_only(root: Path, base_ref: str) -> list[str]:
    cmd = ["git", "diff", "--name-only", f"{base_ref}...HEAD"]
    result = subprocess.run(cmd, cwd=root, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"warning: could not compute diff against {base_ref}: {result.stderr.strip()}", file=sys.stderr)
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def is_target_declared(build_dir: Path, target: str) -> bool:
    ninja_file = build_dir / "build.ninja"
    if not ninja_file.is_file():
        return True
    text = ninja_file.read_text(encoding="utf-8", errors="ignore")
    needles = (
        f"bin/{target}",
        f"CMakeFiles/{target}",
        f"{target}:",
        f"{target}$:",
    )
    return any(needle in text for needle in needles)


def add_layer_scope(plan: Plan, path: str, prefix: str, targets: tuple[str, ...], labels: tuple[str, ...]) -> None:
    plan.build_targets.update(targets)
    plan.labels.update(labels)
    plan.structural_checks.add("layering")
    plan.reasons.append(f"{path}: matched {prefix}; selected {', '.join(labels)} verification")


def analyze_changed_files(changed_files: list[str]) -> Plan:
    plan = Plan(changed_files=changed_files)

    for path in changed_files:
        matched = False

        if path in {"CMakeLists.txt", "CMakePresets.json"} or path.startswith("cmake/") or path == "tests/CMakeLists.txt":
            plan.broad_cpu_gate = True
            plan.structural_checks.add("test_layout")
            plan.reasons.append(f"{path}: build/test graph changed; selected broad CPU gate")
            continue

        if path.startswith(".github/workflows/"):
            plan.structural_checks.update({"docs", "task_policy"})
            plan.reasons.append(f"{path}: workflow changed; selected policy/docs checks")
            matched = True

        if path.startswith("docs/") or path.endswith(".md"):
            plan.structural_checks.add("docs")
            plan.reasons.append(f"{path}: documentation changed; selected doc link check")
            matched = True

        if path.startswith("tasks/"):
            plan.structural_checks.add("task_policy")
            plan.reasons.append(f"{path}: task record changed; selected task policy check")
            matched = True

        if path.startswith("tools/agents/"):
            plan.structural_checks.add("task_policy")
            plan.reasons.append(f"{path}: task tooling changed; selected task policy check")
            matched = True
        elif path.startswith("tools/docs/"):
            plan.structural_checks.add("docs")
            plan.reasons.append(f"{path}: docs tooling changed; selected doc link check")
            matched = True
        elif path.startswith("tools/repo/"):
            plan.structural_checks.update({"layering", "test_layout"})
            plan.reasons.append(f"{path}: repo checker changed; selected structural checks")
            matched = True
        elif path.startswith("tools/ci/"):
            plan.structural_checks.add("touched_scope_tests")
            plan.reasons.append(f"{path}: CI helper changed; selected tooling regression tests")
            matched = True
        elif path.startswith("tools/"):
            plan.structural_checks.add("tool_smoke")
            plan.reasons.append(f"{path}: generic tool changed; selected broad tool smoke")
            matched = True

        if path.startswith("src/core/"):
            plan.broad_cpu_gate = True
            plan.structural_checks.add("layering")
            plan.reasons.append(f"{path}: core is a foundational layer; selected broad CPU gate")
            continue

        for prefix, (targets, labels) in LAYER_TARGETS.items():
            if path.startswith(prefix):
                add_layer_scope(plan, path, prefix, targets, labels)
                matched = True
                break

        if path.startswith("src/") and not matched:
            plan.broad_cpu_gate = True
            plan.structural_checks.add("layering")
            plan.reasons.append(f"{path}: source path has no narrow mapping; selected broad CPU gate")
            continue

        if path.startswith("tests/"):
            plan.structural_checks.add("test_layout")
            for prefix, targets, labels in TEST_SCOPE_MAP:
                if path.startswith(prefix):
                    plan.build_targets.update(targets)
                    plan.labels.update(labels)
                    plan.reasons.append(f"{path}: matched {prefix}; selected {', '.join(labels)} test scope")
                    matched = True
                    break
            if not matched and path.endswith((".cpp", ".cppm", ".hpp", ".h")):
                plan.broad_cpu_gate = True
                plan.reasons.append(f"{path}: test source has no narrow mapping; selected broad CPU gate")
            elif path.startswith("tests/regression/tooling/") and path.endswith(".py"):
                plan.structural_checks.add("touched_scope_tests")
                plan.reasons.append(f"{path}: tooling regression changed; selected tooling regression tests")
                matched = True

        if path.startswith("assets/shaders/"):
            plan.structural_checks.add("shader_outputs")
            plan.labels.add("graphics")
            plan.reasons.append(f"{path}: shader asset changed; selected shader output check and graphics labels")
            matched = True

        if not matched and not path.startswith(("external/", "third_party/", "build/", "cmake-build-debug/")):
            plan.broad_cpu_gate = True
            plan.reasons.append(f"{path}: unknown path; selected broad CPU gate")

    return plan


def structural_commands(root_arg: str, checks: set[str]) -> list[Command]:
    commands: list[Command] = []
    if "touched_scope_tests" in checks:
        commands.append(Command(("python3", "tests/regression/tooling/Test.TouchedScope.py"), "tooling regression tests"))
    if "tool_smoke" in checks:
        commands.append(Command(("python3", "tools/repo/check_pr_contract.py", "--root", root_arg, "--mode", "local"), "generic tool smoke"))
    if "layering" in checks:
        commands.append(Command(("python3", "tools/repo/check_layering.py", "--root", "src", "--strict"), "layering contract"))
    if "test_layout" in checks:
        commands.append(Command(("python3", "tools/repo/check_test_layout.py", "--root", root_arg, "--strict"), "test layout contract"))
    if "docs" in checks:
        commands.append(Command(("python3", "tools/docs/check_doc_links.py", "--root", root_arg), "documentation links"))
    if "task_policy" in checks:
        commands.append(Command(("python3", "tools/agents/check_task_policy.py", "--root", root_arg, "--strict"), "task policy"))
    if "shader_outputs" in checks:
        commands.append(Command(("python3", "tools/repo/check_shader_outputs.py", "--root", root_arg), "shader output policy"))
    return commands


def commands_for_plan(plan: Plan, args: argparse.Namespace) -> list[Command]:
    commands: list[Command] = []

    if not plan.changed_files:
        return commands

    if plan.broad_cpu_gate:
        commands.append(Command(("cmake", "--preset", args.preset), "configure broad CPU gate"))
        commands.append(Command(("cmake", "--build", "--preset", args.preset, "--target", "IntrinsicTests"), "build broad CPU gate"))
        commands.append(Command(("ctest", "--test-dir", args.preset_build_dir, "--output-on-failure", "-LE", DEFAULT_EXCLUDE_LABELS, "--timeout", str(args.timeout), f"-j{args.jobs}"), "run broad CPU-supported tests"))
    else:
        targets = sorted(t for t in plan.build_targets if is_target_declared(Path(args.build_dir), t))
        if targets:
            commands.append(Command(("cmake", "--build", args.build_dir, "--target", *targets), "build touched-scope test target(s)"))
        if plan.labels:
            label_regex = "|".join(sorted(plan.labels))
            commands.append(Command(("ctest", "--test-dir", args.build_dir, "--output-on-failure", "-L", label_regex, "-LE", DEFAULT_EXCLUDE_LABELS, "--timeout", str(args.timeout), f"-j{args.jobs}"), "run touched-scope CTest labels"))

    commands.extend(structural_commands(args.root, plan.structural_checks))
    return commands


def print_plan(plan: Plan, commands: list[Command]) -> None:
    print("Touched-scope verification plan")
    print("Changed files:")
    if plan.changed_files:
        for path in plan.changed_files:
            print(f"  - {path}")
    else:
        print("  - <none>")

    print("\nSelection notes:")
    if plan.reasons:
        for reason in plan.reasons:
            print(f"  - {reason}")
    else:
        print("  - No changed files detected; no commands selected.")

    print("\nCommands:")
    if commands:
        for command in commands:
            print(f"# {command.reason}")
            print(command.shell_text())
    else:
        print("# No commands selected.")

    print("\nNote: this touched-scope helper is not a replacement for the full PR/merge CPU gate.")


def run_commands(root: Path, commands: list[Command]) -> int:
    for command in commands:
        print(f"[touched_scope] {command.reason}: {command.shell_text()}", flush=True)
        result = subprocess.run(command.argv, cwd=root, check=False)
        if result.returncode != 0:
            return result.returncode
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="Repository root")
    parser.add_argument("--base-ref", default="origin/main", help="Base ref for git diff mode")
    parser.add_argument("--changed-file", action="append", default=[], help="Changed file path; may be passed multiple times")
    parser.add_argument("--build-dir", default="build/ci", help="Configured CMake build directory for narrow touched-scope builds")
    parser.add_argument("--preset", default="ci", help="CMake preset for broad fallback configure/build")
    parser.add_argument("--preset-build-dir", default="build/ci", help="CTest directory for broad preset fallback")
    parser.add_argument("--timeout", type=int, default=60, help="CTest timeout in seconds")
    parser.add_argument("--jobs", type=int, default=max(os.cpu_count() or 1, 1), help="CTest parallelism")
    parser.add_argument("--print", dest="print_only", action="store_true", help="Print the selected plan without running it")
    parser.add_argument("--run", action="store_true", help="Run the selected plan")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    changed_files = args.changed_file or run_git_diff_name_only(root, args.base_ref)
    changed_files = sorted(dict.fromkeys(path.strip().lstrip("./") for path in changed_files if path.strip()))
    plan = analyze_changed_files(changed_files)
    commands = commands_for_plan(plan, args)

    if args.run and args.print_only:
        print("error: choose only one of --run or --print", file=sys.stderr)
        return 2
    if not args.run:
        print_plan(plan, commands)
        return 0
    return run_commands(root, commands)


if __name__ == "__main__":
    raise SystemExit(main())
