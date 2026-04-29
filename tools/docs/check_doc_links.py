#!/usr/bin/env python3
"""Validate relative markdown links under a repository root."""

from __future__ import annotations

import argparse
import fnmatch
import re
from pathlib import Path

LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
FENCED_CODE_PATTERN = re.compile(r"```.*?```", re.DOTALL)
INLINE_CODE_PATTERN = re.compile(r"`[^`]*`")
IGNORED_TOP_LEVEL_PATTERNS = {
    ".git",
    ".idea",
    "Testing",
    "build",
    "build-*",
    "cmake-build-*",
    "external",
    "experimental",
    "third_party",
}


def is_ignored_path(path: Path, root: Path) -> bool:
    try:
        relative = path.relative_to(root)
    except ValueError:
        return False

    if not relative.parts:
        return False

    top_level = relative.parts[0]
    return any(fnmatch.fnmatchcase(top_level, pattern) for pattern in IGNORED_TOP_LEVEL_PATTERNS)


def is_ignored_link(link: str) -> bool:
    stripped = link.strip()
    return (
        not stripped
        or stripped.startswith("http://")
        or stripped.startswith("https://")
        or stripped.startswith("mailto:")
        or stripped.startswith("#")
    )


def normalize_target(source_file: Path, raw_link: str) -> Path:
    target_text = raw_link.split("#", 1)[0].strip()
    return (source_file.parent / target_text).resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail if broken links are found")
    args = parser.parse_args()

    root = args.root.resolve()
    markdown_files = sorted(p for p in root.rglob("*.md") if not is_ignored_path(p, root))

    broken: list[tuple[Path, str]] = []
    checked = 0

    for md_file in markdown_files:
        try:
            content = md_file.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            print(f"[check_doc_links] Skipping non-UTF8 markdown: {md_file}")
            continue

        content_wo_code = FENCED_CODE_PATTERN.sub("", content)
        content_wo_code = INLINE_CODE_PATTERN.sub("", content_wo_code)

        for match in LINK_PATTERN.finditer(content_wo_code):
            link = match.group(1).strip()
            if is_ignored_link(link):
                continue
            checked += 1
            target = normalize_target(md_file, link)
            if not target.exists():
                broken.append((md_file, link))

    print(f"[check_doc_links] Root: {root}")
    print(f"[check_doc_links] Checked relative links: {checked}")
    print(f"[check_doc_links] Mode: {'strict' if args.strict else 'warning'}")

    if broken:
        print("[check_doc_links] Broken relative links:")
        for md_file, link in broken:
            print(f"  - {md_file.relative_to(root)} -> {link}")
        print("[check_doc_links] Action: update moved paths or convert to canonical links.")
        if args.strict:
            print("[check_doc_links] STRICT MODE: failing.")
            return 1
        print("[check_doc_links] WARNING MODE: non-fatal.")
    else:
        print("[check_doc_links] No broken relative links found.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
