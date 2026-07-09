#!/usr/bin/env python3
"""Fail fast when CI steps are blocked by missing build artifacts."""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path

BLOCKED_EXIT_CODE = 3


def _is_executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def _target_declared(build_dir: Path, target: str) -> bool:
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


def _validate_target_names(targets: list[str], source: str) -> str | None:
    if not targets:
        return f"{source} contains no test binary targets"
    if len(targets) != len(set(targets)):
        return f"{source} contains duplicate test binary targets"
    invalid = [
        target
        for target in targets
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.+-]*", target) is None
    ]
    if invalid:
        return f"{source} contains invalid target name(s): {', '.join(invalid)}"
    return None


def _load_inventory(path: Path) -> tuple[list[str], str | None]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        return [], f"test target inventory does not exist: {path}"
    except (OSError, UnicodeError) as exc:
        return [], f"cannot read test target inventory {path}: {exc}"

    targets: list[str] = []
    for line_number, line in enumerate(lines, start=1):
        target = line.strip()
        if not target:
            return [], f"{path}:{line_number}: blank inventory entries are invalid"
        targets.append(target)

    return targets, _validate_target_names(targets, str(path))


def _resolve_test_targets(args: argparse.Namespace) -> tuple[list[str], str | None]:
    inventory = getattr(args, "inventory", None)
    targets = list(getattr(args, "targets", None) or [])
    if inventory is not None:
        if args.skip_undeclared:
            return [], "--skip-undeclared cannot be combined with --inventory"
        return _load_inventory(inventory)
    return targets, _validate_target_names(targets, "--targets")


def check_test_binaries(args: argparse.Namespace) -> int:
    build_dir = args.build_dir
    targets, declaration_error = _resolve_test_targets(args)
    if declaration_error:
        print(
            "BLOCKED: invalid test binary prerequisite declaration; "
            f"{declaration_error}."
        )
        return BLOCKED_EXIT_CODE

    missing: list[tuple[str, Path]] = []
    skipped: list[str] = []

    for target in targets:
        if args.skip_undeclared and not _target_declared(build_dir, target):
            skipped.append(target)
            continue
        artifact = build_dir / "bin" / target
        if not _is_executable(artifact):
            missing.append((target, artifact))

    if missing:
        print("BLOCKED: required test binary artifact(s) are missing; not running the dependent step.")
        for target, artifact in missing:
            print(f" - {artifact} (producer target: {target})")
        print("Build the missing producer target(s) first, then rerun the dependent command.")
        print("Example: cmake --build --preset ci --target " + " ".join(target for target, _ in missing))
        return BLOCKED_EXIT_CODE

    if skipped:
        print("Prerequisite check skipped undeclared optional target(s): " + ", ".join(skipped))
    print(f"Prerequisite check passed for {len(targets) - len(skipped)} test binary target(s).")
    return 0


def check_path(args: argparse.Namespace) -> int:
    path = args.path
    if args.kind == "dir":
        exists = path.is_dir()
        description = "directory"
    else:
        exists = path.is_file()
        description = "file"

    if exists:
        print(f"Prerequisite check passed: {description} exists: {path}")
        return 0

    print(f"BLOCKED: required {description} is missing; not running the dependent step: {path}")
    if args.producer_target:
        print(f"Build or run producer target first: {args.producer_target}")
    if args.prior_log:
        print(f"Check the earlier root-cause log before this dependent step: {args.prior_log}")
    return BLOCKED_EXIT_CODE


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    binaries = subparsers.add_parser("test-binaries", help="Check CTest-producing executable targets exist")
    binaries.add_argument("--build-dir", type=Path, required=True, help="CMake build directory")
    binaries.add_argument("--skip-undeclared", action="store_true", help="Skip targets not declared in build.ninja")
    target_source = binaries.add_mutually_exclusive_group(required=True)
    target_source.add_argument(
        "--targets",
        nargs="+",
        help="Executable target names expected under <build-dir>/bin",
    )
    target_source.add_argument(
        "--inventory",
        type=Path,
        help="Generated one-target-per-line aggregate inventory",
    )
    binaries.set_defaults(func=check_test_binaries)

    path = subparsers.add_parser("path", help="Check a required file or directory exists")
    path.add_argument("--path", type=Path, required=True, help="Required path")
    path.add_argument("--kind", choices=("file", "dir"), default="file", help="Required path kind")
    path.add_argument("--producer-target", help="Producer target/command to mention in blocked output")
    path.add_argument("--prior-log", help="Prior build/configure log to inspect first")
    path.set_defaults(func=check_path)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
