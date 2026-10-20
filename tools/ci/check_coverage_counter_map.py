#!/usr/bin/env python3
"""Fail when built production code counts an expression with its entry counter.

Clang's PGO counter map misses expressions that exist only in Sema's semantic
form (BUG-242); codegen then increments counter 0, the function-entry counter,
for them, so the function's counts are silently wrong until one goes negative.
This re-emits frontend IR for every instrumented production TU of a built
coverage tree and reports each extra counter-0 increment by source location.
Defaulted comparisons are skipped: the collector already omits their records.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Sequence

from source_coverage import (
    PRODUCTION_SUFFIXES,
    CoverageError,
    _command_arguments,
    _is_defaulted_declaration,
    _normalize_repo_path,
)

_INCREMENT = re.compile(
    r"call void @llvm\.instrprof\.increment(?:\.step)?\(ptr @(__profn_[^,]+), "
    r"i64 -?\d+, i32 \d+, i32 0[,)]"
)
_LOCATION = re.compile(r"^!(\d+) = !DILocation\(line: (\d+), (?:column: (\d+), )?scope: !(\d+)")
_SCOPE_FILE = re.compile(r"^!(\d+) = distinct !DI(?:Subprogram|LexicalBlock)\(.*?file: !(\d+)")
_FILE = re.compile(r'^!(\d+) = !DIFile\(filename: "([^"]+)", directory: "([^"]*)"')
_DBG = re.compile(r"!dbg !(\d+)")
_DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
_DROPPED = frozenset({"-c", "-MD", "-MMD"})


def _ir_command(record: dict, module_output: Path) -> list[str]:
    directory = Path(record["directory"])
    expanded: list[str] = []
    for argument in _command_arguments(record):
        # CMake's .modmap response files carry -fmodule-output, which must not
        # overwrite the build tree's BMI.
        if argument.startswith("@"):
            expanded += shlex.split((directory / argument[1:]).read_text(encoding="utf-8"))
        else:
            expanded.append(argument)
    command: list[str] = []
    skip = False
    for argument in expanded:
        if skip:
            skip = False
        elif argument in _DROPPED_WITH_VALUE:
            skip = True
        elif argument not in _DROPPED and not argument.startswith("-fmodule-output"):
            command.append(argument)
    if "c++-module" in command:
        command.append(f"-fmodule-output={module_output}")
    return command + ["-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes", "-o", "-"]


def _scan(record: dict, repo_root: Path) -> tuple[set[str], str]:
    with tempfile.TemporaryDirectory() as scratch:
        result = subprocess.run(
            _ir_command(record, Path(scratch) / "unit.pcm"),
            cwd=record["directory"],
            capture_output=True,
            text=True,
        )
    if result.returncode != 0:
        return set(), result.stderr.strip()[-2000:] or f"exit {result.returncode}"
    lines = result.stdout.splitlines()
    increments: dict[str, list[int]] = {}
    locations: dict[str, tuple[int, int, str]] = {}
    scopes: dict[str, str] = {}
    files: dict[str, Path] = {}
    for index, line in enumerate(lines):
        if match := _INCREMENT.search(line):
            increments.setdefault(match.group(1), []).append(index)
        elif match := _LOCATION.match(line):
            locations[match.group(1)] = (
                int(match.group(2)),
                int(match.group(3) or 0),
                match.group(4),
            )
        elif match := _SCOPE_FILE.match(line):
            scopes[match.group(1)] = match.group(2)
        elif match := _FILE.match(line):
            files[match.group(1)] = Path(match.group(3)) / match.group(2)
    findings: set[str] = set()
    for function, indices in increments.items():
        for start in indices[1:]:
            # The increment carries line 0; the expression it counts follows.
            source, line_number, column = None, 0, 0
            for following in lines[start + 1 : start + 40]:
                debug = _DBG.search(following)
                if debug and locations.get(debug.group(1), (0,))[0]:
                    line_number, column, scope = locations[debug.group(1)]
                    source = files.get(scopes.get(scope, ""))
                    break
            if source is None:
                findings.add(f"?:0:0: {function} (no source location)")
                continue
            normalized = _normalize_repo_path(source, repo_root)
            if normalized is None:
                continue  # not production: outside the measured coverage
            text = (repo_root / normalized).read_text(encoding="utf-8", errors="replace")
            if not _is_defaulted_declaration(text.splitlines(), line_number, column):
                findings.add(f"{normalized}:{line_number}:{column}: {function}")
    return findings, ""


def check(build_dir: Path, repo_root: Path, jobs: int) -> int:
    path = build_dir / "compile_commands.json"
    try:
        records = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(f"cannot read compilation database {path}: {error}")
    selected: list[tuple[str, dict]] = []
    for record in records:
        source = _normalize_repo_path(record["file"], repo_root)
        arguments = _command_arguments(record)
        if (
            source is None
            or Path(source).suffix.lower() not in PRODUCTION_SUFFIXES
            or "-fprofile-instr-generate" not in arguments
            or "-o" not in arguments
        ):
            continue
        # Only TUs the coverage cohort actually built carry counters.
        if (Path(record["directory"]) / arguments[arguments.index("-o") + 1]).is_file():
            selected.append((source, record))
    if not selected:
        raise CoverageError(f"{path}: no built instrumented production translation unit")
    findings: set[str] = set()
    failures = 0
    with ThreadPoolExecutor(jobs) as pool:
        scans = pool.map(lambda item: _scan(item[1], repo_root), selected)
        for (source, _), (unit_findings, error) in zip(selected, scans):
            if error:
                failures += 1
                print(f"{source}: cannot emit IR: {error.splitlines()[-1]}")
            findings |= unit_findings
    for finding in sorted(findings):
        print(f"{finding} increments its entry counter again")
    if findings:
        print(
            "a ?: with lvalue operands, one a named constant, written directly in a "
            "braced initializer has no PGO counter; bind it to a local first "
            "(tools/ci/README.md)"
        )
    print(
        f"scanned {len(selected)} production translation units: "
        f"{len(findings)} findings, {failures} failures"
    )
    return 1 if findings or failures else 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--jobs", type=int, default=max(1, min(4, os.cpu_count() or 1)))
    arguments = parser.parse_args(argv)
    try:
        return check(arguments.build_dir.resolve(), arguments.repo_root.resolve(), arguments.jobs)
    except (CoverageError, OSError) as error:
        print(f"error: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
