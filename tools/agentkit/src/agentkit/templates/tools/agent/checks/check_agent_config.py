#!/usr/bin/env python3
"""Validate that agent surfaces exist and point at the one contract.

The workflow's core invariant is a single authoritative contract mirrored to
every agent entry point. This check verifies the contract file exists and that
each enabled thin-redirect surface (CLAUDE.md, Copilot instructions, Codex
config) references it — so the surfaces never silently drift into rival policy.
"""
from __future__ import annotations

import sys
from pathlib import Path

import _common as c

TOOL = "check_agent_config"


def main() -> int:
    args = c.base_parser(__doc__ or TOOL).parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, args.strict)

    contract = c.cfg_get(cfg, "project.contract_file", "AGENTS.md")
    if not (root / contract).is_file():
        rep.error(f"contract file is missing: {contract}")

    surfaces = []
    if c.cfg_get(cfg, "harness.claude", True):
        surfaces.append(("CLAUDE.md", "CLAUDE.md"))
    if c.cfg_get(cfg, "harness.copilot", True):
        surfaces.append((".github/copilot-instructions.md", "Copilot instructions"))
    if c.cfg_get(cfg, "harness.codex", True):
        surfaces.append((".codex/config.yaml", "Codex config"))

    for rel, label in surfaces:
        path = root / rel
        if not path.is_file():
            rep.error(f"{label} is enabled but missing: {rel}")
            continue
        if contract not in path.read_text(encoding="utf-8"):
            rep.warn(f"{rel}: does not reference the contract '{contract}'")

    return rep.finish(f"agent surfaces OK ({len(surfaces)} redirect(s) checked)")


if __name__ == "__main__":
    sys.exit(main())
