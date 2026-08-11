#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
AUDITOR = (
    REPO_ROOT
    / "tools"
    / "agents"
    / "skills"
    / "intrinsicengine-source-documentation"
    / "scripts"
    / "audit_source_documentation.py"
)
CI_DOCS = REPO_ROOT / ".github" / "workflows" / "ci-docs.yml"


def run_audit(
    root: Path, *extra: str, path: str | None = None
) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(AUDITOR), "--root", str(root)]
    if path is not None:
        command.extend(("--path", path))
    command.extend(extra)
    return subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


class SourceDocumentationAuditTests(unittest.TestCase):
    def test_compliant_module_and_header_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(
                root / "src" / "Example.cppm",
                "// Exposes stable example values so callers share one contract.\n"
                "export module Example;\n",
            )
            write(
                root / "src" / "Example.hpp",
                "// SPDX-License-Identifier: MIT\n\n"
                "// Declares the example handle used across the fixture boundary.\n"
                "#pragma once\n",
            )
            write(
                root / "src" / "Direct.hpp",
                "// Defines direct fixture state so consumers need no wrapper.\n"
                "struct Direct {};\n",
            )

            result = run_audit(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("errors=0", result.stdout)
        self.assertIn("review=0", result.stdout)

    def test_missing_synopsis_is_an_objective_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root / "src" / "Missing.hpp", "#pragma once\n")

            result = run_audit(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[file-synopsis-missing]", result.stdout)
        self.assertIn("src/Missing.hpp:1", result.stdout)

    def test_report_only_preserves_errors_but_returns_success(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root / "src" / "Missing.cppm", "export module Missing;\n")

            result = run_audit(root, "--no-fail")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("errors=1", result.stdout)
        self.assertIn("[file-synopsis-missing]", result.stdout)

    def test_long_synopsis_is_review_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(
                root / "src" / "Long.hpp",
                "// Exposes the first fixture concept.\n"
                "// Keeps the second fixture concept aligned.\n"
                "// Separates the third fixture concept.\n"
                "// Provides a fourth line that should be shortened.\n"
                "#pragma once\n",
            )

            result = run_audit(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("[file-synopsis-long]", result.stdout)
        self.assertIn("review=1", result.stdout)

    def test_declaration_and_historical_comments_require_review(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(
                root / "src" / "Comments.hpp",
                "// Exposes fixture state so tests can inspect comment policy.\n"
                "#pragma once\n\n"
                "// Stores fixture state.\n"
                "struct State {};\n",
            )
            write(
                root / "src" / "Comments.cpp",
                "// Previously this used a global, which was retired in PROC-100.\n"
                "int Value() { return 1; }\n",
            )

            result = run_audit(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("[declaration-comment]", result.stdout)
        self.assertIn("[comment-history]", result.stdout)

    def test_readme_history_heading_is_error_and_chronology_is_review(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(
                root / "src" / "README.md",
                "# Fixture\n\n## Architecture history\n\n"
                "Previously this was owned by PROC-100.\n",
            )

            result = run_audit(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[readme-non-current-section]", result.stdout)
        self.assertIn("[readme-chronology]", result.stdout)

    def test_json_output_is_stable_and_path_scope_is_honored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root / "src" / "B.hpp", "#pragma once\n")
            write(root / "ignored" / "A.hpp", "#pragma once\n")

            first = run_audit(root, "--format", "json", "--no-fail", path="src")
            second = run_audit(root, "--format", "json", "--no-fail", path="src")

        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual(first.stdout, second.stdout)
        payload = json.loads(first.stdout)
        self.assertEqual(payload["summary"]["interfaces"], 1)
        self.assertEqual(payload["findings"][0]["path"], "src/B.hpp")

    def test_summary_prioritizes_paths_without_printing_each_finding(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root / "src" / "B.hpp", "#pragma once\n")
            write(root / "src" / "A.hpp", "#pragma once\n")

            result = run_audit(root, "--summary", "--no-fail", "--top", "1")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("HOTSPOT src/A.hpp findings=1", result.stdout)
        self.assertNotIn("ERROR src/A.hpp", result.stdout)
        self.assertNotIn("HOTSPOT src/B.hpp", result.stdout)

    def test_ci_runs_tool_regressions_without_blocking_on_existing_debt(self) -> None:
        workflow = CI_DOCS.read_text(encoding="utf-8")
        self.assertIn(
            "python3 tests/regression/tooling/Test.SourceDocumentationAudit.py",
            workflow,
        )
        self.assertNotIn(
            "audit_source_documentation.py --root .",
            workflow,
        )


if __name__ == "__main__":
    unittest.main()
