#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "tools" / "ci" / "ccache_module_invalidation_probe.py"

spec = importlib.util.spec_from_file_location(
    "ccache_module_invalidation_probe", SCRIPT
)
assert spec is not None and spec.loader is not None
probe = importlib.util.module_from_spec(spec)
sys.modules["ccache_module_invalidation_probe"] = probe
spec.loader.exec_module(probe)


class CcacheModuleInvalidationProbeTests(unittest.TestCase):
    def test_fixture_uses_safe_ccache_launcher(self) -> None:
        self.assertIn("PROBE_CCACHE_LAUNCHER", probe.CMAKE_LISTS)
        self.assertIn(
            "--base-extra-file=${PROBE_MODULE_FINGERPRINT}", probe.CMAKE_LISTS
        )
        self.assertIn("--repo-root=${CMAKE_CURRENT_SOURCE_DIR}", probe.CMAKE_LISTS)
        self.assertNotIn("CCACHE_DEPEND=1", probe.CMAKE_LISTS)
        self.assertIn("PROBE_USE_CCACHE", probe.CMAKE_LISTS)
        self.assertIn("FILE_SET CXX_MODULES", probe.CMAKE_LISTS)
        self.assertIn("schema_version=3", probe.CMAKE_LISTS)
        self.assertIn("@global-module-context", probe.CMAKE_LISTS)
        self.assertNotIn("CMakeCache.txt", probe.CMAKE_LISTS)
        self.assertNotIn("build.ninja", probe.CMAKE_LISTS)

    def test_only_interface_changes_between_fixture_versions(self) -> None:
        self.assertEqual(probe.SOURCE_V1["Probe.cpp"], probe.SOURCE_V2["Probe.cpp"])
        self.assertEqual(probe.SOURCE_V1["main.cpp"], probe.SOURCE_V2["main.cpp"])
        self.assertEqual(
            probe.SOURCE_V1["ProbeConfig.hpp"],
            probe.SOURCE_V2["ProbeConfig.hpp"],
        )
        self.assertNotEqual(
            probe.SOURCE_V1["Probe.cppm"], probe.SOURCE_V2["Probe.cppm"]
        )
        self.assertIn("virtual int value() const", probe.SOURCE_V1["Probe.cppm"])
        self.assertNotIn("virtual int bias() const", probe.SOURCE_V1["Probe.cppm"])
        self.assertIn("virtual int bias() const", probe.SOURCE_V2["Probe.cppm"])
        self.assertIn("int extra = 5", probe.SOURCE_V2["Probe.cppm"])
        self.assertEqual(probe.EXPECTED_V1_OUTPUT, "11")
        self.assertEqual(probe.EXPECTED_V2_OUTPUT, "29")
        self.assertEqual(probe.EXPECTED_GMF_MACRO_OUTPUT, "47")
        self.assertEqual(probe.EXPECTED_TARGET_DEFINITION_OUTPUT, "52")
        self.assertEqual(probe.EXPECTED_TARGET_OPTION_OUTPUT, "58")
        self.assertEqual(probe.EXPECTED_GMF_HEADER_OUTPUT, "65")

    def test_changed_macro_is_consumed_only_by_the_gmf_header(self) -> None:
        self.assertIn("PROBE_GMF_VALUE", probe.COMMON_SOURCES["ProbeConfig.hpp"])
        self.assertIn("PROBE_TARGET_GMF_VALUE", probe.COMMON_SOURCES["ProbeConfig.hpp"])
        self.assertIn(
            "PROBE_TARGET_OPTION_VALUE", probe.COMMON_SOURCES["ProbeConfig.hpp"]
        )
        for source in ("Probe.cppm", "Probe.cpp", "main.cpp"):
            self.assertNotIn("PROBE_GMF_VALUE", probe.SOURCE_V2[source])
            self.assertNotIn("PROBE_TARGET_GMF_VALUE", probe.SOURCE_V2[source])
            self.assertNotIn("PROBE_TARGET_OPTION_VALUE", probe.SOURCE_V2[source])
        self.assertIn('#include "ProbeConfig.hpp"', probe.PROBE_INTERFACE_V2)
        self.assertIn("PROBE_GMF_BASE", probe.PROBE_INTERFACE_V2)
        self.assertIn("target_compile_definitions(probe PRIVATE", probe.CMAKE_LISTS)
        self.assertIn("target_compile_options(probe PRIVATE", probe.CMAKE_LISTS)

    @staticmethod
    def _scenario(
        name: str,
        results: dict[str, tuple[str, ...]],
        *,
        hits: int,
        misses: int,
    ) -> probe.ScenarioResult:
        return probe.ScenarioResult(
            name=name,
            source_version="v1",
            use_ccache=True,
            expected_output="11",
            observed_output="11",
            ccache_summary={
                "hit_count": hits,
                "miss_count": misses,
                "cache_size_kib": 1,
                "error_count": 0,
            },
            ccache_invocations=tuple(
                probe.CcacheInvocation(source, outcome)
                for source, outcome in results.items()
            ),
            dependency_explanations=(),
        )

    def test_cache_evidence_requires_hits_and_all_module_input_misses(self) -> None:
        cold = self._scenario(
            "empty-cache-v1",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        warm = self._scenario(
            "restored-cache-unchanged-v1",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("preprocessed_cache_hit",),
                "main.cpp": ("preprocessed_cache_hit",),
            },
            hits=2,
            misses=0,
        )
        changed = self._scenario(
            "restored-cache-interface-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        macro_changed = self._scenario(
            "restored-cache-gmf-macro-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        target_definition_changed = self._scenario(
            "restored-cache-target-definition-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        target_option_changed = self._scenario(
            "restored-cache-target-option-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        header_changed = self._scenario(
            "restored-cache-gmf-header-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )

        errors, interface_mode = probe._cache_evidence_errors(
            cold,
            warm,
            changed,
            macro_changed,
            target_definition_changed,
            target_option_changed,
            header_changed,
        )

        self.assertEqual(errors, [])
        self.assertEqual(interface_mode, "compiler-pass-through")

        no_hit_warm = self._scenario(
            "restored-cache-unchanged-v1",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("cache_miss",),
                "main.cpp": ("cache_miss",),
            },
            hits=0,
            misses=2,
        )
        errors, _ = probe._cache_evidence_errors(
            cold,
            no_hit_warm,
            changed,
            macro_changed,
            target_definition_changed,
            target_option_changed,
            header_changed,
        )
        self.assertTrue(any("zero cache hits" in error for error in errors))
        self.assertTrue(
            any("main.cpp" in error and "cache hit" in error for error in errors)
        )

        unsupported_consumer_warm = self._scenario(
            "restored-cache-unchanged-v1",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("preprocessed_cache_hit",),
                "main.cpp": ("unsupported_source_language",),
            },
            hits=1,
            misses=0,
        )
        errors, _ = probe._cache_evidence_errors(
            cold,
            unsupported_consumer_warm,
            changed,
            macro_changed,
            target_definition_changed,
            target_option_changed,
            header_changed,
        )
        self.assertTrue(
            any("main.cpp" in error and "cache hit" in error for error in errors)
        )

        stale_macro = self._scenario(
            "restored-cache-gmf-macro-change-v2",
            {
                "Probe.cppm": ("unsupported_source_language",),
                "Probe.cpp": ("preprocessed_cache_hit",),
                "main.cpp": ("preprocessed_cache_hit",),
            },
            hits=2,
            misses=0,
        )
        errors, _ = probe._cache_evidence_errors(
            cold,
            warm,
            changed,
            stale_macro,
            target_definition_changed,
            target_option_changed,
            header_changed,
        )
        self.assertTrue(
            any("gmf-macro" in error and "main.cpp" in error for error in errors),
            errors,
        )

    def test_probe_runs_cached_and_clean_module_input_changes(self) -> None:
        for tool in ("cmake", "ninja", "ccache"):
            if shutil.which(tool) is None:
                self.skipTest(f"{tool} is not available")
        ccache_version = subprocess.run(
            ["ccache", "--version"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        ).stdout
        if "ccache version 4.9.1" not in ccache_version:
            self.skipTest("integration fixture targets pinned ccache 4.9.1")
        try:
            toolchain = probe.find_clang_toolchain()
        except probe.ProbeError as exc:
            self.skipTest(str(exc))

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "result.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--work-dir",
                    str(root / "work"),
                    "--output",
                    str(output),
                    "--cxx",
                    str(toolchain.cxx),
                    "--scan-deps",
                    str(toolchain.scan_deps),
                ],
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(
                completed.returncode,
                0,
                msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
            )
            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(payload["status"], "passed")
        self.assertEqual(payload["schema_version"], 2)
        self.assertIn("ccache version 4.9.1", payload["ccache_version"])
        self.assertTrue(payload["parity"]["matched"])
        self.assertEqual(payload["parity"]["cached_interface_change_output"], "29")
        self.assertEqual(payload["parity"]["clean_interface_change_output"], "29")
        self.assertTrue(payload["parity"]["interface_matched"])
        self.assertEqual(payload["parity"]["cached_gmf_macro_change_output"], "47")
        self.assertEqual(payload["parity"]["clean_gmf_macro_change_output"], "47")
        self.assertTrue(payload["parity"]["gmf_macro_matched"])
        self.assertEqual(
            payload["parity"]["cached_target_definition_change_output"], "52"
        )
        self.assertEqual(
            payload["parity"]["clean_target_definition_change_output"], "52"
        )
        self.assertTrue(payload["parity"]["target_definition_matched"])
        self.assertEqual(payload["parity"]["cached_target_option_change_output"], "58")
        self.assertEqual(payload["parity"]["clean_target_option_change_output"], "58")
        self.assertTrue(payload["parity"]["target_option_matched"])
        self.assertEqual(payload["parity"]["cached_gmf_header_change_output"], "65")
        self.assertEqual(payload["parity"]["clean_gmf_header_change_output"], "65")
        self.assertTrue(payload["parity"]["gmf_header_matched"])
        self.assertFalse(payload["cache_mode"]["direct_mode"])
        self.assertFalse(payload["cache_mode"]["depend_mode"])
        self.assertEqual(len(payload["cache_mode"]["extra_files_to_hash"]), 1)
        self.assertEqual(
            Path(payload["cache_mode"]["extra_files_to_hash"][0]).name,
            "probe-global-module-context.txt",
        )
        self.assertEqual(Path(payload["ccache_config_path"]).parent, root / "work")
        self.assertEqual(Path(payload["toolchain"]["cxx"]), toolchain.cxx.absolute())
        self.assertEqual(
            Path(payload["toolchain"]["clang_scan_deps"]),
            toolchain.scan_deps.absolute(),
        )
        self.assertTrue(payload["source_invariance"]["interface_changed"])
        self.assertTrue(payload["source_invariance"]["implementation_unchanged"])
        self.assertTrue(payload["source_invariance"]["consumer_unchanged"])
        self.assertTrue(
            payload["source_invariance"]["gmf_macro_change_sources_unchanged"]
        )
        self.assertTrue(
            payload["source_invariance"]["target_definition_change_sources_unchanged"]
        )
        self.assertTrue(
            payload["source_invariance"]["target_option_change_sources_unchanged"]
        )
        self.assertTrue(payload["source_invariance"]["gmf_header_changed_only"])
        for change in (
            "interface_change",
            "gmf_macro_change",
            "target_definition_change",
            "target_option_change",
            "gmf_header_change",
        ):
            invalidation = payload["dependency_invalidation"][change]
            self.assertTrue(invalidation["module_dependency_dirty"])
            self.assertTrue(invalidation["consumer_dependency_dirty"])
            self.assertTrue(invalidation["consumer_recompiled"])
        for source in ("Probe.cpp", "main.cpp"):
            self.assertEqual(
                payload["source_invariance"]["v1"][source],
                payload["source_invariance"]["v2"][source],
            )
        self.assertNotEqual(
            payload["source_invariance"]["v1"]["Probe.cppm"]["sha256"],
            payload["source_invariance"]["v2"]["Probe.cppm"]["sha256"],
        )
        scenarios = {scenario["name"]: scenario for scenario in payload["scenarios"]}
        self.assertEqual(
            set(scenarios),
            {
                "empty-cache-v1",
                "restored-cache-unchanged-v1",
                "restored-cache-interface-change-v2",
                "clean-no-ccache-interface-change-v2",
                "restored-cache-gmf-macro-change-v2",
                "clean-no-ccache-gmf-macro-change-v2",
                "restored-cache-target-definition-change-v2",
                "clean-no-ccache-target-definition-change-v2",
                "restored-cache-target-option-change-v2",
                "clean-no-ccache-target-option-change-v2",
                "restored-cache-gmf-header-change-v2",
                "clean-no-ccache-gmf-header-change-v2",
            },
        )
        for name in (
            "empty-cache-v1",
            "restored-cache-interface-change-v2",
            "restored-cache-gmf-macro-change-v2",
            "restored-cache-target-definition-change-v2",
            "restored-cache-target-option-change-v2",
            "restored-cache-gmf-header-change-v2",
        ):
            summary = scenarios[name]["ccache_summary"]
            self.assertIsNotNone(summary)
            self.assertEqual(summary["error_count"], 0)
            self.assertGreaterEqual(summary["miss_count"], 2)
        warm = scenarios["restored-cache-unchanged-v1"]
        self.assertGreaterEqual(warm["ccache_summary"]["hit_count"], 2)
        warm_results = {
            invocation["source"]: set(invocation["results"])
            for invocation in warm["ccache_invocations"]
        }
        for source in ("Probe.cpp", "main.cpp"):
            self.assertTrue(probe.CACHE_HIT_RESULTS.intersection(warm_results[source]))
            for name in (
                "restored-cache-interface-change-v2",
                "restored-cache-gmf-macro-change-v2",
                "restored-cache-target-definition-change-v2",
                "restored-cache-target-option-change-v2",
                "restored-cache-gmf-header-change-v2",
            ):
                changed_results = {
                    invocation["source"]: set(invocation["results"])
                    for invocation in scenarios[name]["ccache_invocations"]
                }
                self.assertIn("cache_miss", changed_results[source])
        for name in (
            "clean-no-ccache-interface-change-v2",
            "clean-no-ccache-gmf-macro-change-v2",
            "clean-no-ccache-target-definition-change-v2",
            "clean-no-ccache-target-option-change-v2",
            "clean-no-ccache-gmf-header-change-v2",
        ):
            self.assertIsNone(scenarios[name]["ccache_summary"])
            self.assertIsNone(scenarios[name]["ccache_invocations"])


if __name__ == "__main__":
    unittest.main()
