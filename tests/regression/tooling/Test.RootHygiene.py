#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ROOT_HYGIENE = REPO_ROOT / "tools" / "repo" / "check_root_hygiene.py"
EXPECTED_TOP_LEVEL = REPO_ROOT / "tools" / "repo" / "check_expected_top_level.py"


def run_checker(
    checker: Path,
    root: Path,
    policy: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(checker), "--root", str(root), "--strict"]
    if policy is not None:
        command.extend(("--allowlist", str(policy)))
    return subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def write_policy(path: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "allowed_root_entries:",
                "  - .gitignore",
                "  - README.md",
                "  - ara/",
                "ignored_local_entries:",
                "  - .ruff_cache/",
                "  - imgui.ini",
                "",
            ]
        ),
        encoding="utf-8",
    )


def make_fixture(parent: Path) -> tuple[Path, Path]:
    root = parent / "repo"
    root.mkdir()
    (root / ".gitignore").write_text("", encoding="utf-8")
    (root / "README.md").write_text("# Fixture\n", encoding="utf-8")
    (root / "ara").mkdir()
    (root / ".ruff_cache").mkdir()
    (root / "imgui.ini").write_text("[Window][Fixture]\n", encoding="utf-8")
    policy = parent / "root_allowlist.yaml"
    write_policy(policy)
    return root, policy


class RootHygieneTests(unittest.TestCase):
    def test_repository_root_matches_canonical_policy(self) -> None:
        for checker in (ROOT_HYGIENE, EXPECTED_TOP_LEVEL):
            with self.subTest(checker=checker.name):
                result = run_checker(checker, REPO_ROOT)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_named_local_entries_and_tracked_ara_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))

            for checker in (ROOT_HYGIENE, EXPECTED_TOP_LEVEL):
                with self.subTest(checker=checker.name):
                    result = run_checker(checker, root, policy)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    self.assertIn("ara/", result.stdout)
                    self.assertIn(".ruff_cache/", result.stdout)
                    self.assertIn("imgui.ini", result.stdout)

    def test_canonical_fixture_without_local_state_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))
            (root / ".ruff_cache").rmdir()
            (root / "imgui.ini").unlink()

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 0, result.stdout)

    def test_compatibility_entrypoint_matches_canonical_checker(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))

            canonical = run_checker(ROOT_HYGIENE, root, policy)
            compatibility = run_checker(EXPECTED_TOP_LEVEL, root, policy)

        self.assertEqual(compatibility.returncode, canonical.returncode)
        self.assertEqual(compatibility.stdout, canonical.stdout)

    def test_missing_policy_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, _ = make_fixture(Path(tmp))
            missing = Path(tmp) / "missing-policy.yaml"

            result = run_checker(ROOT_HYGIENE, root, missing)

        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("root policy does not exist", result.stdout)

    def test_empty_policy_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root, policy = make_fixture(parent)
            policy.write_text("allowed_root_entries:\n", encoding="utf-8")

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("no `allowed_root_entries`", result.stdout)

    def test_blanket_ignored_pattern_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root, policy = make_fixture(parent)
            policy.write_text(
                "allowed_root_entries:\n"
                "  - README.md\n"
                "ignored_local_entries:\n"
                "  - '**/'\n",
                encoding="utf-8",
            )

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("ignored local patterns must be exact", result.stdout)

    def test_unknown_policy_section_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root, policy = make_fixture(parent)
            policy.write_text(
                "allowed_root_entries:\n"
                "  - README.md\n"
                "unexpected_policy:\n"
                "  - source/\n",
                encoding="utf-8",
            )

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("unknown policy section", result.stdout)

    def test_unowned_root_markdown_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))
            (root / "PLAN.md").write_text("# Unowned\n", encoding="utf-8")

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("PLAN.md: disallowed", result.stdout)

    def test_unknown_source_directory_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))
            (root / "unowned_source").mkdir()

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("unowned_source/", result.stdout)

    def test_missing_required_root_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root, policy = make_fixture(Path(tmp))
            (root / "ara").rmdir()

            result = run_checker(ROOT_HYGIENE, root, policy)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Missing expected root entries", result.stdout)
        self.assertIn("ara/", result.stdout)

    def test_global_git_ignore_cannot_hide_unknown_source(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root, policy = make_fixture(parent)
            (root / "unowned_source").mkdir()
            subprocess.run(
                ["git", "init", "--quiet", str(root)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            excludes = parent / "global-ignore"
            excludes.write_text("unowned_source/\n", encoding="utf-8")
            global_config = parent / "global.gitconfig"
            global_config.write_text(
                f"[core]\n\texcludesFile = {excludes}\n", encoding="utf-8"
            )
            env = os.environ.copy()
            env["GIT_CONFIG_GLOBAL"] = str(global_config)
            ignored = subprocess.run(
                ["git", "-C", str(root), "check-ignore", "--quiet", "unowned_source"],
                env=env,
                check=False,
            )
            self.assertEqual(ignored.returncode, 0)

            result = run_checker(ROOT_HYGIENE, root, policy, env=env)

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("unowned_source/", result.stdout)


if __name__ == "__main__":
    unittest.main()
