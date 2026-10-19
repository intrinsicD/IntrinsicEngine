#!/usr/bin/env python3
"""Validate relative markdown links under a repository root."""

from __future__ import annotations

import argparse
import fnmatch
import os
import re
import subprocess
from pathlib import Path

LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
FENCED_CODE_PATTERN = re.compile(r"```.*?```", re.DOTALL)
LINE_SUFFIX_PATTERN = re.compile(r":\d+(?:-\d+)?$")
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


def inline_code_spans(content: str) -> list[tuple[int, int]]:
    spans: list[tuple[int, int]] = []
    position = 0
    while position < len(content):
        if content[position] != "`":
            position += 1
            continue

        tick_end = position + 1
        while tick_end < len(content) and content[tick_end] == "`":
            tick_end += 1

        fence = content[position:tick_end]
        close = content.find(fence, tick_end)
        if close == -1:
            position = tick_end
            continue

        spans.append((position, close + len(fence)))
        position = close + len(fence)

    return spans


def is_inside_span(start: int, end: int, spans: list[tuple[int, int]]) -> bool:
    return any(span_start <= start and end <= span_end for span_start, span_end in spans)


def normalize_target(source_file: Path, raw_link: str) -> Path:
    target_text = raw_link.split("#", 1)[0].strip()
    return (source_file.parent / target_text).resolve()


def line_suffix_target_exists(source_file: Path, raw_link: str) -> bool:
    # Source links such as `file.cpp:42` or `file.cpp:42-60` name lines of an existing file.
    target_text = raw_link.split("#", 1)[0].strip()
    stripped = LINE_SUFFIX_PATTERN.sub("", target_text)
    return stripped != target_text and (source_file.parent / stripped).resolve().is_file()


def discover_markdown_files(root: Path) -> list[Path]:
    # Git-ignored files (local evidence, review copies) are not maintained documentation and
    # never exist in a CI checkout; outside a Git work tree every file is scanned.
    try:
        listed = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            check=True,
            capture_output=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        candidates = list(root.rglob("*.md"))
    else:
        # Filter here: a "*.md" pathspec depends on GIT_*_PATHSPECS and could silently match nothing.
        candidates = [root / os.fsdecode(name) for name in listed.split(b"\0") if name.endswith(b".md")]
    return sorted(p for p in candidates if p.is_file() and not is_ignored_path(p, root))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--strict", action="store_true", help="Fail if broken links are found")
    args = parser.parse_args()

    root = args.root.resolve()
    markdown_files = discover_markdown_files(root)

    broken: list[tuple[Path, str]] = []
    checked = 0

    for md_file in markdown_files:
        try:
            content = md_file.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            print(f"[check_doc_links] Skipping non-UTF8 markdown: {md_file}")
            continue

        content_wo_code = FENCED_CODE_PATTERN.sub("", content)
        inline_spans = inline_code_spans(content_wo_code)

        for match in LINK_PATTERN.finditer(content_wo_code):
            if is_inside_span(match.start(), match.end(), inline_spans):
                continue
            link = match.group(1).strip()
            if is_ignored_link(link):
                continue
            checked += 1
            target = normalize_target(md_file, link)
            if not target.exists() and not line_suffix_target_exists(md_file, link):
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
