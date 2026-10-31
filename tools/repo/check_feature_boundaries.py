#!/usr/bin/env python3
"""Ratchet the feature boundaries of the architecture target (ARCH-022, rules R1 and R2).

R1: files under src/runtime and src/app, outside plugin folders, that name two or more of the
operations in the `run_operation` tool's `operation` enum (tests/data/agent_declared_tool_schemas.json).
A name matches as its snake_case form not adjacent to [A-Za-z0-9], or as its PascalCase form not
preceded by an uppercase letter and not followed by a lowercase one (so it is found inside
identifiers such as `ApplyEditorKernelDensityConfig`). Comments and strings count: they name it too.

R2: per plugin folder (every directory under src/runtime/Modules/ and plugins/, keyed by its path),
the distinct modules it imports from the editor (interface unit under src/runtime/Editor/) and from
other plugin folders. Module declarations and imports are read with comments and string literals
removed.

The ceilings in tools/repo/feature_boundaries_baseline.json may only fall. A measured value above
or below its ceiling fails (a fall lowers the ceiling in the same change); a folder without an entry
or an entry without a folder fails; and a ceiling above its lowest value in the baseline file's Git
history fails (a key absent from a version counts as 0 there, so a new plugin starts at 0). The
history, not one comparison revision, is the reference: a raise that landed in an earlier commit,
even a failed push, cannot become the new baseline. Docs: docs/architecture/engine-and-feature-modules.md.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


BASELINE = Path("tools/repo/feature_boundaries_baseline.json")
SCHEMA = Path("tests/data/agent_declared_tool_schemas.json")
R1_ROOTS = ("src/runtime", "src/app")
SUFFIXES = (".cpp", ".cppm", ".hpp", ".h")
PLUGIN_ROOTS = ("src/runtime/Modules", "plugins")
# The ARCH-022 plugin-host spike (built only with INTRINSIC_PLUGIN_SPIKE) measures the plugin boundary instead of
# obeying it; its R2 count is a spike finding, not a ratcheted plugin. Delete this entry with the spike.
NOT_PLUGINS = frozenset({"plugins/spike"})
EDITOR_ROOT = "src/runtime/Editor/"
NAME_RE = re.compile(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*")
RAW_START_RE = re.compile(r'(?<![A-Za-z0-9_])(?:u8|u|U|L)?R"([^()\\\s]{0,16})\(')
# A module name is dot-separated identifiers; C++ allows whitespace (and so removed comments) between tokens.
NAME = r"(\w+(?:\s*\.\s*\w+)*)(?:\s*:\s*\w+(?:\s*\.\s*\w+)*)?"
MODULE_RE = re.compile(rf"(?<![\w.:])export\s+module\s+{NAME}\s*;")
IMPORT_RE = re.compile(rf"(?<![\w.:])import\s+{NAME}\s*;")


class CheckError(RuntimeError):
    """The inputs are malformed or the baseline history is unavailable."""


def code_only(text: str) -> str:
    """The text with comments and literals blanked, in C++'s order.

    A raw string is recognized on the original text (splicing is reverted inside it). Outside raw
    strings, backslash-newline is spliced away first, so a continued `//` comment continues and
    tokens split by it join.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("\\\n", i):
            i += 2
            continue
        raw = RAW_START_RE.match(text, i)
        if raw:
            end = text.find(")" + raw.group(1) + '"', raw.end())
            i = n if end < 0 else end + len(raw.group(1)) + 2
            out.append(" ")
        elif text[i] in "\"'":
            quote, i = text[i], i + 1
            while i < n and text[i] != quote and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
            out.append(" ")
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 2 if text.startswith("\\\n", i) else 1
            out.append(" ")
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def operation_names(root: Path) -> list[str]:
    try:
        names = json.loads((root / SCHEMA).read_text())["run_operation"]["properties"]["operation"]["enum"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CheckError(f"the run_operation operation enum is unreadable in {SCHEMA}: {error}") from error
    if (not isinstance(names, list) or not names or len(set(map(str, names))) != len(names)
            or not all(isinstance(n, str) and NAME_RE.fullmatch(n) for n in names)):
        raise CheckError(f"the run_operation operation enum in {SCHEMA} must be a non-empty list of unique "
                         "snake_case names")
    return names


def name_matchers(names: list[str]) -> list[tuple[str, str, re.Pattern[str]]]:
    matchers = []
    for name in names:
        pascal = "".join(word.capitalize() for word in name.split("_"))
        pattern = re.compile(rf"(?<![A-Za-z0-9]){re.escape(name)}(?![A-Za-z0-9])|(?<![A-Z]){pascal}(?![a-z])")
        matchers.append((name, pascal, pattern))
    return matchers


def plugin_folders(root: Path) -> list[str]:
    return sorted(rel for top in PLUGIN_ROOTS if (root / top).is_dir() for d in (root / top).iterdir()
                  if d.is_dir() and not d.name.startswith(".") and (rel := d.relative_to(root).as_posix()) not in NOT_PLUGINS)


def folder_of(rel: str, folders: list[str]) -> str | None:
    return next((f for f in folders if rel.startswith(f + "/")), None)


def source_files(root: Path, top: str) -> list[Path]:
    base = root / top
    if not base.is_dir():
        return []
    return sorted(p for p in base.rglob("*") if p.is_file() and (p.suffix in SUFFIXES or p.name == "CMakeLists.txt"))


def measure_r1(root: Path, matchers: list[tuple[str, str, re.Pattern[str]]], folders: list[str]) -> list[str]:
    hubs = []
    for top in R1_ROOTS:
        for path in source_files(root, top):
            rel = path.relative_to(root).as_posix()
            if folder_of(rel, folders) is not None:
                continue
            text = path.read_text(errors="replace")
            # The literal test first: a lookbehind at the start of a pattern makes `re` try every position.
            named = sum(1 for snake, pascal, pattern in matchers
                        if (snake in text or pascal in text) and pattern.search(text))
            if named >= 2:
                hubs.append(rel)
    return hubs


def measure_r2(root: Path, folders: list[str]) -> dict[str, dict[str, int]]:
    owner: dict[str, str] = {}  # module -> "editor" or its plugin folder
    imports: dict[str, set[str]] = {folder: set() for folder in folders}
    for top in ("src", "plugins"):
        for path in source_files(root, top):
            rel = path.relative_to(root).as_posix()
            folder = folder_of(rel, folders)
            if folder is None and not rel.startswith(EDITOR_ROOT):
                continue  # only editor and plugin modules have an owner that counts; only plugin imports count
            code = code_only(path.read_text(errors="replace"))
            for module in (re.sub(r"\s+", "", m) for m in MODULE_RE.findall(code)):
                if rel.startswith(EDITOR_ROOT):
                    owner[module] = "editor"
                elif folder is not None:
                    owner[module] = folder
            if folder is not None:
                imports[folder].update(re.sub(r"\s+", "", m) for m in IMPORT_RE.findall(code))
    return {folder: {"editor_internals": sum(1 for m in imported if owner.get(m) == "editor"),
                     "other_plugins": sum(1 for m in imported if owner.get(m) not in (None, "editor", folder))}
            for folder, imported in imports.items()}


def parse_baseline(text: str, where: str) -> dict:
    try:
        data = json.loads(text)
        if data.get("schema_version") != 1 or not isinstance(data.get("r1_files"), int):
            raise ValueError("schema_version 1 and an integer r1_files are required")
        folders = data.get("r2")
        if not isinstance(folders, dict) or not all(
                isinstance(v, dict) and set(v) == {"editor_internals", "other_plugins"}
                and all(isinstance(n, int) and n >= 0 for n in v.values()) for v in folders.values()):
            raise ValueError("r2 must map folders to editor_internals and other_plugins counts")
    except (ValueError, AttributeError) as error:
        raise CheckError(f"malformed baseline {where}: {error}") from error
    return data


def baseline_history(root: Path) -> list[dict]:
    def git(*args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)

    if git("rev-parse", "--is-inside-work-tree").returncode != 0:
        raise CheckError("the baseline history is unavailable: not a Git work tree")
    if git("rev-parse", "--is-shallow-repository").stdout.strip() != "false":
        raise CheckError("the baseline history is unavailable: shallow clone (fetch the full history)")
    # Full history: default simplification follows one parent at a merge and can hide a lowering.
    log = git("log", "--full-history", "--format=%H", "--", BASELINE.as_posix())
    if log.returncode != 0:
        raise CheckError(f"the baseline history is unavailable: {log.stderr.strip()}")
    versions = []
    for commit in log.stdout.split():
        listed = git("ls-tree", commit, "--", BASELINE.as_posix())
        if listed.returncode != 0:
            raise CheckError(f"the baseline history is unavailable at {commit[:12]}: {listed.stderr.strip()}")
        if not listed.stdout.strip():
            continue  # the commit deletes the file
        shown = git("show", f"{commit}:{BASELINE.as_posix()}")
        if shown.returncode != 0:
            raise CheckError(f"the baseline history is unreadable at {commit[:12]}: {shown.stderr.strip()}")
        versions.append(parse_baseline(shown.stdout, f"at {commit[:12]}"))
    return versions


def check(root: Path) -> tuple[list[str], list[str]]:
    folders = plugin_folders(root)
    hubs = measure_r1(root, name_matchers(operation_names(root)), folders)
    r2 = measure_r2(root, folders)
    try:
        baseline = parse_baseline((root / BASELINE).read_text(), str(BASELINE))
    except OSError as error:
        raise CheckError(f"missing baseline {BASELINE}: {error}") from error
    history = baseline_history(root)

    report = [f"R1 files: {len(hubs)} (ceiling {baseline['r1_files']})"] + [f"  {hub}" for hub in hubs]
    findings = []

    def compare(label: str, measured: int, ceiling: int) -> None:
        if measured > ceiling:
            findings.append(f"{label}: {measured} exceeds its ceiling {ceiling}")
        elif measured < ceiling:
            findings.append(f"{label}: {measured} is below its ceiling {ceiling}; lower the ceiling to {measured}")

    compare("R1 files", len(hubs), baseline["r1_files"])
    for folder, counts in r2.items():
        report.append(f"R2 {folder}: editor_internals {counts['editor_internals']}, "
                      f"other_plugins {counts['other_plugins']}")
        ceiling = baseline["r2"].get(folder)
        if ceiling is None:
            findings.append(f"R2 {folder}: plugin folder without a baseline entry (a new plugin starts at 0)")
            continue
        for key in ("editor_internals", "other_plugins"):
            compare(f"R2 {folder}: {key}", counts[key], ceiling[key])
    for folder in sorted(set(baseline["r2"]) - set(r2)):
        findings.append(f"R2 {folder}: stale baseline entry, the folder is gone")

    if history:
        lowest = min(version["r1_files"] for version in history)
        if baseline["r1_files"] > lowest:
            findings.append(f"R1 files: ceiling raised to {baseline['r1_files']} above its lowest {lowest}")
        for folder, ceiling in baseline["r2"].items():
            for key in ("editor_internals", "other_plugins"):
                lowest = min(version["r2"].get(folder, {}).get(key, 0) for version in history)
                if ceiling[key] > lowest:
                    findings.append(f"R2 {folder}: {key} ceiling raised to {ceiling[key]} above its lowest {lowest}")
    return report, findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--strict", action="store_true", help="fail on findings")
    args = parser.parse_args()
    try:
        report, findings = check(args.root.resolve())
    except CheckError as error:
        print(f"[check_feature_boundaries] error: {error}")
        return 1
    print("\n".join(f"[check_feature_boundaries] {line}" for line in report))
    for finding in findings:
        print(f"[check_feature_boundaries] {'ERROR' if args.strict else 'WARNING'}: {finding}")
    if not findings:
        print("[check_feature_boundaries] OK")
    return 1 if findings and args.strict else 0


if __name__ == "__main__":
    sys.exit(main())
