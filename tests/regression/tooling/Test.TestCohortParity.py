#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "tools" / "ci" / "test_cohort_parity.py"


def _case(
    name: str,
    labels: tuple[str, ...],
    statuses: tuple[str, ...] = (
        "passed",
        "passed",
        "passed",
        "passed",
        "passed",
    ),
) -> dict[str, object]:
    durations = list(range(10, 10 + len(statuses)))
    return {
        "disabled": False,
        "duration_us": {
            "median": durations[len(durations) // 2],
            "p95": durations[-1],
            "samples": durations,
        },
        "executable": f"{name.split('.', 1)[0]}Tests",
        "labels": list(labels),
        "name": name,
        "statuses": list(statuses),
    }


def _report(
    cohort: str,
    cases: list[dict[str, object]],
    *,
    sha: str,
) -> dict[str, object]:
    identities = {
        "pr-fast": {
            "aggregate": "IntrinsicPrFastTests",
            "selector": {
                "exclude_any": [
                    "flaky-quarantine",
                    "gpu",
                    "slow",
                    "vulkan",
                ],
                "include_any": ["contract", "unit"],
            },
            "timeout_seconds": 60,
        },
        "cpu": {
            "aggregate": "IntrinsicCpuTests",
            "selector": {
                "exclude_any": [
                    "flaky-quarantine",
                    "gpu",
                    "slow",
                    "vulkan",
                ],
                "include_any": [],
            },
            "timeout_seconds": 60,
        },
        "cpu-slow": {
            "aggregate": "IntrinsicCpuSlowTests",
            "selector": {
                "exclude_any": [
                    "benchmark",
                    "flaky-quarantine",
                    "gpu",
                    "slo",
                    "vulkan",
                ],
                "include_any": ["slow"],
            },
            "timeout_seconds": 120,
        },
    }
    sample_count = len(cases[0]["statuses"])
    load = {
        "available": True,
        "load_1m": 1.0,
        "load_5m": 1.0,
        "load_15m": 1.0,
    }
    return {
        "cases": cases,
        "host": {
            "github": {
                "github_repository": "intrinsicD/IntrinsicEngine",
                "github_sha": sha,
                "imageos": "ubuntu24",
                "imageversion": "20260714.240.1",
                "runner_arch": "X64",
                "runner_environment": "github-hosted",
                "runner_os": "Linux",
            },
            "logical_cpu_count": 4,
            "machine": "x86_64",
            "parallel_jobs": 4,
            "system": "Linux",
        },
        "identity": {
            **identities[cohort],
            "cohort": cohort,
            "parallel_jobs": 4,
        },
        "samples": [
            {
                "ctest_returncode": 0,
                "index": index,
                "load_after": load,
                "load_before": load,
            }
            for index in range(1, sample_count + 1)
        ],
        "schema": "intrinsic.test-timing/v1",
        "selection_digest": "0" * 64,
        "summary": {
            "sample_count": sample_count,
            "selected_test_count": len(cases),
            "valid": True,
        },
        "test_cost_data": {
            "reset_before_each_sample": True,
            "restored_after_collection": True,
        },
    }


def _junit(
    records: tuple[tuple[str, str], ...],
) -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', "<testsuite>"]
    for name, outcome in records:
        lines.append(f'<testcase name="{name}" status="run">')
        if outcome:
            lines.append(f'<{outcome} message="{outcome}"/>')
        lines.append("</testcase>")
    lines.append("</testsuite>")
    return "\n".join(lines) + "\n"


class TestCohortParityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.manifest = {
            "added_fast_sentinels": ["Heavy.Sentinel"],
            "moved_to_slow": ["Heavy.Stress"],
            "schema": "intrinsic.test-cohort-transition/v1",
        }
        baseline_cases = [
            _case("Alpha.Keep", ("core", "unit")),
            _case("Heavy.Stress", ("geometry", "unit")),
        ]
        candidate_cases = [
            _case("Alpha.Keep", ("core", "unit")),
            _case("Heavy.Sentinel", ("geometry", "unit")),
        ]
        self.baseline_cpu = _report(
            "cpu",
            copy.deepcopy(baseline_cases),
            sha="a" * 40,
        )
        self.baseline_pr_fast = _report(
            "pr-fast",
            copy.deepcopy(baseline_cases),
            sha="a" * 40,
        )
        self.candidate_cpu = _report(
            "cpu",
            copy.deepcopy(candidate_cases),
            sha="b" * 40,
        )
        self.candidate_pr_fast = _report(
            "pr-fast",
            copy.deepcopy(candidate_cases),
            sha="b" * 40,
        )
        self.slow = _report(
            "cpu-slow",
            [
                _case("Heavy.Stress", ("geometry", "slow", "unit")),
            ],
            sha="b" * 40,
        )
        self.junit_records = (("Heavy.Stress", ""),)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_json(self, name: str, document: object) -> Path:
        path = self.root / name
        path.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return path

    def _run(
        self,
        *,
        manifest: object | None = None,
        baseline: object | None = None,
        baseline_pr_fast: object | None = None,
        fast: object | None = None,
        candidate_pr_fast: object | None = None,
        slow: object | None = None,
        junit_records: tuple[tuple[str, str], ...] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        manifest_path = self._write_json(
            "manifest.json", self.manifest if manifest is None else manifest
        )
        baseline_path = self._write_json(
            "baseline-cpu.json",
            self.baseline_cpu if baseline is None else baseline,
        )
        baseline_pr_fast_path = self._write_json(
            "baseline-pr-fast.json",
            (
                self.baseline_pr_fast
                if baseline_pr_fast is None
                else baseline_pr_fast
            ),
        )
        fast_path = self._write_json(
            "candidate-cpu.json",
            self.candidate_cpu if fast is None else fast,
        )
        candidate_pr_fast_path = self._write_json(
            "candidate-pr-fast.json",
            (
                self.candidate_pr_fast
                if candidate_pr_fast is None
                else candidate_pr_fast
            ),
        )
        slow_path = self._write_json(
            "slow.json", self.slow if slow is None else slow
        )
        junit_path = self.root / "scheduled-slow.junit.xml"
        junit_path.write_text(
            _junit(self.junit_records if junit_records is None else junit_records),
            encoding="utf-8",
        )
        return subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--manifest",
                str(manifest_path),
                "--baseline-cpu",
                str(baseline_path),
                "--baseline-pr-fast",
                str(baseline_pr_fast_path),
                "--candidate-cpu",
                str(fast_path),
                "--candidate-pr-fast",
                str(candidate_pr_fast_path),
                "--candidate-slow",
                str(slow_path),
                "--scheduled-slow-junit",
                str(junit_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_accepts_declared_transition_and_exact_slow_lane(self) -> None:
        result = self._run()
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {
                "added_fast_sentinel_count": 1,
                "baseline_cpu_case_count": 2,
                "baseline_pr_fast_case_count": 2,
                "candidate_cpu_case_count": 2,
                "candidate_pr_fast_case_count": 2,
                "candidate_slow_case_count": 1,
                "moved_case_count": 1,
            },
        )

    def test_rejects_duplicate_missing_and_overlapping_cases(self) -> None:
        duplicate = copy.deepcopy(self.candidate_cpu)
        duplicate["cases"].append(copy.deepcopy(duplicate["cases"][0]))
        result = self._run(fast=duplicate)
        self.assertEqual(result.returncode, 3)
        self.assertIn("repeats case 'Alpha.Keep'", result.stderr)

        missing = copy.deepcopy(self.candidate_cpu)
        missing["cases"] = [
            case for case in missing["cases"] if case["name"] != "Alpha.Keep"
        ]
        missing["summary"]["selected_test_count"] = len(missing["cases"])
        result = self._run(fast=missing)
        self.assertEqual(result.returncode, 3)
        self.assertIn("removals differ from moved_to_slow", result.stderr)

        extra_slow = copy.deepcopy(self.slow)
        extra_slow["cases"].append(_case("Legacy.Slow", ("runtime", "slow")))
        extra_slow["summary"]["selected_test_count"] = len(extra_slow["cases"])
        result = self._run(slow=extra_slow)
        self.assertEqual(result.returncode, 3)
        self.assertIn("must contain exactly moved_to_slow", result.stderr)

    def test_rejects_undeclared_additions_and_incomplete_moves(self) -> None:
        undeclared = copy.deepcopy(self.candidate_cpu)
        undeclared["cases"].append(_case("Extra.Sentinel", ("core", "unit")))
        undeclared["summary"]["selected_test_count"] = len(undeclared["cases"])
        result = self._run(fast=undeclared)
        self.assertEqual(result.returncode, 3)
        self.assertIn("additions differ from added_fast_sentinels", result.stderr)

        pr_fast_missing_sentinel = copy.deepcopy(self.candidate_pr_fast)
        pr_fast_missing_sentinel["cases"] = [
            case
            for case in pr_fast_missing_sentinel["cases"]
            if case["name"] != "Heavy.Sentinel"
        ]
        pr_fast_missing_sentinel["summary"]["selected_test_count"] = len(
            pr_fast_missing_sentinel["cases"]
        )
        result = self._run(candidate_pr_fast=pr_fast_missing_sentinel)
        self.assertEqual(result.returncode, 3)
        self.assertIn("baseline-to-candidate PR-fast additions", result.stderr)

        incomplete = copy.deepcopy(self.slow)
        incomplete["cases"] = [_case("Other.Slow", ("geometry", "slow", "unit"))]
        incomplete["summary"]["selected_test_count"] = len(incomplete["cases"])
        result = self._run(slow=incomplete)
        self.assertEqual(result.returncode, 3)
        self.assertIn("must contain exactly moved_to_slow", result.stderr)

    def test_rejects_label_drift_and_nonpassing_moved_samples(self) -> None:
        label_drift = copy.deepcopy(self.slow)
        label_drift["cases"][0]["labels"] = ["geometry", "regression", "slow"]
        result = self._run(slow=label_drift)
        self.assertEqual(result.returncode, 3)
        self.assertIn("transition only by adding 'slow'", result.stderr)

        skipped = copy.deepcopy(self.slow)
        skipped["cases"][0]["statuses"][1] = "skipped"
        result = self._run(slow=skipped)
        self.assertEqual(result.returncode, 3)
        self.assertIn("must pass every sample without skips", result.stderr)

        skipped_sentinel = copy.deepcopy(self.candidate_cpu)
        skipped_sentinel["cases"][1]["statuses"][0] = "skipped"
        result = self._run(fast=skipped_sentinel)
        self.assertEqual(result.returncode, 3)
        self.assertIn("candidate CPU sentinel", result.stderr)

        baseline_failed = copy.deepcopy(self.baseline_cpu)
        baseline_failed["cases"][1]["statuses"][2] = "failed"
        result = self._run(baseline=baseline_failed)
        self.assertEqual(result.returncode, 3)
        self.assertIn("baseline CPU case 'Heavy.Stress'", result.stderr)

    def test_rejects_missing_duplicate_or_nonpassing_scheduled_cases(self) -> None:
        result = self._run(junit_records=(("Legacy.Slow", ""),))
        self.assertEqual(result.returncode, 3)
        self.assertIn("omits declared moved cases", result.stderr)

        result = self._run(
            junit_records=(
                ("Heavy.Stress", ""),
                ("Heavy.Stress", ""),
            )
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("repeats testcase 'Heavy.Stress'", result.stderr)

        result = self._run(junit_records=(("Heavy.Stress", "skipped"),))
        self.assertEqual(result.returncode, 3)
        self.assertIn("must pass without skips", result.stderr)

    def test_rejects_manifest_and_report_shape_drift(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["moved_to_slow"] = ["Zulu.Stress", "Alpha.Stress"]
        result = self._run(manifest=manifest)
        self.assertEqual(result.returncode, 3)
        self.assertIn("moved_to_slow must be sorted", result.stderr)

        report = copy.deepcopy(self.candidate_cpu)
        report["summary"]["selected_test_count"] = 99
        result = self._run(fast=report)
        self.assertEqual(result.returncode, 3)
        self.assertIn("but cases contains 2 records", result.stderr)

    def test_rejects_under_sampled_identity_and_run_drift(self) -> None:
        under_sampled = copy.deepcopy(self.candidate_cpu)
        under_sampled["summary"]["sample_count"] = 3
        under_sampled["samples"] = under_sampled["samples"][:3]
        for case in under_sampled["cases"]:
            case["statuses"] = case["statuses"][:3]
            case["duration_us"]["samples"] = case["duration_us"]["samples"][:3]
        result = self._run(fast=under_sampled)
        self.assertEqual(result.returncode, 3)
        self.assertIn("requires at least 5 samples", result.stderr)

        wrong_identity = copy.deepcopy(self.candidate_cpu)
        wrong_identity["identity"]["aggregate"] = "IntrinsicTests"
        result = self._run(fast=wrong_identity)
        self.assertEqual(result.returncode, 3)
        self.assertIn("identity drifted", result.stderr)

        wrong_sha = copy.deepcopy(self.candidate_pr_fast)
        wrong_sha["host"]["github"]["github_sha"] = "c" * 40
        result = self._run(candidate_pr_fast=wrong_sha)
        self.assertEqual(result.returncode, 3)
        self.assertIn("candidate CPU, PR-fast, and slow reports", result.stderr)


if __name__ == "__main__":
    unittest.main()
