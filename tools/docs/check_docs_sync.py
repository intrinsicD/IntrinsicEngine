#!/usr/bin/env python3
"""Check whether code changes include required docs updates per policy rules."""

from __future__ import annotations

import argparse
import fnmatch
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Rule:
    trigger_glob: str
    required_any_globs: list[str]


def parse_rules(path: Path) -> list[Rule]:
    """Parse a minimal YAML subset for docs-sync rules.

    Supported format:

      some/glob/**:
        one_of:
          - docs/path.md
          - docs/other/**
    """

    rules: list[Rule] = []
    current_trigger: str | None = None
    in_one_of = False
    required: list[str] = []

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        stripped = line.strip()

        if not stripped or stripped.startswith("#"):
            continue

        if not line.startswith(" ") and stripped.endswith(":") and stripped != "one_of:":
            if current_trigger is not None:
                rules.append(Rule(trigger_glob=current_trigger, required_any_globs=required.copy()))
            current_trigger = stripped[:-1].strip()
            in_one_of = False
            required = []
            continue

        if stripped == "one_of:":
            in_one_of = True
            continue

        if in_one_of and stripped.startswith("- "):
            required.append(stripped[2:].strip())
            continue

        raise ValueError(f"Unsupported rules format line: {raw}")

    if current_trigger is not None:
        rules.append(Rule(trigger_glob=current_trigger, required_any_globs=required.copy()))

    return rules


def git_changed_paths(root: Path, base_ref: str) -> tuple[list[str], str | None]:
    try:
        merge_base = subprocess.check_output(
            ["git", "-C", str(root), "merge-base", "HEAD", base_ref],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except subprocess.CalledProcessError as exc:
        message = exc.output.strip() or f"unable to resolve merge-base with {base_ref}"
        return [], message

    diff_cmd = [
        "git",
        "-C",
        str(root),
        "diff",
        "--name-only",
        "--diff-filter=ACMR",
        f"{merge_base}...HEAD",
    ]

    try:
        output = subprocess.check_output(diff_cmd, text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as exc:
        message = exc.output.strip() or "failed to compute changed files"
        return [], message

    paths = [line.strip() for line in output.splitlines() if line.strip()]
    return paths, None


def evaluate(rules: list[Rule], changed_paths: list[str]) -> tuple[list[str], int]:
    violations: list[str] = []
    evaluated = 0

    for rule in rules:
        matched_triggers = [p for p in changed_paths if fnmatch.fnmatch(p, rule.trigger_glob)]
        if not matched_triggers:
            continue

        evaluated += 1

        has_required_doc = any(
            fnmatch.fnmatch(path, required_glob)
            for path in changed_paths
            for required_glob in rule.required_any_globs
        )

        if has_required_doc:
            continue

        required_fmt = ", ".join(rule.required_any_globs) if rule.required_any_globs else "<none>"
        sample = ", ".join(matched_triggers[:5])
        violations.append(
            f"trigger '{rule.trigger_glob}' matched [{sample}] but none of [{required_fmt}] were changed"
        )

    return violations, evaluated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument(
        "--rules",
        type=Path,
        default=Path("tools/docs/docs_sync_rules.yaml"),
        help="Path to docs sync rules file",
    )
    parser.add_argument(
        "--base-ref",
        default="origin/main",
        help="Git base ref used for --diff-mode (default: origin/main)",
    )
    parser.add_argument(
        "--diff-mode",
        action="store_true",
        help="Evaluate files changed from merge-base(base-ref)...HEAD",
    )
    parser.add_argument(
        "--files",
        nargs="*",
        default=None,
        help="Explicit file list (bypasses git diff lookup)",
    )
    parser.add_argument("--strict", action="store_true", help="Fail on violations")
    args = parser.parse_args()

    root = args.root.resolve()
    rules_path = (root / args.rules).resolve() if not args.rules.is_absolute() else args.rules

    if not rules_path.exists():
        print(f"[check_docs_sync] Rules file missing: {rules_path}")
        return 1

    try:
        rules = parse_rules(rules_path)
    except ValueError as exc:
        print(f"[check_docs_sync] Invalid rules file: {exc}")
        return 1

    if args.files is not None and len(args.files) > 0:
        changed = [Path(p).as_posix() for p in args.files]
        source = "--files"
    elif args.diff_mode:
        changed, diff_error = git_changed_paths(root, args.base_ref)
        source = f"git diff {args.base_ref}"
        if diff_error:
            print(f"[check_docs_sync] Could not compute diff: {diff_error}")
            print("[check_docs_sync] WARNING MODE: non-fatal diff lookup failure.")
            return 0 if not args.strict else 1
    else:
        changed = [
            p.relative_to(root).as_posix()
            for p in root.rglob("*")
            if p.is_file() and ".git/" not in p.as_posix()
        ]
        source = "repo scan"

    violations, evaluated_rules = evaluate(rules, changed)

    print(f"[check_docs_sync] Root: {root}")
    print(f"[check_docs_sync] Rules: {rules_path.relative_to(root)}")
    print(f"[check_docs_sync] Changed files source: {source}")
    print(f"[check_docs_sync] Changed files count: {len(changed)}")
    print(f"[check_docs_sync] Triggered rules: {evaluated_rules}")

    if violations:
        print("[check_docs_sync] Missing docs sync updates:")
        for violation in violations:
            print(f"  - {violation}")
        if args.strict:
            print("[check_docs_sync] STRICT MODE: failing.")
            return 1
        print("[check_docs_sync] WARNING MODE: non-fatal.")
    else:
        print("[check_docs_sync] Docs sync rules satisfied.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
