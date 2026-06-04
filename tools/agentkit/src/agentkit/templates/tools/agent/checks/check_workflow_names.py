#!/usr/bin/env python3
"""Validate GitHub workflow naming and structure policy.

From ``agentkit.toml`` [workflows]: only ``allowed`` files may exist, all
``required`` files must exist. For each workflow: top-level ``name:`` must match
the filename stem, an ``on:`` trigger must be present, and the YAML must not be
compressed onto a single line.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import _common as c

TOOL = "check_workflow_names"
NAME_RE = re.compile(r"^name:\s*(.+?)\s*$")


def main() -> int:
    args = c.base_parser(__doc__ or TOOL).parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, args.strict)

    allowed = set(c.cfg_get(cfg, "workflows.allowed", []))
    required = set(c.cfg_get(cfg, "workflows.required", []))
    wf_dir = root / ".github" / "workflows"
    if not wf_dir.is_dir():
        if required:
            rep.error(f"missing .github/workflows/ (required: {sorted(required)})")
        return rep.finish("workflow naming OK")

    files = sorted(p for p in wf_dir.glob("*.yml")) + sorted(p for p in wf_dir.glob("*.yaml"))
    found = {p.name for p in files}

    if allowed:
        for unexpected in sorted(found - allowed):
            rep.error(f"unexpected workflow file: {unexpected} (allowed: {sorted(allowed)})")
    for missing in sorted(required - found):
        rep.error(f"missing required workflow file: {missing}")

    for path in files:
        lines = path.read_text(encoding="utf-8").splitlines()
        if len(lines) <= 1:
            rep.error(f"{path.name}: workflow is compressed to one line; keep YAML readable")
        name_value = None
        on_seen = False
        for line in lines:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            m = NAME_RE.match(line)
            if m and name_value is None:
                name_value = m.group(1).strip().strip("\"'")
            if re.match(r"^on:\s*", line) or stripped in ("on:",):
                on_seen = True
        if name_value != path.stem:
            rep.error(f"{path.name}: top-level name must equal stem '{path.stem}' (found {name_value!r})")
        if not on_seen:
            rep.error(f"{path.name}: missing explicit 'on:' trigger section")

    return rep.finish(f"workflow naming OK ({len(files)} workflow(s))")


if __name__ == "__main__":
    sys.exit(main())
