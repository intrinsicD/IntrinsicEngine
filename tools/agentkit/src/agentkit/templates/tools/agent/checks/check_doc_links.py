#!/usr/bin/env python3
"""Validate that relative markdown links resolve to existing files.

Skips external links (http/https/mailto), pure ``#anchor`` links, and anything
inside fenced or inline code. A link beginning with ``/`` is treated as
repository-root-relative (resolved against ``--root``) — this convention keeps
links stable when docs are mirrored verbatim into skill ``references/``.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import _common as c

TOOL = "check_doc_links"
LINK_RE = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)]+)\)")
INLINE_CODE_RE = re.compile(r"`[^`]*`")
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:", "//")


def _strip_code(text: str) -> str:
    out_lines: list[str] = []
    in_fence = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            out_lines.append("")
            continue
        out_lines.append("" if in_fence else INLINE_CODE_RE.sub("", line))
    return "\n".join(out_lines)


def main() -> int:
    args = c.base_parser(__doc__ or TOOL).parse_args()
    root = Path(args.root)
    cfg = c.load_config(root)
    rep = c.Reporter(TOOL, args.strict)

    checked = 0
    for path in c.iter_files(root, ".md", cfg):
        rel = path.relative_to(root)
        text = _strip_code(path.read_text(encoding="utf-8"))
        for raw in LINK_RE.findall(text):
            target = raw.strip().split()[0]  # drop optional "title"
            target = target.split("#", 1)[0].split("?", 1)[0]
            if not target or target.startswith("#"):
                continue
            if target.startswith(EXTERNAL_PREFIXES):
                continue
            checked += 1
            if target.startswith("/"):
                resolved = root / target.lstrip("/")
            else:
                resolved = path.parent / target
            if not resolved.exists():
                rep.error(f"{rel}: broken link -> {raw}")

    return rep.finish(f"all markdown links resolve ({checked} checked)")


if __name__ == "__main__":
    sys.exit(main())
