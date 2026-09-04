#!/usr/bin/env python3
from __future__ import annotations

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

    @staticmethod
    def _write_semantic_module_fixture(
        root: Path,
    ) -> tuple[Path, Path, Path, list[str], list[str]]:
        source_dir = root / "src"
        build_dir = root / "build"
        provider_dir = build_dir / "CMakeFiles" / "direct.dir"
        consumer_dir = build_dir / "CMakeFiles" / "consumer.dir"
        source_dir.mkdir()
        provider_dir.mkdir(parents=True)
        consumer_dir.mkdir(parents=True)

        source = source_dir / "Direct.cppm"
        header = source_dir / "DirectConfig.hpp"
        source.write_text(
            'module;\n#include "DirectConfig.hpp"\nexport module Direct;\n',
            encoding="utf-8",
        )
        header.write_text("#define DIRECT_VALUE 1\n", encoding="utf-8")

        pcm = provider_dir / "Direct.pcm"
        unrelated_pcm = provider_dir / "Unrelated.pcm"
        pcm.write_bytes(b"nondeterministic-pcm-v1")
        unrelated_pcm.write_bytes(b"unrelated")
        primary_output = "CMakeFiles/direct.dir/Direct.cppm.o"
        provider_ddi = build_dir / f"{primary_output}.ddi"
        provider_ddi.write_text(
            json.dumps(
                {
                    "revision": 0,
                    "rules": [
                        {
                            "primary-output": primary_output,
                            "provides": [
                                {
                                    "is-interface": True,
                                    "logical-name": "Direct",
                                    "source-path": str(source),
                                }
                            ],
                            "requires": [],
                        }
                    ],
                    "version": 1,
                }
            ),
            encoding="utf-8",
        )
        Path(f"{provider_ddi}.d").write_text(
            f"{provider_ddi}: {source} {header}\n",
            encoding="utf-8",
        )
        provider_map = Path(f"{build_dir / primary_output}.modmap")
        provider_map.write_text(f'-fmodule-output="{pcm}"\n', encoding="utf-8")
        (provider_dir / "CXX.dd").write_text(
            "ninja_dyndep_version = 1.0\n"
            f"build {primary_output} | {pcm}: dyndep\n"
            "  restat = 1\n",
            encoding="utf-8",
        )
        (provider_dir / "CXXDependInfo.json").write_text(
            json.dumps(
                {
                    "compiler-frontend-variant": "GNU",
                    "compiler-id": "Clang",
                    "compiler-simulate-id": "",
                    "config": "Debug",
                    "cxx-modules": {
                        primary_output: {
                            "compile-features": ["cxx_std_23"],
                            "compile-options": ["-DDIRECT_OPTION=1"],
                            "definitions": ["DIRECT_VALUE=1"],
                            "include-directories": [str(source_dir)],
                            "source": str(source),
                            "type": "CXX_MODULES",
                        }
                    },
                    "language": "CXX",
                }
            ),
            encoding="utf-8",
        )

        consumer_map = consumer_dir / "Consumer.cpp.o.modmap"
        consumer_map.write_text(
            f'-fmodule-file="Direct={pcm}"\n'
            f'-fmodule-file="Unrelated={unrelated_pcm}"\n',
            encoding="utf-8",
        )
        consumer_map.with_suffix(".ddi").write_text(
            json.dumps(
                {
                    "revision": 0,
                    "rules": [
                        {
                            "primary-output": "CMakeFiles/consumer.dir/Consumer.cpp.o",
                            "requires": [{"logical-name": "Direct"}],
                        }
                    ],
                    "version": 1,
                }
            ),
            encoding="utf-8",
        )
        return (
            build_dir,
            pcm,
            header,
            [
                "clang++",
                "-DDIRECT_VALUE=1",
                "-std=c++23",
                f"@{provider_map}",
                "-o",
                primary_output,
                "-c",
                str(source),
            ],
            ["clang++", f"@{consumer_map}"],
        )

    def test_pr_fast_persists_only_external_ccache_store(self) -> None:
        payload = yaml.safe_load(self._workflow_text())
        job = payload["jobs"]["pr-fast"]
        self.assertEqual(
            job["env"]["CCACHE_DIR"],
            "/tmp/intrinsic-pr-fast-ccache",
        )
        self.assertEqual(job["env"]["CCACHE_MAXSIZE"], "2G")
        self.assertEqual(
            job["env"]["CCACHE_CONFIGPATH"],
            "/tmp/intrinsic-pr-fast-ccache.conf",
        )
        self.assertFalse(
            any("${{ runner." in str(value) for value in job["env"].values())
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
        self.assertIn("preset-ci-fast-sanitizer-", text)
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
            text.index("Configure (ci-fast preset)"),
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
            text.index("Build selected test closure"),
        )
        self.assertLess(
            text.index("Collect ccache stats"),
            text.index("Aggregate gate timing result"),
        )
        self.assertLess(
            text.index("Validate gate timing result"),
            text.index("Save validated ccache store"),
        )
        self.assertIn("--build-dir build/ci-fast", text)
        self.assertIn("--expected-sanitizer none", text)

    def test_pr_fast_cache_has_read_only_repository_permissions(self) -> None:
        payload = yaml.safe_load(self._workflow_text())
        self.assertEqual(payload["permissions"], {"contents": "read"})
        checkout = payload["jobs"]["pr-fast"]["steps"][0]
        self.assertFalse(checkout["with"]["persist-credentials"])
        self.assertEqual(checkout["with"]["fetch-depth"], 0)

    def test_static_ccache_and_timing_regressions_run_in_ci_docs(self) -> None:
        text = CI_DOCS_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("tests/regression/tooling/Test.CcacheWorkflow.py", text)
        self.assertIn("tests/regression/tooling/Test.CiTiming.py", text)
        self.assertIn(
            "tests/regression/tooling/Test.CcacheModuleInvalidationProbe.py", text
        )
        self.assertIn("tests/regression/tooling/Test.TouchedScope.py", text)

    def test_cmake_ccache_launcher_has_explicit_opt_out(self) -> None:
        text = DEPENDENCIES.read_text(encoding="utf-8")
        self.assertIn("option(INTRINSIC_ENABLE_CCACHE", text)
        self.assertIn("if(INTRINSIC_ENABLE_CCACHE)", text)
        self.assertIn("elseif(NOT INTRINSIC_ENABLE_CCACHE)", text)
        self.assertIn("ccache_ci.py", text)
        self.assertIn("--base-extra-file=", text)
        self.assertIn("--repo-root=", text)
        self.assertIn("@global-module-context", text)
        self.assertIn("dependency-local-semantic-v2", text)
        self.assertIn("CMAKE_CXX_FLAGS", text)
        self.assertNotIn("CMakeCache.txt", text)
        self.assertNotIn("build.ninja", text)
        self.assertNotIn("CACHE_VARIABLES", text)

    def test_global_module_context_validation_accepts_exact_schema(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            context = root / ccache_ci.MODULE_CONTEXT_NAME
            context.write_text(
                f"schema_version=3\n{'0' * 64}  @global-module-context\n",
                encoding="utf-8",
            )

            self.assertEqual(ccache_ci._validate_module_context(context), [])

    def test_global_module_context_validation_rejects_extra_records(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            context = root / ccache_ci.MODULE_CONTEXT_NAME
            context.write_text(
                "schema_version=3\n"
                f"{'0' * 64}  @global-module-context\n"
                "unexpected=true\n",
                encoding="utf-8",
            )

            errors = ccache_ci._validate_module_context(context)
            self.assertTrue(any("invalid content" in error for error in errors))

    def test_module_fingerprint_is_semantic_and_dependency_local(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir, direct_pcm, header, provider_command, consumer_command = (
                self._write_semantic_module_fixture(root)
            )
            fingerprint_inputs = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )
            self.assertEqual(len(fingerprint_inputs), 1)
            fingerprint = fingerprint_inputs[0]
            original = fingerprint.read_text(encoding="utf-8")
            self.assertEqual(
                ccache_ci.module_fingerprint_inputs(
                    consumer_command, cwd=build_dir, repo_root=root
                ),
                (fingerprint,),
            )

            direct_pcm.write_bytes(b"nondeterministic-pcm-v2")
            same_fingerprint = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )[0]
            self.assertEqual(same_fingerprint.read_text(encoding="utf-8"), original)

            header.write_text("#define DIRECT_VALUE 2\n", encoding="utf-8")
            changed_fingerprint = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )[0]
            self.assertNotEqual(
                changed_fingerprint.read_text(encoding="utf-8"), original
            )

    def test_module_fingerprint_uses_command_when_cmake_context_omits_flags(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir, _, _, provider_command, _ = self._write_semantic_module_fixture(
                root
            )
            context_path = (
                build_dir / "CMakeFiles" / "direct.dir" / "CXXDependInfo.json"
            )
            context = json.loads(context_path.read_text(encoding="utf-8"))
            module_context = context["cxx-modules"][
                "CMakeFiles/direct.dir/Direct.cppm.o"
            ]
            for key in (
                "compile-features",
                "compile-options",
                "definitions",
                "include-directories",
            ):
                module_context.pop(key)
            context_path.write_text(json.dumps(context), encoding="utf-8")

            original = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )[0].read_text(encoding="utf-8")
            changed_command = [
                "-DDIRECT_VALUE=2" if item == "-DDIRECT_VALUE=1" else item
                for item in provider_command
            ]
            changed = ccache_ci.module_fingerprint_inputs(
                changed_command, cwd=build_dir, repo_root=root
            )[0].read_text(encoding="utf-8")

            self.assertNotEqual(changed, original)

    def test_module_fingerprint_propagates_direct_dependency_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir, direct_pcm, _, provider_command, _ = (
                self._write_semantic_module_fixture(root)
            )
            provider_map = Path(
                next(item[1:] for item in provider_command if item.startswith("@"))
            )
            provider_ddi = provider_map.with_suffix(".ddi")
            lower_pcm = direct_pcm.with_name("Lower.pcm")
            lower_fingerprint = lower_pcm.with_suffix(".ccache-inputs")
            lower_pcm.write_bytes(b"lower-pcm")
            provider_map.write_text(
                provider_map.read_text(encoding="utf-8")
                + f'-fmodule-file="Lower={lower_pcm}"\n',
                encoding="utf-8",
            )
            metadata = json.loads(provider_ddi.read_text(encoding="utf-8"))
            metadata["rules"][0]["requires"] = [{"logical-name": "Lower"}]
            provider_ddi.write_text(json.dumps(metadata), encoding="utf-8")

            lower_fingerprint.write_text(
                json.dumps(
                    {
                        "schema_version": ccache_ci.MODULE_INPUT_SCHEMA_VERSION,
                        "logical_name": "Lower",
                        "semantic_digest": "0" * 64,
                    }
                ),
                encoding="utf-8",
            )
            direct_fingerprint = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )[0]
            original = direct_fingerprint.read_text(encoding="utf-8")

            lower_fingerprint.write_text(
                json.dumps(
                    {
                        "schema_version": ccache_ci.MODULE_INPUT_SCHEMA_VERSION,
                        "logical_name": "Lower",
                        "semantic_digest": "1" * 64,
                    }
                ),
                encoding="utf-8",
            )
            changed = ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )[0]
            self.assertNotEqual(changed.read_text(encoding="utf-8"), original)

    def test_module_fingerprint_fails_closed_on_missing_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module_map = root / "consumer.cpp.o.modmap"
            module_map.write_text("", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "dependency metadata"):
                ccache_ci.module_fingerprint_inputs(
                    ["clang++", f"@{module_map}"],
                    cwd=root,
                    repo_root=root,
                )

    def test_module_map_allows_only_identical_duplicate_mappings(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module_map = root / "consumer.modmap"
            module_map.write_text(
                '-fmodule-file="Direct=Direct.pcm"\n'
                '-fmodule-file="Direct=Direct.pcm"\n',
                encoding="utf-8",
            )
            self.assertEqual(
                ccache_ci._read_module_map(module_map, root),
                {"Direct": (root / "Direct.pcm").resolve()},
            )

            module_map.write_text(
                '-fmodule-file="Direct=Direct.pcm"\n-fmodule-file="Direct=Other.pcm"\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "conflicting"):
                ccache_ci._read_module_map(module_map, root)

    def test_launch_ccache_hashes_base_and_semantic_module_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir, _, _, provider_command, command = (
                self._write_semantic_module_fixture(root)
            )
            ccache_ci.module_fingerprint_inputs(
                provider_command, cwd=build_dir, repo_root=root
            )
            base = root / "base.txt"
            base.write_text("base", encoding="utf-8")
            args = SimpleNamespace(
                ccache=Path("/opt/ccache"),
                base_extra_file=base,
                repo_root=root,
                compiler_command=["--", *command],
            )

            with (
                mock.patch.object(ccache_ci.os, "execvpe") as execute,
                mock.patch.object(ccache_ci.Path, "cwd", return_value=build_dir),
            ):
                self.assertEqual(ccache_ci.launch_ccache(args), 0)

            command, argv, environment = execute.call_args.args
            self.assertEqual(command, "/opt/ccache")
            self.assertEqual(argv, ["/opt/ccache", *args.compiler_command[1:]])
            self.assertEqual(environment["CCACHE_NODIRECT"], "1")
            self.assertEqual(environment["CCACHE_NODEPEND"], "1")
            extra_files = environment["CCACHE_EXTRAFILES"].split(os.pathsep)
            self.assertEqual(extra_files[0], str(base.resolve()))
            self.assertEqual(Path(extra_files[1]).suffix, ".ccache-inputs")
            fingerprint = json.loads(Path(extra_files[1]).read_text(encoding="utf-8"))
            self.assertEqual(fingerprint["logical_name"], "Direct")

    def test_configured_identity_uses_selected_toolchain_and_sanitizer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_CXX_COMPILER:FILEPATH=/opt/llvm/bin/clang++-23\n"
                "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS:FILEPATH="
                "/opt/llvm/bin/clang-scan-deps-23\n"
                "INTRINSIC_SANITIZER_IDENTITY:INTERNAL=asan-ubsan\n",
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
                identity = ccache_ci.configured_identity(build, "asan-ubsan")

        self.assertEqual(identity.compiler, "clang-23")
        self.assertEqual(identity.compiler_key, "clang-23.0.1")
        self.assertEqual(identity.scan_deps_key, "clang-scan-deps-23.0.1")
        self.assertEqual(identity.ccache_key, "ccache-4.9.1")
        self.assertEqual(identity.sanitizer, "asan-ubsan")

    def test_configured_identity_rejects_missing_resolved_sanitizer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_CXX_COMPILER:FILEPATH=/opt/llvm/bin/clang++-23\n"
                "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS:FILEPATH="
                "/opt/llvm/bin/clang-scan-deps-23\n"
                "INTRINSIC_ENABLE_SANITIZERS:BOOL=OFF\n",
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
                with self.assertRaisesRegex(
                    RuntimeError,
                    "INTRINSIC_SANITIZER_IDENTITY",
                ):
                    ccache_ci.configured_identity(build, "none")

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
        self.assertIn("generated Ninja ccache launcher is missing ccache_ci.py", errors)


if __name__ == "__main__":
    unittest.main()
