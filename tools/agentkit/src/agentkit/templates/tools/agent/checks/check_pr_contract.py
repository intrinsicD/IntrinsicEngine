#!/usr/bin/env python3
"""Validate the PR template contains the required sections.

``--mode ci`` (default) fails if ``.github/pull_request_template.md`` is missing
required sections from ``agentkit.toml`` ([pr].required_sections). ``--mode
local`` prints review-focus hints based on changed paths.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import _common as c

TOOL = "check_pr_contract"


def main() -> int:
    parser = c.base_parser(__doc__ or TOOL)
    parser.add_argument("--mode", choices=["ci", "local"], default="ci")
    parser.add_argument("--base-ref", default="origin/main")
    args = parser.parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, strict=(args.mode == "ci"))

    required = c.cfg_get(cfg, "pr.required_sections", ["Summary", "Tests", "Docs"])
    template = root / ".github" / "pull_request_template.md"

    if args.mode == "local":
        try:
            out = subprocess.run(
                ["git", "diff", "--name-only", f"{args.base_ref}...HEAD"],
                cwd=str(root), capture_output=True, text=True, check=True,
            )
            changed = [ln for ln in out.stdout.splitlines() if ln.strip()]
        except (subprocess.CalledProcessError, FileNotFoundError):
            changed = []
        print(f"[{TOOL}] review focus for {len(changed)} changed file(s):")
        print(f"[{TOOL}] confirm the PR description fills: {', '.join(required)}")
        if any(f.startswith("docs/") for f in changed):
            print(f"[{TOOL}] - docs changed: keep the docs-sync decision honest.")
        if any(f.startswith("tasks/") for f in changed):
            print(f"[{TOOL}] - tasks changed: scope should match one task.")
        return c.EXIT_OK

    if not template.is_file():
        rep.error("missing .github/pull_request_template.md")
        return rep.finish("PR template OK")
    text = template.read_text(encoding="utf-8")
    headings = {line[3:].strip() for line in text.splitlines() if line.startswith("## ")}
    for section in required:
        if section not in headings:
            rep.error(f"PR template missing required section '## {section}'")
    return rep.finish(f"PR template OK ({len(required)} required section(s))")


if __name__ == "__main__":
    sys.exit(main())
