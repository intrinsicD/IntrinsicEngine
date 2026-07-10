#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "pr-fast.yml"
CI_DOCS_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "ci-docs.yml"
SCRIPT = REPO_ROOT / "tools" / "ci" / "ccache_ci.py"
DEPENDENCIES = REPO_ROOT / "cmake" / "Dependencies.cmake"

spec = importlib.util.spec_from_file_location("ccache_ci", SCRIPT)
assert spec is not None and spec.loader is not None
ccache_ci = importlib.util.module_from_spec(spec)
sys.modules["ccache_ci"] = ccache_ci
spec.loader.exec_module(ccache_ci)


class CcacheWorkflowTests(unittest.TestCase):
    @staticmethod
    def _ccache_4_9_1_stats() -> str:
        return "\n".join(
            [
                "direct_cache_hit\t2",
                "preprocessed_cache_hit\t5",
                "cache_miss\t11",
                "cache_size_kibibyte\t1234",
                "bad_input_file\t1",
                "bad_output_file\t2",
                "compiler_check_failed\t3",
                "could_not_find_compiler\t4",
                "error_hashing_extra_file\t5",
                "internal_error\t6",
                "missing_cache_file\t7",
                "modified_input_file\t8",
                "bad_compiler_arguments\t101",
                "compile_failed\t102",
                "compiler_produced_empty_output\t103",
                "compiler_produced_no_output\t104",
                "compiler_produced_stdout\t105",
                "preprocessor_error\t106",
                "remote_storage_error\t107",
                "remote_storage_timeout\t108",
            ]
        )

    def _workflow_text(self) -> str:
        return WORKFLOW.read_text(encoding="utf-8")

    def test_pr_fast_persists_only_external_ccache_store(self) -> None:
        payload = yaml.safe_load(self._workflow_text())
        job = payload["jobs"]["pr-fast"]
        self.assertEqual(
            job["env"]["CCACHE_DIR"],
            "${{ runner.temp }}/intrinsic-pr-fast-ccache",
        )
        self.assertEqual(job["env"]["CCACHE_MAXSIZE"], "2G")
        self.assertEqual(
            job["env"]["CCACHE_CONFIGPATH"],
            "${{ runner.temp }}/intrinsic-pr-fast-ccache.conf",
        )

        text = self._workflow_text()
        restore_step = next(
            step for step in job["steps"] if step.get("id") == "ccache-restore"
        )
        save_step = next(
            step for step in job["steps"] if step.get("uses") == "actions/cache/save@v4"
        )
        self.assertEqual(restore_step["uses"], "actions/cache/restore@v4")
        self.assertEqual(restore_step["with"]["path"], "${{ env.CCACHE_DIR }}")
        self.assertEqual(save_step["with"]["path"], "${{ env.CCACHE_DIR }}")
        self.assertEqual(
            save_step["with"]["key"],
            "${{ steps.ccache-restore.outputs.cache-primary-key }}",
        )
        self.assertIn("ccache=4.9.1-1", text)
        self.assertIn("id: ccache-restore", text)
        self.assertIn("path: ${{ env.CCACHE_DIR }}", text)
        self.assertNotIn("CCACHE_CONFIGPATH }}", restore_step["with"]["path"])

    def test_pr_fast_cache_key_is_safely_namespaced(self) -> None:
        text = self._workflow_text()
        payload = yaml.safe_load(text)
        restore_step = next(
            step
            for step in payload["jobs"]["pr-fast"]["steps"]
            if step.get("id") == "ccache-restore"
        )
        primary_key = restore_step["with"]["key"]
        restore_keys = restore_step["with"]["restore-keys"].splitlines()
        self.assertEqual(len(restore_keys), 1)
        self.assertTrue(primary_key.endswith("-${{ github.sha }}"))
        self.assertEqual(primary_key.removesuffix("${{ github.sha }}"), restore_keys[0])
        self.assertIn("${{ steps.toolchain.outputs.compiler-key }}", text)
        self.assertIn("${{ steps.toolchain.outputs.scan-deps-key }}", text)
        self.assertIn("${{ steps.toolchain.outputs.ccache-key }}", text)
        self.assertIn("sanitizer-${{ steps.toolchain.outputs.sanitizer }}", text)
        self.assertIn("${{ github.sha }}", text)
        self.assertNotIn("-ci-nosan-", text)
        self.assertIn("'CMakePresets.json'", text)
        self.assertIn("'cmake/**/*.cmake'", text)
        self.assertIn("'tools/ci/ccache_module_invalidation_probe.py'", text)
        self.assertIn("'vcpkg.json'", text)
        self.assertIn("'vcpkg-configuration.json'", text)
        self.assertIn("'tools/vcpkg/**'", text)
        self.assertIn("restore-keys:", text)
        self.assertIn("ccache_ci.py configured-identity", text)

    def test_pr_fast_fails_closed_and_publishes_ccache_stats(self) -> None:
        text = self._workflow_text()
        self.assertIn("ccache_ci.py check-config", text)
        self.assertIn('--expected-cache-dir "$CCACHE_DIR"', text)
        self.assertIn("ccache --zero-stats", text)
        self.assertIn("ccache_ci.py write-stats", text)
        self.assertIn("cache_state=cold", text)
        self.assertIn("cache_state=warm", text)
        self.assertIn("cache-matched-key", text)
        self.assertIn("--ccache-stats-json", text)
        self.assertIn("--require-ccache-stats", text)
        self.assertIn("ccache_module_invalidation_probe.py", text)
        self.assertIn("--cxx '${{ steps.toolchain.outputs.compiler-path }}'", text)
        self.assertIn(
            "--scan-deps '${{ steps.toolchain.outputs.scan-deps-path }}'", text
        )
        self.assertIn("ci-ccache-module-invalidation-pr-fast", text)
        self.assertIn("ccache/module-invalidation-probe.json", text)
        self.assertNotIn("steps.ccache-stats.outputs", text)
        self.assertNotIn(" || 0", text)

        self.assertLess(
            text.index("Configure (ci preset)"),
            text.index("Restore compatible ccache store"),
        )
        self.assertLess(
            text.index("Validate ccache pilot mode"),
            text.index("Run module invalidation ccache probe"),
        )
        self.assertLess(
            text.index("Run module invalidation ccache probe"),
            text.index("ccache --zero-stats"),
        )
        self.assertLess(
            text.index("ccache --zero-stats"),
            text.index("Build PR-fast test aggregate"),
        )
        self.assertLess(
            text.index("Collect ccache stats"),
            text.index("Aggregate gate timing result"),
        )
        self.assertLess(
            text.index("Validate layering (strict mode)"),
            text.index("Save validated ccache store"),
        )

    def test_pr_fast_cache_has_read_only_repository_permissions(self) -> None:
        payload = yaml.safe_load(self._workflow_text())
        self.assertEqual(payload["permissions"], {"contents": "read"})
        checkout = payload["jobs"]["pr-fast"]["steps"][0]
        self.assertFalse(checkout["with"]["persist-credentials"])

    def test_static_ccache_and_timing_regressions_run_in_ci_docs(self) -> None:
        text = CI_DOCS_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("tests/regression/tooling/Test.CcacheWorkflow.py", text)
        self.assertIn("tests/regression/tooling/Test.CiTiming.py", text)
        self.assertIn(
            "tests/regression/tooling/Test.CcacheModuleInvalidationProbe.py", text
        )

    def test_cmake_ccache_launcher_has_explicit_opt_out(self) -> None:
        text = DEPENDENCIES.read_text(encoding="utf-8")
        self.assertIn("option(INTRINSIC_ENABLE_CCACHE", text)
        self.assertIn("if(INTRINSIC_ENABLE_CCACHE)", text)
        self.assertIn("elseif(NOT INTRINSIC_ENABLE_CCACHE)", text)
        self.assertIn("CCACHE_NODEPEND=1", text)
        self.assertIn("CCACHE_NODIRECT=1", text)
        self.assertIn("CCACHE_EXTRAFILES=", text)
        self.assertIn("CMAKE_CONFIGURE_DEPENDS", text)

    def test_module_digest_validation_detects_interface_changes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            interface = root / "src" / "Probe.cppm"
            interface.parent.mkdir()
            interface.write_text("export module Probe;\n", encoding="utf-8")
            digest = root / ccache_ci.MODULE_DIGEST_NAME
            digest.write_text(
                "schema_version=1\n"
                f"{hashlib.sha256(interface.read_bytes()).hexdigest()}  src/Probe.cppm\n",
                encoding="utf-8",
            )

            self.assertEqual(ccache_ci._validate_module_digest(digest, root), [])
            interface.write_text(
                "export module Probe;\nexport int changed();\n", encoding="utf-8"
            )
            errors = ccache_ci._validate_module_digest(digest, root)
            self.assertTrue(any("stale" in error for error in errors), errors)

    def test_configured_identity_uses_selected_toolchain_and_sanitizer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_CXX_COMPILER:FILEPATH=/opt/llvm/bin/clang++-23\n"
                "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS:FILEPATH="
                "/opt/llvm/bin/clang-scan-deps-23\n"
                "INTRINSIC_ENABLE_SANITIZERS:BOOL=ON\n",
                encoding="utf-8",
            )
            with mock.patch.object(
                ccache_ci,
                "_run_version",
                side_effect=(
                    "clang version 23.0.1\n",
                    "LLVM version 23.0.1\n",
                    "ccache version 4.9.1\n",
                ),
            ):
                identity = ccache_ci.configured_identity(
                    build, "combined-project-default"
                )

        self.assertEqual(identity.compiler, "clang-23")
        self.assertEqual(identity.compiler_key, "clang-23.0.1")
        self.assertEqual(identity.scan_deps_key, "clang-scan-deps-23.0.1")
        self.assertEqual(identity.ccache_key, "ccache-4.9.1")
        self.assertEqual(identity.sanitizer, "combined-project-default")

    def test_ccache_stat_summary_matches_official_error_counters(self) -> None:
        stats = ccache_ci.parse_print_stats(self._ccache_4_9_1_stats())
        summary = ccache_ci.summarize_stats(stats)
        self.assertEqual(summary.hit_count, 7)
        self.assertEqual(summary.miss_count, 11)
        self.assertEqual(summary.cache_size_kib, 1234)
        self.assertEqual(summary.error_count, 36)

    def test_ccache_stat_summary_rejects_missing_4_9_1_counters(self) -> None:
        stats = ccache_ci.parse_print_stats(
            "direct_cache_hit\t2\npreprocessed_cache_hit\t5\n"
        )
        with self.assertRaisesRegex(ValueError, "missing required counters"):
            ccache_ci.summarize_stats(stats)

    def test_ccache_stat_parser_rejects_ambiguous_values(self) -> None:
        for text in (
            "cache_miss\t1\ncache_miss\t2\n",
            "cache_miss\t-1\n",
            "cache_miss\tnot-a-number\n",
            "\n\t\n",
        ):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    ccache_ci.parse_print_stats(text)

    def test_write_stats_publishes_explicit_availability(self) -> None:
        completed = subprocess.CompletedProcess(
            ["ccache", "--print-stats"],
            0,
            stdout=self._ccache_4_9_1_stats(),
            stderr="",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "stats.json"
            output.write_text('{"stale": true}\n', encoding="utf-8")
            github_output = root / "github-output.txt"
            with mock.patch.object(ccache_ci.subprocess, "run", return_value=completed):
                with mock.patch.dict(
                    os.environ,
                    {
                        "GITHUB_OUTPUT": str(github_output),
                        "GITHUB_STEP_SUMMARY": "",
                    },
                ):
                    returncode = ccache_ci.write_stats(SimpleNamespace(output=output))

            self.assertEqual(returncode, 0)
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(payload["summary"]["error_count"], 36)
            outputs = github_output.read_text(encoding="utf-8").splitlines()
            self.assertIn("error_count=36", outputs)
            self.assertIn("stats_available=true", outputs)

    def test_write_stats_marks_command_failure_unavailable(self) -> None:
        completed = subprocess.CompletedProcess(
            ["ccache", "--print-stats"],
            4,
            stdout="",
            stderr="statistics unavailable",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "stats.json"
            output.write_text('{"stale": true}\n', encoding="utf-8")
            github_output = root / "github-output.txt"
            with mock.patch.object(ccache_ci.subprocess, "run", return_value=completed):
                with mock.patch.dict(
                    os.environ,
                    {"GITHUB_OUTPUT": str(github_output)},
                ):
                    returncode = ccache_ci.write_stats(SimpleNamespace(output=output))

            self.assertEqual(returncode, 4)
            self.assertFalse(output.exists())
            self.assertEqual(
                github_output.read_text(encoding="utf-8"),
                "stats_available=false\n",
            )

    def test_write_stats_marks_parse_failure_unavailable(self) -> None:
        completed = subprocess.CompletedProcess(
            ["ccache", "--print-stats"],
            0,
            stdout="cache_miss\tnot-a-number\n",
            stderr="",
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "stats.json"
            github_output = root / "github-output.txt"
            with mock.patch.object(ccache_ci.subprocess, "run", return_value=completed):
                with mock.patch.dict(
                    os.environ,
                    {"GITHUB_OUTPUT": str(github_output)},
                ):
                    returncode = ccache_ci.write_stats(SimpleNamespace(output=output))

            self.assertEqual(returncode, 2)
            self.assertFalse(output.exists())
            self.assertEqual(
                github_output.read_text(encoding="utf-8"),
                "stats_available=false\n",
            )

    def test_launcher_validation_rejects_missing_module_hash_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build"
            build.mkdir()
            (build / "build.ninja").write_text(
                "  LAUNCHER = /usr/bin/cmake -E env CCACHE_NODIRECT=1 /usr/bin/ccache\n",
                encoding="utf-8",
            )
            original = ccache_ci._run_ccache_config
            ccache_ci._run_ccache_config = lambda key: {
                "cache_dir": str(root / "external-ccache"),
                "max_size": "2.0 GB",
                "direct_mode": "false",
                "depend_mode": "false",
            }[key]
            try:
                errors = ccache_ci.validate_config(
                    build,
                    root / "repo",
                    root / "external-ccache",
                    "2.0 GB",
                )
            finally:
                ccache_ci._run_ccache_config = original
        self.assertIn(
            "generated Ninja ccache launcher is missing CCACHE_NODEPEND=1", errors
        )


if __name__ == "__main__":
    unittest.main()
