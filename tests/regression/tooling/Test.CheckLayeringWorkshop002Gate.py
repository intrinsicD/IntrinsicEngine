#!/usr/bin/env python3
"""Unit tests for ``tools/ci/check_layering_workshop_002_gate.py``.

These tests exercise the pure ``evaluate(...)`` classifier so a future
change cannot quietly weaken the gate. A separate smoke run drives the
script end-to-end against the live ``src/`` tree.
"""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
GATE_PATH = REPO_ROOT / "tools" / "ci" / "check_layering_workshop_002_gate.py"

spec = importlib.util.spec_from_file_location(
    "check_layering_workshop_002_gate", GATE_PATH
)
assert spec is not None and spec.loader is not None
gate = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = gate
spec.loader.exec_module(gate)


EXPECTED_LINES = [
    "  - src/graphics/renderer/Backends/Null/Backends.Null.cpp:22: [import] graphics cannot depend on platform (reference='Extrinsic.Platform.Window')",
    "  - src/graphics/rhi/CMakeLists.txt:37: [cmake_link] graphics_rhi cannot depend on platform via CMake target_link_libraries(ExtrinsicPlatform) (reference='ExtrinsicPlatform')",
    "  - src/graphics/rhi/RHI.Device.cppm:10: [import] graphics_rhi cannot depend on platform (reference='Extrinsic.Platform.Window')",
    "  - src/graphics/vulkan/Backends.Vulkan.Device.cppm:29: [import] graphics cannot depend on platform (reference='Extrinsic.Platform.Window')",
]


def render(stdout_lines: list[str]) -> str:
    return "\n".join(stdout_lines) + "\n"


class EvaluateTests(unittest.TestCase):
    def test_returncode_zero_passes_with_revert_notice(self) -> None:
        exit_code, notices = gate.evaluate(0, "[check_layering] No layering violations found.\n")
        self.assertEqual(exit_code, 0)
        self.assertTrue(
            any("restore the unguarded" in n for n in notices),
            notices,
        )

    def test_expected_set_only_passes(self) -> None:
        exit_code, notices = gate.evaluate(1, render(EXPECTED_LINES))
        self.assertEqual(exit_code, 0, notices)
        self.assertTrue(
            any("0 unexpected" in n for n in notices),
            notices,
        )
        # No ::error:: line is emitted.
        self.assertFalse(any(n.startswith("::error::") for n in notices), notices)

    def test_extra_violation_fails_even_with_expected_set_present(self) -> None:
        extra = (
            "  - src/runtime/Runtime.Engine.cppm:42: [import] runtime cannot "
            "depend on app (reference='Extrinsic.App.Sandbox')"
        )
        exit_code, notices = gate.evaluate(1, render(EXPECTED_LINES + [extra]))
        self.assertEqual(exit_code, 1, notices)
        self.assertTrue(
            any("beyond the expected WORKSHOP-002 set" in n for n in notices),
            notices,
        )
        self.assertTrue(
            any("src/runtime/Runtime.Engine.cppm" in n for n in notices),
            notices,
        )

    def test_extra_violation_alone_fails(self) -> None:
        extra = (
            "  - src/ecs/Events/ECS.Events.cppm:5: [import] ecs cannot depend "
            "on graphics (reference='Extrinsic.Graphics.Pass.Surface')"
        )
        exit_code, notices = gate.evaluate(1, render([extra]))
        self.assertEqual(exit_code, 1, notices)
        self.assertTrue(
            any("beyond the expected WORKSHOP-002 set" in n for n in notices),
            notices,
        )

    def test_partial_expected_set_still_passes_with_missing_notice(self) -> None:
        # WORKSHOP-002 fixes one violation but the rest are still present.
        partial = [line for line in EXPECTED_LINES if "RHI.Device.cppm" not in line]
        exit_code, notices = gate.evaluate(1, render(partial))
        self.assertEqual(exit_code, 0, notices)
        self.assertTrue(
            any("no longer reported" in n and "RHI.Device.cppm" in n for n in notices),
            notices,
        )

    def test_failure_with_unparseable_output_fails(self) -> None:
        exit_code, notices = gate.evaluate(
            1, "[check_layering] STRICT MODE: failing.\nUnexpected garbage line\n"
        )
        self.assertEqual(exit_code, 1)
        self.assertTrue(
            any("no parseable violation lines" in n for n in notices),
            notices,
        )

    def test_line_number_drift_does_not_break_matching(self) -> None:
        shifted = [
            line.replace(":22:", ":42:").replace(":37:", ":99:").replace(":10:", ":500:")
            for line in EXPECTED_LINES
        ]
        exit_code, notices = gate.evaluate(1, render(shifted))
        self.assertEqual(exit_code, 0, notices)

    def test_extra_violation_at_known_path_with_different_reference_fails(self) -> None:
        # Same file path as an expected entry, but a different module
        # reference (e.g. a freshly added import on the doomed file).
        extra = (
            "  - src/graphics/rhi/RHI.Device.cppm:55: [import] graphics_rhi "
            "cannot depend on runtime (reference='Extrinsic.Runtime.Engine')"
        )
        exit_code, notices = gate.evaluate(1, render(EXPECTED_LINES + [extra]))
        self.assertEqual(exit_code, 1, notices)
        self.assertTrue(
            any(
                "Extrinsic.Runtime.Engine" in n and "src/graphics/rhi/RHI.Device.cppm" in n
                for n in notices
            ),
            notices,
        )


class ParseViolationsTests(unittest.TestCase):
    def test_ignores_non_violation_lines(self) -> None:
        stdout = (
            "[check_layering] Scan root: /repo/src\n"
            "[check_layering] Files scanned: 771\n"
            "[check_layering] Layering violations:\n"
            + "\n".join(EXPECTED_LINES)
            + "\n[check_layering] STRICT MODE: failing.\n"
        )
        observed = gate.parse_violations(stdout)
        self.assertEqual(observed, set(gate.EXPECTED_VIOLATIONS))


class GateScriptSmoke(unittest.TestCase):
    """End-to-end smoke: run the script against the live src/ tree.

    Passes whenever the live checker's violation set is a subset of
    EXPECTED_VIOLATIONS (the WORKSHOP-002 transition window) and also
    whenever the checker is clean (post-WORKSHOP-002).
    """

    def test_gate_script_against_live_src_tree(self) -> None:
        result = subprocess.run(
            [sys.executable, str(GATE_PATH)],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
