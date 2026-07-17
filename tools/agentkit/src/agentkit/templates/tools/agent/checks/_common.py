"""Shared helpers for agentkit's config-driven checks.

Standard library only, so the checks run anywhere a modern Python does — CI,
agent sandboxes, locked-down hooks. Every check reads ``agentkit.toml`` from
``--root`` and follows the shared conventions:

  * args:        ``--root`` (default ".") and ``--strict``
  * exit codes:  0 pass/warn-only · 1 strict failure · 2 usage/env
  * reporting:   ``[<tool>] LEVEL: message`` lines + a final summary
"""
from __future__ import annotations

import argparse
import fnmatch
import tomllib
from pathlib import Path
from typing import Any, Iterator

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2

CONFIG_FILENAME = "agentkit.toml"

_FALLBACK_IGNORE = [
    ".git",
    "build",
    "build-*",
    "cmake-build-*",
    "dist",
    "out",
    "node_modules",
    ".venv",
    "venv",
    "__pycache__",
    ".mypy_cache",
    ".pytest_cache",
    "target",
    "vendor",
    "third_party",
    "external",
]


def load_config(root: Path) -> dict[str, Any]:
    path = Path(root) / CONFIG_FILENAME
    if not path.is_file():
        return {}
    with path.open("rb") as handle:
        return tomllib.load(handle)


def cfg_get(cfg: dict[str, Any], dotted: str, default: Any = None) -> Any:
    node: Any = cfg
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return default
        node = node[part]
    return node


def ignore_globs(cfg: dict[str, Any]) -> list[str]:
    return cfg_get(cfg, "hygiene.ignore_globs", None) or list(_FALLBACK_IGNORE)


def is_ignored(rel: Path, globs: list[str]) -> bool:
    return any(fnmatch.fnmatch(part, pattern) for part in rel.parts for pattern in globs)


def iter_files(root: Path, suffix: str, cfg: dict[str, Any]) -> Iterator[Path]:
    """Yield files under *root* with *suffix*, skipping ignored directories."""
    globs = ignore_globs(cfg)
    root = Path(root)
    for path in sorted(root.rglob(f"*{suffix}")):
        rel = path.relative_to(root)
        if is_ignored(rel, globs):
            continue
        if path.is_file():
            yield path


def base_parser(description: str, *, default_root: str = ".") -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--root", type=Path, default=Path(default_root), help="repository root")
    parser.add_argument("--strict", action="store_true", help="fail (exit 1) on findings")
    return parser


class Reporter:
    """Accumulates findings and prints a uniform summary."""

    def __init__(self, tool: str, strict: bool) -> None:
        self.tool = tool
        self.strict = strict
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)

    def finish(self, ok_message: str) -> int:
        for warning in self.warnings:
            print(f"[{self.tool}] WARNING: {warning}")
        for error in self.errors:
            print(f"[{self.tool}] ERROR: {error}")
        findings = len(self.errors)
        if findings:
            mode = "strict" if self.strict else "warning"
            print(f"[{self.tool}] {findings} finding(s); mode={mode}.")
            if self.strict:
                print(f"[{self.tool}] STRICT MODE: failing.")
                return EXIT_FAIL
            print(f"[{self.tool}] WARNING MODE: non-fatal.")
            return EXIT_OK
        suffix = f" ({len(self.warnings)} warning(s))" if self.warnings else ""
        print(f"[{self.tool}] {ok_message}{suffix}")
        return EXIT_OK


def split_sections(markdown: str) -> dict[str, str]:
    """Map ``## Heading`` -> body text (until the next ``## `` or EOF)."""
    sections: dict[str, str] = {}
    current: str | None = None
    buffer: list[str] = []
    for line in markdown.splitlines():
        if line.startswith("## "):
            if current is not None:
                sections[current] = "\n".join(buffer)
            current = line[3:].strip()
            buffer = []
        elif current is not None:
            buffer.append(line)
    if current is not None:
        sections[current] = "\n".join(buffer)
    return sections
