"""Run the shipped, config-driven validators and aggregate their results.

This mirrors what CI runs, so ``agentkit check`` is a faithful local preview of
the ``ci-docs`` gate. Each shipped check is invoked as a standalone process so
the runner stays decoupled from their internals.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from . import config as cfgmod

# (check filename, extra args, supports --strict)
_AGGREGATE: list[tuple[str, list[str], bool]] = [
    ("check_tasks.py", ["--root", "."], True),
    ("check_doc_links.py", ["--root", "."], True),
    ("check_root_hygiene.py", ["--root", "."], True),
    ("check_workflow_names.py", ["--root", "."], True),
    ("check_agent_config.py", ["--root", "."], True),
    ("check_pr_contract.py", ["--root", ".", "--mode", "ci"], False),
]


def run_checks(target: Path, *, strict: bool) -> int:
    target = Path(target)
    checks_dir = target / cfgmod.CHECKS_DIR
    if not checks_dir.is_dir():
        print(f"[agentkit] no checks found at {checks_dir} — run `agentkit init` first.")
        return 2

    overall = 0
    for name, args, supports_strict in _AGGREGATE:
        script = checks_dir / name
        if not script.is_file():
            print(f"[agentkit] SKIP {name} (not installed)")
            continue
        cmd = [sys.executable, str(script), *args]
        if strict and supports_strict:
            cmd.append("--strict")
        print(f"\n[agentkit] $ {name} {' '.join(args)}{' --strict' if strict and supports_strict else ''}")
        result = subprocess.run(cmd, cwd=str(target))
        if result.returncode != 0:
            overall = 1
    print("\n[agentkit] " + ("checks FAILED" if overall else "all checks passed"))
    return overall
