#!/usr/bin/env python3
"""Reject source patterns that known supported compilers miscompile.

Hazard ``ref-default-named-constant`` (BUG-223): Clang 20.1 (the hosted CI
compiler) constant-folds every integral member read through a ``const T&``
parameter whose default argument names a constant, e.g.
``Result Extract(..., const Params& params = kDefaults)``: inside the function
``params.Algorithm`` evaluates to ``kDefaults.Algorithm`` whatever the caller
passed. ``= {}`` / ``= T{}`` defaults are unaffected. Use an overload that
forwards the constant instead.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".cppm", ".cpp", ".hpp", ".h", ".ixx", ".inl"}
# `const Type& name = expr` followed by `,` or `)`: a parameter default argument.
REF_DEFAULT = re.compile(
    r"const\s+[A-Za-z_][\w:<>,\s]*?&\s*[A-Za-z_]\w*\s*=\s*([^,(){};]+?)\s*[,)]")
NAMED = re.compile(r"^[A-Za-z_][\w:]*$")
SAFE = {"nullptr", "true", "false"}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan_text(text: str) -> list[tuple[int, str]]:
    findings = []
    clean = strip_comments(text)
    for match in REF_DEFAULT.finditer(clean):
        expr = match.group(1).strip()
        if NAMED.match(expr) and expr not in SAFE:
            line = clean.count("\n", 0, match.start()) + 1
            findings.append((line, " ".join(match.group(0).split())))
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", action="append", default=None,
                        help="directory to scan (repeatable; default: src and methods)")
    args = parser.parse_args(argv)
    roots = [Path(r) for r in (args.root or ["src", "methods"])]
    findings = []
    for root in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                for line, snippet in scan_text(path.read_text(encoding="utf-8", errors="replace")):
                    findings.append(f"{path}:{line}: ref-default-named-constant: {snippet}")
    for finding in findings:
        print(finding)
    if findings:
        print(f"[check_compiler_hazards] {len(findings)} finding(s). A const-reference parameter must not "
              "default to a named constant (Clang 20 folds its integral members; BUG-223). "
              "Add a forwarding overload instead.", file=sys.stderr)
        return 1
    print("[check_compiler_hazards] No compiler hazards found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
