#!/usr/bin/env python3
"""Keep the repository root tidy.

* Only allowed markdown files may live at the repo root
  (``agentkit.toml`` [hygiene].allowed_root_markdown).
* If [hygiene].expected_top_level is non-empty, every (non-ignored) top-level
  entry must be on that allowlist — otherwise the check is skipped.
"""
from __future__ import annotations

import fnmatch
import sys
from pathlib import Path

import _common as c

TOOL = "check_root_hygiene"


def main() -> int:
    args = c.base_parser(__doc__ or TOOL).parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, args.strict)

    allowed_md = set(c.cfg_get(cfg, "hygiene.allowed_root_markdown", ["README.md"]))
    expected = set(c.cfg_get(cfg, "hygiene.expected_top_level", []) or [])
    globs = c.ignore_globs(cfg)

    for entry in sorted(root.iterdir()):
        name = entry.name
        if any(fnmatch.fnmatch(name, pat) for pat in globs):
            continue
        if entry.is_file() and name.endswith(".md") and name not in allowed_md:
            rep.error(f"unexpected root markdown file: {name} (allowed: {sorted(allowed_md)})")
        if expected and name not in expected:
            rep.warn(f"top-level entry not in expected_top_level allowlist: {name}")

    return rep.finish("root hygiene OK")


if __name__ == "__main__":
    sys.exit(main())
