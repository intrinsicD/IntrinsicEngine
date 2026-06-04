#!/usr/bin/env python3
"""Enforce "if you change X, also update a doc in Y" rules in the same change.

Rules live in a TOML file (default ``tools/agent/docs_sync_rules.toml``):

    [[rule]]
    name = "source changes need docs"
    when = ["src/**", "lib/**"]
    require_one_of = ["docs/**", "CHANGELOG.md"]

Changed files come from ``--files`` or ``git diff`` against ``--base-ref``.
Globs are fnmatch-style (``*`` spans path separators). If the rules file is
absent or git cannot resolve the base ref, the check is a no-op (exit 0).
"""
from __future__ import annotations

import fnmatch
import subprocess
import sys
import tomllib
from pathlib import Path

import _common as c

TOOL = "check_docs_sync"


def _match_any(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatch(path, pat) for pat in patterns)


def _changed_files(root: Path, base_ref: str, explicit: list[str] | None) -> list[str] | None:
    if explicit:
        return explicit
    for rev in (f"{base_ref}...HEAD", base_ref):
        try:
            out = subprocess.run(
                ["git", "diff", "--name-only", rev],
                cwd=str(root),
                capture_output=True,
                text=True,
                check=True,
            )
            return [line.strip() for line in out.stdout.splitlines() if line.strip()]
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return None


def main() -> int:
    parser = c.base_parser(__doc__ or TOOL)
    parser.add_argument("--rules", default="tools/agent/docs_sync_rules.toml")
    parser.add_argument("--base-ref", default="origin/main")
    parser.add_argument("--files", nargs="*", help="explicit changed-file list (skips git)")
    args = parser.parse_args()
    root = Path(args.root)
    rep = c.Reporter(TOOL, args.strict)

    rules_path = root / args.rules
    if not rules_path.is_file():
        print(f"[{TOOL}] no rules file at {args.rules}; nothing to enforce.")
        return c.EXIT_OK
    with rules_path.open("rb") as handle:
        rules = tomllib.load(handle).get("rule", [])
    if not rules:
        print(f"[{TOOL}] no rules defined; nothing to enforce.")
        return c.EXIT_OK

    changed = _changed_files(root, args.base_ref, args.files)
    if changed is None:
        print(f"[{TOOL}] could not determine changed files (no git/base ref); skipping.")
        return c.EXIT_OK

    for rule in rules:
        name = rule.get("name", "<unnamed rule>")
        when = rule.get("when", [])
        require = rule.get("require_one_of", [])
        triggered = [f for f in changed if _match_any(f, when)]
        if not triggered:
            continue
        if not any(_match_any(f, require) for f in changed):
            rep.error(
                f"rule '{name}': changed {triggered[:3]} but no file matched "
                f"required docs {require}"
            )

    return rep.finish(f"docs-sync OK ({len(rules)} rule(s), {len(changed)} changed file(s))")


if __name__ == "__main__":
    sys.exit(main())
