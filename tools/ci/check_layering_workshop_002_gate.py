#!/usr/bin/env python3
"""Expected-failure wrapper around ``tools/repo/check_layering.py``.

WORKSHOP-001 made the layering checker module- and CMake-aware, which
surfaces a small, known set of ``graphics_rhi`` / ``graphics`` ->
``platform`` edges that WORKSHOP-002 will remove. Until WORKSHOP-002
lands, the strict layer check against ``src/`` is expected to fail with
exactly that set; this wrapper enforces that:

- the strict check exits non-zero, AND
- every reported violation belongs to the WORKSHOP-002 expected set.

Crucially, the wrapper fails when the checker reports *any* additional
violation beyond the expected set — protecting against regressions that
would otherwise ride along under the looser "contains
``Extrinsic.Platform.Window``" condition.

When the strict check is clean (i.e. WORKSHOP-002 has landed), the
wrapper exits 0 with a notice asking CI to revert to the unguarded
``python3 tools/repo/check_layering.py --root src --strict`` invocation.

This script is intentionally narrow: it only knows about the
WORKSHOP-002 expected set. Once that task retires, delete this file and
the workflow steps that call it (see WORKSHOP-002's Required changes).
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "tools" / "repo" / "check_layering.py"

# (file_path, kind, reference) for each WORKSHOP-002 expected violation.
# Line numbers shift; the file/kind/reference tuple is stable.
EXPECTED_VIOLATIONS: frozenset[tuple[str, str, str]] = frozenset(
    {
        (
            "src/graphics/renderer/Backends/Null/Backends.Null.cpp",
            "import",
            "Extrinsic.Platform.Window",
        ),
        (
            "src/graphics/rhi/CMakeLists.txt",
            "cmake_link",
            "ExtrinsicPlatform",
        ),
        (
            "src/graphics/rhi/RHI.Device.cppm",
            "import",
            "Extrinsic.Platform.Window",
        ),
        (
            "src/graphics/vulkan/Backends.Vulkan.Device.cppm",
            "import",
            "Extrinsic.Platform.Window",
        ),
    }
)

# Matches the per-violation line emitted by check_layering.py:
#   "  - <path>:<line>: [<kind>] <reason> (reference='<ref>')"
VIOLATION_RE = re.compile(
    r"^  - (?P<path>[^:]+):\d+: \[(?P<kind>[a-z_]+)\] .* "
    r"\(reference='(?P<ref>[^']+)'\)\s*$"
)


def parse_violations(stdout: str) -> set[tuple[str, str, str]]:
    """Return the set of (path, kind, reference) tuples reported in ``stdout``."""

    observed: set[tuple[str, str, str]] = set()
    for line in stdout.splitlines():
        match = VIOLATION_RE.match(line)
        if not match:
            continue
        observed.add(
            (match.group("path"), match.group("kind"), match.group("ref"))
        )
    return observed


def evaluate(
    returncode: int, stdout: str
) -> tuple[int, list[str]]:
    """Classify a checker run; return ``(exit_code, notices)``.

    ``notices`` is a list of human-readable lines to print in addition
    to forwarding the raw checker output. The function never reads the
    filesystem and never invokes subprocesses, so it is straightforward
    to unit-test.
    """

    notices: list[str] = []

    if returncode == 0:
        notices.append(
            "::notice::Layering check is clean — restore the unguarded "
            "`python3 tools/repo/check_layering.py --root src --strict` "
            "invocation per WORKSHOP-002's Required changes and delete "
            "tools/ci/check_layering_workshop_002_gate.py."
        )
        return 0, notices

    observed = parse_violations(stdout)
    if not observed:
        notices.append(
            "::error::Layering check failed but reported no parseable "
            "violation lines; investigate (checker output may have changed "
            "format)."
        )
        return 1, notices

    unexpected = observed - EXPECTED_VIOLATIONS
    if unexpected:
        notices.append(
            "::error::Layering check reported violations beyond the "
            "expected WORKSHOP-002 set. Fix the new edge(s) before merging."
        )
        for path, kind, ref in sorted(unexpected):
            notices.append(f"  - {path}: [{kind}] reference={ref!r}")
        return 1, notices

    missing = EXPECTED_VIOLATIONS - observed
    for path, kind, ref in sorted(missing):
        notices.append(
            "::notice::Expected WORKSHOP-002 violation no longer reported: "
            f"{path} [{kind}] reference={ref!r}; consider tightening this "
            "gate's EXPECTED_VIOLATIONS set."
        )

    notices.append(
        "::notice::Layering check failing only on the expected WORKSHOP-002 "
        f"violation set ({len(observed)} reported, 0 unexpected)."
    )
    return 0, notices


def main() -> int:
    result = subprocess.run(
        [sys.executable, str(CHECKER), "--root", "src", "--strict"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    sys.stdout.write(result.stdout)
    if not result.stdout.endswith("\n"):
        sys.stdout.write("\n")

    exit_code, notices = evaluate(result.returncode, result.stdout)
    for line in notices:
        # Notices go to stdout so GitHub Actions surfaces them in the
        # step summary alongside the checker output; errors are also
        # mirrored on stderr so they appear in raw logs even when stdout
        # is buffered.
        print(line)
        if line.startswith("::error::"):
            print(line, file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
