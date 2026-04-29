#!/usr/bin/env python3
"""Check architecture/PR contract coverage for local and CI workflows."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REQUIRED_PR_TEMPLATE_SECTIONS = [
    "## Summary",
    "## Type",
    "## Layering",
    "## Tests",
    "## Docs",
    "## Performance",
    "## Benchmarking",
    "## Agent self-review",
    "## Temporary shims",
]

REQUIRED_CHANGED_PATH_PREFIXES = [
    "src/",
    "tests/",
    "tools/",
    "docs/",
    ".github/workflows/",
    "CMakeLists.txt",
    "cmake/",
]

CHECKLIST_HINTS = {
    "src/": "Layering, ownership, lifetime, concurrency, error handling, tests, docs sync",
    "tests/": "Test category placement, naming/labels, regression scope",
    "tools/": "Tool ownership docs, strict/warning mode decisions, CI references",
    "docs/": "Canonical vs migration status, link integrity, contract consistency",
    ".github/workflows/": "Workflow readability, triggers, scope, strict validator wiring",
    "CMakeLists.txt": "Build graph ownership, target boundaries, migration safety",
    "cmake/": "Preset/config hygiene, target abstraction, CI reproducibility",
}


def run_git_diff_name_only(base_ref: str) -> list[str]:
    cmd = ["git", "diff", "--name-only", f"{base_ref}...HEAD"]
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"warning: could not compute diff against {base_ref}: {result.stderr.strip()}")
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def check_pr_template(repo_root: Path) -> list[str]:
    errors: list[str] = []
    template = repo_root / ".github" / "pull_request_template.md"
    if not template.is_file():
        return [f"missing PR template: {template}"]
    content = template.read_text(encoding="utf-8")
    for section in REQUIRED_PR_TEMPLATE_SECTIONS:
        if section not in content:
            errors.append(f"PR template missing section: {section}")
    return errors


def summarize_local_check(changed_files: list[str]) -> int:
    if not changed_files:
        print("No changed files detected relative to base ref; nothing to summarize.")
        return 0

    print("Changed files:")
    for path in changed_files:
        print(f"  - {path}")

    matched_prefixes = set()
    for path in changed_files:
        for prefix in REQUIRED_CHANGED_PATH_PREFIXES:
            if path == prefix or path.startswith(prefix):
                matched_prefixes.add(prefix)

    if not matched_prefixes:
        print("No architecture-contract hints triggered by changed paths.")
        return 0

    print("\nRequired review focus from changed-file mapping:")
    for prefix in sorted(matched_prefixes):
        print(f"  - {prefix}: {CHECKLIST_HINTS[prefix]}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="Repository root path")
    parser.add_argument("--mode", choices=["local", "ci"], default="local")
    parser.add_argument("--base-ref", default="origin/main", help="Base ref for local diff mode")
    args = parser.parse_args()

    repo_root = Path(args.root).resolve()

    if args.mode == "ci":
        errors = check_pr_template(repo_root)
        if errors:
            print("PR contract check failed:")
            for err in errors:
                print(f"  - {err}")
            return 1
        print("PR contract check passed: PR template contains required sections.")
        return 0

    changed_files = run_git_diff_name_only(args.base_ref)
    return summarize_local_check(changed_files)


if __name__ == "__main__":
    sys.exit(main())
