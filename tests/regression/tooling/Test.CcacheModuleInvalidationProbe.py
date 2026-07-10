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
        self.assertIn("CCACHE_NODEPEND=1", probe.CMAKE_LISTS)
        self.assertIn("CCACHE_NODIRECT=1", probe.CMAKE_LISTS)
        self.assertIn(
            "CCACHE_EXTRAFILES=${PROBE_MODULE_FINGERPRINT}", probe.CMAKE_LISTS
        )
        self.assertNotIn("CCACHE_DEPEND=1", probe.CMAKE_LISTS)
        self.assertIn("PROBE_USE_CCACHE", probe.CMAKE_LISTS)
        self.assertIn("FILE_SET CXX_MODULES", probe.CMAKE_LISTS)

    def test_only_interface_changes_between_fixture_versions(self) -> None:
        self.assertEqual(probe.SOURCE_V1["Probe.cpp"], probe.SOURCE_V2["Probe.cpp"])
        self.assertEqual(probe.SOURCE_V1["main.cpp"], probe.SOURCE_V2["main.cpp"])
        self.assertNotEqual(
            probe.SOURCE_V1["Probe.cppm"], probe.SOURCE_V2["Probe.cppm"]
        )
        self.assertIn("virtual int value() const", probe.SOURCE_V1["Probe.cppm"])
        self.assertNotIn("virtual int bias() const", probe.SOURCE_V1["Probe.cppm"])
        self.assertIn("virtual int bias() const", probe.SOURCE_V2["Probe.cppm"])
        self.assertIn("int extra = 5", probe.SOURCE_V2["Probe.cppm"])
        self.assertEqual(probe.EXPECTED_V1_OUTPUT, "11")
        self.assertEqual(probe.EXPECTED_V2_OUTPUT, "29")

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

    def test_cache_evidence_requires_hits_and_interface_change_misses(self) -> None:
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

        errors, interface_mode = probe._cache_evidence_errors(cold, warm, changed)

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
        errors, _ = probe._cache_evidence_errors(cold, no_hit_warm, changed)
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
        )
        self.assertTrue(
            any("main.cpp" in error and "cache hit" in error for error in errors)
        )

    def test_probe_runs_cached_and_clean_interface_change(self) -> None:
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
        self.assertIn("ccache version 4.9.1", payload["ccache_version"])
        self.assertTrue(payload["parity"]["matched"])
        self.assertEqual(payload["parity"]["cached_interface_change_output"], "29")
        self.assertEqual(payload["parity"]["clean_no_ccache_output"], "29")
        self.assertFalse(payload["cache_mode"]["direct_mode"])
        self.assertFalse(payload["cache_mode"]["depend_mode"])
        self.assertEqual(len(payload["cache_mode"]["extra_files_to_hash"]), 1)
        self.assertEqual(Path(payload["ccache_config_path"]).parent, root / "work")
        self.assertEqual(Path(payload["toolchain"]["cxx"]), toolchain.cxx.absolute())
        self.assertEqual(
            Path(payload["toolchain"]["clang_scan_deps"]),
            toolchain.scan_deps.absolute(),
        )
        self.assertTrue(payload["source_invariance"]["interface_changed"])
        self.assertTrue(payload["source_invariance"]["implementation_unchanged"])
        self.assertTrue(payload["source_invariance"]["consumer_unchanged"])
        self.assertTrue(payload["dependency_invalidation"]["module_dependency_dirty"])
        self.assertTrue(payload["dependency_invalidation"]["consumer_dependency_dirty"])
        self.assertTrue(payload["dependency_invalidation"]["consumer_recompiled"])
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
            },
        )
        for name in ("empty-cache-v1", "restored-cache-interface-change-v2"):
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
        changed_results = {
            invocation["source"]: set(invocation["results"])
            for invocation in scenarios["restored-cache-interface-change-v2"][
                "ccache_invocations"
            ]
        }
        for source in ("Probe.cpp", "main.cpp"):
            self.assertTrue(probe.CACHE_HIT_RESULTS.intersection(warm_results[source]))
            self.assertIn("cache_miss", changed_results[source])
        self.assertIsNone(
            scenarios["clean-no-ccache-interface-change-v2"]["ccache_summary"]
        )
        self.assertIsNone(
            scenarios["clean-no-ccache-interface-change-v2"]["ccache_invocations"]
        )


if __name__ == "__main__":
    unittest.main()
