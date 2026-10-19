#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "docs" / "check_doc_links.py"


def run_checker(root: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), "--strict"],
        cwd=REPO_ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class CheckDocLinksTests(unittest.TestCase):
    def test_inline_code_label_link_is_validated(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "target.md").write_text("# Target\n", encoding="utf-8")
            (root / "index.md").write_text(
                "See [`TASK-001`](target.md) and [plain](target.md).\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Checked relative links: 2", result.stdout)

    def test_broken_inline_code_label_link_fails_strict_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "index.md").write_text(
                "See [`TASK-404`](missing.md).\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("index.md -> missing.md", result.stdout)
        self.assertIn("STRICT MODE: failing", result.stdout)

    def test_inline_code_snippet_is_not_treated_as_a_link(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "index.md").write_text(
                "Literal code: `[not-a-link](missing.md)`.\n",
                encoding="utf-8",
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Checked relative links: 0", result.stdout)

    def test_git_ignored_markdown_is_not_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / ".gitignore").write_text("evidence/\n", encoding="utf-8")
            (root / "evidence").mkdir()
            (root / "evidence" / "review.md").write_text("[gone](missing.md)\n", encoding="utf-8")
            (root / "index.md").write_text("[self](index.md)\n", encoding="utf-8")

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Checked relative links: 1", result.stdout)

    def test_untracked_markdown_outside_ignore_rules_is_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "draft.md").write_text("[gone](missing.md)\n", encoding="utf-8")

            result = run_checker(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("draft.md -> missing.md", result.stdout)

    def test_nested_markdown_is_scanned_under_glob_pathspec_environment(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "docs").mkdir()
            (root / "docs" / "draft.md").write_text("[gone](missing.md)\n", encoding="utf-8")

            result = run_checker(root, env={**os.environ, "GIT_GLOB_PATHSPECS": "1"})

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("draft.md -> missing.md", result.stdout)

    def test_source_line_suffix_resolves_to_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "src" / "a.cpp").write_text("int a;\n", encoding="utf-8")
            (root / "index.md").write_text(
                "[line](src/a.cpp:12) and [range](src/a.cpp:12-20).\n", encoding="utf-8"
            )

            result = run_checker(root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Checked relative links: 2", result.stdout)

    def test_source_line_suffix_on_missing_file_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "index.md").write_text("[line](src/missing.cpp:12)\n", encoding="utf-8")

            result = run_checker(root)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("index.md -> src/missing.cpp:12", result.stdout)


if __name__ == "__main__":
    unittest.main()
