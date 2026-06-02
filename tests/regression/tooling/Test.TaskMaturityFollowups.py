#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "agents" / "check_task_maturity_followups.py"


def write_task(root: Path, rel_path: str, body: str) -> None:
    path = root / rel_path
    path.parent.mkdir(parents=True)
    path.write_text(body, encoding="utf-8")


def run_checker(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), *args],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def task_body(title: str, maturity: str, context: str = "Owned by graphics renderer.") -> str:
    return "\n".join(
        [
            f"# {title}",
            "",
            "## Context",
            f"- {context}",
            "",
            "## Maturity",
            maturity,
            "",
        ]
    )


class TaskMaturityFollowupTests(unittest.TestCase):
    def test_backend_cpucontracted_closure_without_operational_owner_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                "tasks/backlog/rendering/GRAPHICS-100-fixture.md",
                task_body(
                    "GRAPHICS-100 — Fixture graphics task",
                    "- Target: `CPUContracted` for the renderer pass command seam.",
                ),
            )

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("GRAPHICS-100-fixture.md", result.stdout)
        self.assertIn("Target: `CPUContracted`", result.stdout)
        self.assertIn("Operational owned by <TASK-ID>", result.stdout)

    def test_backend_cpucontracted_closure_with_operational_owner_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                "tasks/backlog/rendering/GRAPHICS-101-fixture.md",
                task_body(
                    "GRAPHICS-101 — Fixture graphics task",
                    "\n".join(
                        [
                            "- Target: `CPUContracted` for the renderer pass command seam.",
                            "- `Operational` owned by `GRAPHICS-102`.",
                        ]
                    ),
                ),
            )

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No maturity follow-up findings", result.stdout)

    def test_backend_cpucontracted_closure_with_no_followup_statement_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                "tasks/backlog/rendering/HARDEN-103-fixture.md",
                task_body(
                    "HARDEN-103 — Fixture RHI hygiene task",
                    "\n".join(
                        [
                            "- Target: `CPUContracted` for a CPU/null module hygiene seam.",
                            "- This endpoint records that no `Operational` follow-up is owed.",
                        ]
                    ),
                ),
            )

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No maturity follow-up findings", result.stdout)

    def test_cpu_only_tooling_cpucontracted_closure_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                "tasks/backlog/architecture/HARDEN-104-fixture.md",
                task_body(
                    "HARDEN-104 — Fixture tooling task",
                    "- Target: `CPUContracted` for task-policy checker coverage.",
                    context="Owned by tooling; no backend dimension exists.",
                ),
            )

            result = run_checker(root, "--strict")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("No maturity follow-up findings", result.stdout)

    def test_warning_mode_reports_findings_without_failing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_task(
                root,
                "tasks/active/GRAPHICS-105-fixture.md",
                task_body(
                    "GRAPHICS-105 — Fixture active graphics task",
                    "- Target: `CPUContracted` for a Vulkan renderer seam.",
                ),
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Findings: 1", result.stdout)
        self.assertIn("WARNING MODE", result.stdout)


if __name__ == "__main__":
    unittest.main()
