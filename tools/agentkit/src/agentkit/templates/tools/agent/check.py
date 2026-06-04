#!/usr/bin/env python3
"""Run all agentkit checks for this repository.

Vendored, dependency-free, and self-locating: it invokes the sibling
``checks/*.py`` scripts, so it works in any generated repo with no agentkit
install and no external launcher on disk. This is the local preview of the
``ci-docs`` gate.

    python3 tools/agent/check.py            # warning mode
    python3 tools/agent/check.py --strict   # fail on findings
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent       # tools/agent
ROOT = HERE.parent.parent                     # repository root
CHECKS = HERE / "checks"

# (script, extra args, supports --strict) — keep in sync with the ci-docs workflow.
AGGREGATE: list[tuple[str, list[str], bool]] = [
    ("check_tasks.py", [], True),
    ("check_doc_links.py", [], True),
    ("check_docs_sync.py", [], True),
    ("check_root_hygiene.py", [], True),
    ("check_workflow_names.py", [], True),
    ("check_agent_config.py", [], True),
    ("check_pr_contract.py", ["--mode", "ci"], False),
]


def main() -> int:
    strict = "--strict" in sys.argv[1:]
    overall = 0
    for name, extra, supports_strict in AGGREGATE:
        script = CHECKS / name
        if not script.is_file():
            print(f"[check] SKIP {name} (not installed)")
            continue
        cmd = [sys.executable, str(script), "--root", str(ROOT), *extra]
        if strict and supports_strict:
            cmd.append("--strict")
        print(f"\n[check] $ {name} {' '.join(extra)}".rstrip())
        if subprocess.run(cmd).returncode != 0:
            overall = 1
    print("\n[check] " + ("checks FAILED" if overall else "all checks passed"))
    return overall


if __name__ == "__main__":
    sys.exit(main())
