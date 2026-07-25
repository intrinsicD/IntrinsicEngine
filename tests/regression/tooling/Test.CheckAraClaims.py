#!/usr/bin/env python3
"""Regression tests for tools/agents/check_ara_claims.py.

The validator gates the claim ledger, so a regression in it silently removes the guardrail that
keeps a research/performance/capability statement bound to evidence. Each fixture test builds a
minimal ara/ tree in a temporary root; the canonical test asserts the committed tree is clean.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
VALIDATOR = REPO_ROOT / "tools" / "agents" / "check_ara_claims.py"

CLAIM = """\
## C01: A minimal supported claim
- **Statement**: Something observable happened.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A repeat run disagrees.
- **Proof**: [docs/agent/ara-evidence-policy.md]
- **Dependencies**: []
- **Tags**: fixture
- **From staging**: O01
"""

PAPER = """\
# Fixture Research Artifact

## Layers

- `logic/claims.md` — the claim ledger.
- `trace/` — the research journey.

## Notes

Prose may mention src/ without claiming it is a layer.
"""


def run_validator(root: Path, *, strict: bool = True) -> subprocess.CompletedProcess[str]:
    argv = [sys.executable, str(VALIDATOR), "--root", str(root)]
    if strict:
        argv.append("--strict")
    return subprocess.run(
        argv, cwd=REPO_ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False
    )


def build_fixture(root: Path, *, claim: str = CLAIM, paper: str = PAPER) -> None:
    (root / "ara" / "logic" / "solution").mkdir(parents=True)
    (root / "ara" / "staging").mkdir(parents=True)
    (root / "ara" / "trace" / "sessions").mkdir(parents=True)
    (root / "ara" / "evidence").mkdir(parents=True)
    (root / "docs" / "agent").mkdir(parents=True)

    (root / "ara" / "PAPER.md").write_text(paper, encoding="utf-8")
    (root / "ara" / "logic" / "problem.md").write_text("# Problem\n", encoding="utf-8")
    (root / "ara" / "logic" / "claims.md").write_text(f"# Claims\n\n{claim}", encoding="utf-8")
    (root / "ara" / "logic" / "solution" / "constraints.md").write_text(
        "# Constraints\n\n## K01: A constraint\n", encoding="utf-8"
    )
    (root / "ara" / "logic" / "solution" / "architecture.md").write_text(
        "# Architecture\n\n## A01: A statement\n", encoding="utf-8"
    )
    (root / "ara" / "logic" / "solution" / "heuristics.md").write_text(
        "# Heuristics\n\n## H01: A heuristic\n", encoding="utf-8"
    )
    (root / "ara" / "staging" / "observations.yaml").write_text(
        "observations:\n  - id: O01\n    content: fixture\n", encoding="utf-8"
    )
    (root / "ara" / "trace" / "exploration_tree.yaml").write_text("tree: []\n", encoding="utf-8")
    (root / "ara" / "trace" / "sessions" / "session_index.yaml").write_text(
        "sessions: []\n", encoding="utf-8"
    )
    (root / "ara" / "evidence" / "README.md").write_text("# Evidence\n", encoding="utf-8")
    (root / "docs" / "agent" / "ara-evidence-policy.md").write_text("# Policy\n", encoding="utf-8")
    (root / "AGENTS.md").write_text("See ara/logic/claims.md for the ledger.\n", encoding="utf-8")


class CheckAraClaimsTests(unittest.TestCase):
    def test_canonical_repository_is_clean(self) -> None:
        result = run_validator(REPO_ROOT)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("check_ara_claims: OK", result.stdout)

    def test_wellformed_fixture_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root)
            result = run_validator(root)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_missing_required_field_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("- **Tags**: fixture\n", ""))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("missing required field 'Tags'", result.stdout)

    def test_unknown_status_word_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("**Status**: supported", "**Status**: maybe"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("is not one of", result.stdout)

    def test_unresolved_dependency_namespace_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("**Dependencies**: []", "**Dependencies**: [K99]"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("not crystallized in", result.stdout)

    def test_missing_proof_path_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("docs/agent/ara-evidence-policy.md", "docs/gone.md"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("does not exist", result.stdout)

    def test_disposed_claim_without_artifact_path_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("[docs/agent/ara-evidence-policy.md]", "[N42]"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("cites no repository", result.stdout)

    def test_unknown_staging_id_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("**From staging**: O01", "**From staging**: O99"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("not defined in ara/staging/observations.yaml", result.stdout)

    def test_stale_paper_layer_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, paper=PAPER.replace("`trace/`", "`nonexistent/`"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("does not exist under ara/", result.stdout)

    def test_paper_prose_outside_layers_section_is_not_a_layer_claim(self) -> None:
        """A directory named in prose after the Layers section must not be validated as a layer."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, paper=PAPER.replace("Prose may mention src/", "Prose may mention `src/`"))
            result = run_validator(root)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_agents_md_must_reference_the_ledger(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root)
            (root / "AGENTS.md").write_text("No mention of the ledger here.\n", encoding="utf-8")
            result = run_validator(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("does not mention ara/logic/claims.md", result.stdout)

    def test_warning_mode_reports_but_exits_zero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root, claim=CLAIM.replace("**Status**: supported", "**Status**: maybe"))
            result = run_validator(root, strict=False)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("warning(s)", result.stdout)


if __name__ == "__main__":
    unittest.main()
