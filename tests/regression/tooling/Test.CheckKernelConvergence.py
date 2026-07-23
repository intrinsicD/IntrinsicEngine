#!/usr/bin/env python3
"""Regression tests for the Runtime.Engine kernel-convergence checker."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tools" / "repo" / "check_kernel_convergence.py"
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "pr-fast.yml"
CI_DOCS_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "ci-docs.yml"

BASE_SOURCE = """\
export module Fixture.Engine;

export import Extrinsic.Runtime.InputActions;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.Domain;

export class Engine;
export class Engine
{
public:
    Domain& GetDomain() const;
    // int GetCommentOnly();
    void Invoke() { GetInlineCall(); }
    const char* Label{"GetStringOnly()"};
private:
    int GetHidden() const;
};
"""


def fixture_policy(*, plain_imports: int = 2) -> dict[str, object]:
    exact_plain_imports = [
        "Extrinsic.Core.Error",
        "Extrinsic.Runtime.Domain",
    ]
    if plain_imports == 3:
        exact_plain_imports.append("Extrinsic.Core.FrameClock")
    return {
        "schema_version": 2,
        "engine_interface": "src/runtime/Runtime.Engine.cppm",
        "substrate_imports": {
            "prefixes": ["Extrinsic.Core."],
            "exact": ["Extrinsic.Runtime.CommandBus"],
        },
        "reference_snapshot": {
            "date": "2026-01-01",
            "metric": "fixture",
            "plain_import_count": plain_imports,
            "domain_import_count": 1,
            "public_getter_count": 1,
            "public_getter_names": ["GetDomain"],
        },
        "current_snapshot": {
            "date": "2026-01-01",
            "metric": "fixture",
            "plain_import_count": plain_imports,
            "plain_imports": exact_plain_imports,
            "domain_import_count": 1,
            "domain_imports": ["Extrinsic.Runtime.Domain"],
            "export_import_count": 1,
            "export_imports": ["Extrinsic.Runtime.InputActions"],
            "public_getter_count": 1,
            "public_getters": [
                {
                    "name": "GetDomain",
                    "return_type": "Domain&",
                    "owning_type": "Domain",
                    "owning_import": "Extrinsic.Runtime.Domain",
                }
            ],
        },
        "temporary_debt": None,
    }


def debt_policy(owner: str) -> dict[str, object]:
    policy = fixture_policy(plain_imports=3)
    reference = policy["reference_snapshot"]
    assert isinstance(reference, dict)
    reference["plain_import_count"] = 2
    policy["temporary_debt"] = {
        "plain_imports": 1,
        "domain_imports": 0,
        "getter_names": [],
        "owner": owner,
    }
    return policy


def write_fixture(
    root: Path,
    *,
    source: str = BASE_SOURCE,
    policy: dict[str, object] | None = None,
) -> None:
    engine = root / "src" / "runtime" / "Runtime.Engine.cppm"
    policy_path = root / "tools" / "repo" / "kernel_convergence_policy.json"
    engine.parent.mkdir(parents=True)
    policy_path.parent.mkdir(parents=True)
    engine.write_text(source, encoding="utf-8")
    policy_path.write_text(
        json.dumps(policy if policy is not None else fixture_policy()),
        encoding="utf-8",
    )


def run_checker(root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), "--strict"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


class KernelConvergenceTests(unittest.TestCase):
    def test_current_repository_snapshot_passes(self) -> None:
        result = run_checker(REPO_ROOT)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn(
            "plain_imports=12 domain_imports=0 export_imports=0 "
            "public_getter_names=5",
            result.stdout,
        )
        self.assertIn("Temporary debt: none", result.stdout)

    def test_clean_synthetic_snapshot_ignores_comments_strings_private_and_inline_calls(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root)
            result = run_checker(root)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("public_getter_names=1", result.stdout)
        self.assertIn("Temporary debt: none", result.stdout)

    def test_zero_debt_rejects_stale_owner_record(self) -> None:
        policy = fixture_policy()
        policy["temporary_debt"] = {
            "plain_imports": 0,
            "domain_imports": 0,
            "getter_names": [],
            "owner": "STALE-001",
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, policy=policy)
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("temporary_debt must be null", result.stdout)

    def test_unknown_domain_import_fails_allowlist_complement(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Runtime.Domain;",
            "import Extrinsic.Runtime.Domain;\nimport Extrinsic.Runtime.NewDomain;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Extrinsic.Runtime.NewDomain", result.stdout)
        self.assertIn("plain import count increased", result.stdout)

    def test_same_line_directives_are_all_counted_and_separated(self) -> None:
        source = BASE_SOURCE.replace(
            "export import Extrinsic.Runtime.InputActions;\n"
            "import Extrinsic.Core.Error;\n"
            "import Extrinsic.Runtime.Domain;",
            "export import Extrinsic.Runtime.InputActions; "
            "import Extrinsic.Core.Error; import Extrinsic.Runtime.Domain;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("plain_imports=2 domain_imports=1 export_imports=1", result.stdout)

    def test_second_same_line_domain_import_cannot_bypass_ratchet(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Runtime.Domain;",
            "import Extrinsic.Runtime.Domain; import Extrinsic.Runtime.NewDomain;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Extrinsic.Runtime.NewDomain", result.stdout)

    def test_header_unit_import_fails_closed(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            'import Extrinsic.Core.Error; import "new.hpp";',
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("unsupported header-unit import", result.stdout)

    def test_attributed_import_fails_closed_instead_of_bypassing_count(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Runtime.Domain;",
            "import Extrinsic.Runtime.Domain [[maybe_unused]];",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("unsupported import declaration", result.stdout)

    def test_allowed_substrate_import_is_not_domain(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            "import Extrinsic.Core.Error;\nimport Extrinsic.Core.FrameClock;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source, policy=fixture_policy(plain_imports=3))
            result = run_checker(root)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("plain_imports=3 domain_imports=1", result.stdout)

    def test_same_count_substrate_import_replacement_fails_exact_set(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            "import Extrinsic.Core.FrameClock;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("unexpected plain imports: Extrinsic.Core.FrameClock", result.stdout)
        self.assertIn("stale plain imports policy entries", result.stdout)

    def test_new_public_getter_fails(self) -> None:
        source = BASE_SOURCE.replace(
            "Domain& GetDomain() const;",
            "Domain& GetDomain() const;\n    Domain& GetNewDomainThing() const;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("GetNewDomainThing", result.stdout)

    def test_public_getter_return_type_change_fails_owning_type_ratchet(self) -> None:
        source = BASE_SOURCE.replace(
            "Domain& GetDomain() const;",
            "const Domain& GetDomain() const;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("GetDomain return type changed", result.stdout)
        self.assertIn("owning type Domain", result.stdout)

    def test_getter_owning_import_must_be_an_exact_plain_import(self) -> None:
        policy = fixture_policy()
        current = policy["current_snapshot"]
        assert isinstance(current, dict)
        getters = current["public_getters"]
        assert isinstance(getters, list)
        getter = getters[0]
        assert isinstance(getter, dict)
        getter["owning_import"] = "Extrinsic.Runtime.MissingOwner"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, policy=policy)
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("owning_import is not an exact plain import", result.stdout)

    def test_removed_domain_import_forces_same_change_policy_ratchet(self) -> None:
        source = BASE_SOURCE.replace("import Extrinsic.Runtime.Domain;\n", "")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("plain import count decreased", result.stdout)
        self.assertIn("stale domain imports policy entries", result.stdout)

    def test_new_export_import_fails(self) -> None:
        source = BASE_SOURCE.replace(
            "export import Extrinsic.Runtime.InputActions;",
            "export import Extrinsic.Runtime.InputActions;\n"
            "export import Extrinsic.Runtime.NewFacade;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source)
            result = run_checker(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Extrinsic.Runtime.NewFacade", result.stdout)

    def test_missing_engine_definition_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source="export module Fixture.Engine;\n")
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("export class Engine definition was not found", result.stdout)

    def test_missing_engine_source_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root)
            (root / "src" / "runtime" / "Runtime.Engine.cppm").unlink()
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("cannot read Engine interface", result.stdout)

    def test_malformed_policy_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root)
            policy_path = root / "tools" / "repo" / "kernel_convergence_policy.json"
            policy_path.write_text("{not-json", encoding="utf-8")
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("cannot read policy", result.stdout)

    def test_temporary_debt_owner_must_resolve_to_open_task(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            "import Extrinsic.Core.Error;\nimport Extrinsic.Core.FrameClock;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source, policy=debt_policy("MISSING-001"))
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("must resolve exactly once", result.stdout)

    def test_temporary_debt_delta_is_machine_validated(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            "import Extrinsic.Core.Error;\nimport Extrinsic.Core.FrameClock;",
        )
        policy = debt_policy("DEBT-001")
        debt = policy["temporary_debt"]
        assert isinstance(debt, dict)
        debt["plain_imports"] = 0
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source, policy=policy)
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("plain_imports does not match snapshot delta", result.stdout)

    def test_retired_temporary_debt_owner_fails_closed(self) -> None:
        source = BASE_SOURCE.replace(
            "import Extrinsic.Core.Error;",
            "import Extrinsic.Core.Error;\nimport Extrinsic.Core.FrameClock;",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_fixture(root, source=source, policy=debt_policy("DEBT-001"))
            done_task = root / "tasks" / "done" / "DEBT-001.md"
            done_task.parent.mkdir(parents=True)
            done_task.write_text("---\nid: DEBT-001\n---\n", encoding="utf-8")
            result = run_checker(root)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("must resolve exactly once", result.stdout)

    def test_ci_keeps_synthetic_guard_and_routes_live_checks(self) -> None:
        pr_fast = WORKFLOW.read_text(encoding="utf-8")
        ci_docs = CI_DOCS_WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(
            ci_docs.count(
                "python3 tests/regression/tooling/Test.CheckKernelConvergence.py"
            ),
            1,
        )
        self.assertEqual(
            pr_fast.count("--action structural"),
            1,
        )
        self.assertNotIn(
            "python3 tests/regression/tooling/Test.CheckKernelConvergence.py",
            pr_fast,
        )
        self.assertNotIn(
            "python3 tools/repo/check_kernel_convergence.py --root . --strict",
            pr_fast,
        )


if __name__ == "__main__":
    unittest.main()
